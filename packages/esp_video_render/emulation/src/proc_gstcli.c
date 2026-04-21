/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "video_render_proc.h"
#include "video_render_sys.h"
#include "esp_gmf_video_param.h"
#include "esp_log.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#define TAG  "EMU_PROC_GSTCLI"

// gst-launch subprocess implementation of video_render_proc (Linux emulation).
//
// Goal: work on machines that have GStreamer runtime tools installed (gst-launch-1.0 / gst-inspect-1.0)
// without requiring GStreamer *development* packages or linking libgstreamer.
//
// Data flow:
//   caller frame bytes -> child stdin -> gst-launch pipeline -> child stdout -> caller output frame (RGB565)
//
// Notes:
// - Keeps ONE subprocess pipeline alive across multiple `video_render_proc_write()` calls.
// - Rebuilds the pipeline when crop/scale params change or when open() is called.
// - Crop uses videocrop left/top/right/bottom, so input width/height must be known (from in_info/frame).
// - Raw inputs are parsed with rawvideoparse (gst-plugins-base).

typedef struct {
    esp_video_render_frame_info_t  in;
    esp_video_render_frame_info_t  out;
    bool                           opened;

    esp_video_render_write_cb_t  writer;
    void                        *writer_ctx;
    port_acquire                 out_acquire;
    port_release                 out_release;
    void                        *out_ctx;

    emu_gmf_video_element_t  crop_el;
    emu_gmf_video_element_t  scale_el;
    emu_gmf_video_element_t  rotate_el;

    uint8_t  *out_buf;
    uint32_t  out_buf_size;
    bool      is_borrowed;

    // Persistent gst-launch subprocess
    pid_t  child_pid;
    int    child_stdin_fd;   // write -> child stdin
    int    child_stdout_fd;  // read <- child stdout
    int    child_stderr_fd;  // read <- child stderr
    bool   child_running;
    bool   need_rebuild;
    char   last_cmd[2048];

    // Output resolution detect (when out_info width/height is 0)
    bool  need_detect_out;
    bool  detected_out;
} gstcli_proc_t;

static const char *g_gst_launch = NULL;
static const char *g_gst_inspect = NULL;

/* -------------------------------------------------- */
/* Utils                                              */
/* -------------------------------------------------- */

static const char *find_tool_cached(const char *name)
{
    const char *path = getenv("PATH");
    if (!path) {
        return NULL;
    }

    char *dup = strdup(path);
    if (!dup) {
        return NULL;
    }

    char *save = NULL;
    for (char *tok = strtok_r(dup, ":", &save); tok; tok = strtok_r(NULL, ":", &save)) {
        static char buf[512];
        snprintf(buf, sizeof(buf), "%s/%s", tok, name);
        if (access(buf, X_OK) == 0) {
            free(dup);
            return buf;
        }
    }
    free(dup);
    return NULL;
}

static uint32_t bytes_per_pixel(esp_video_render_format_t fmt)
{
    switch (fmt) {
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
            return 2;
        case ESP_VIDEO_RENDER_FORMAT_RGB888:
        case ESP_VIDEO_RENDER_FORMAT_BGR888:
            return 3;
        default:
            return 0;
    }
}

static uint32_t frame_size_bytes(esp_video_render_format_t fmt, uint16_t w, uint16_t h)
{
    uint32_t bpp = bytes_per_pixel(fmt);
    return bpp ? (uint32_t)w * h * bpp : 0;
}

/* -------------------------------------------------- */
/* I/O helpers                                        */
/* -------------------------------------------------- */

static int write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t wr = write(fd, buf + off, len - off);
        if (wr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += wr;
    }
    return 0;
}

static int read_exact(int fd, uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t rd = read(fd, buf + off, len - off);
        if (rd <= 0) {
            return -1;
        }
        off += rd;
    }
    return 0;
}

/* -------------------------------------------------- */
/* gst error monitoring                               */
/* -------------------------------------------------- */

static int gstcli_check_stderr(gstcli_proc_t *p)
{
    char buf[256];
    ssize_t rd = read(p->child_stderr_fd, buf, sizeof(buf) - 1);
    if (rd > 0) {
        buf[rd] = 0;
        fprintf(stderr, TAG ": gst error: %s\n", buf);
        return -1;
    }
    return 0;
}

/* -------------------------------------------------- */
/* Child process management                           */
/* -------------------------------------------------- */

static void gstcli_stop_child(gstcli_proc_t *p)
{
    if (!p->child_running) {
        return;
    }

    if (p->child_stdin_fd >= 0) {
        close(p->child_stdin_fd);
        p->child_stdin_fd = -1;
    }
    if (p->child_stdout_fd >= 0) {
        close(p->child_stdout_fd);
        p->child_stdout_fd = -1;
    }
    if (p->child_stderr_fd >= 0) {
        close(p->child_stderr_fd);
        p->child_stderr_fd = -1;
    }
    if (p->child_pid > 0) {
        (void)kill(p->child_pid, SIGKILL);
        int status;
        while (waitpid(p->child_pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    p->child_running = false;
    p->child_pid = -1;
}

#define PIPE_CLOSE(pipe)  if (pipe[0] >= 0) {  \
    close(pipe[0]);                            \
    pipe[0] = -1;                              \
    }                                          \
    if (pipe[1] >= 0) {                        \
    close(pipe[1]);                            \
    pipe[1] = -1;                              \
}

static int gstcli_start_child(gstcli_proc_t *p, const char *cmd)
{
    int inpipe[2] = {-1, -1}, outpipe[2] = {-1, -1}, errpipe[2] = {-1, -1};
    if (pipe(inpipe) || pipe(outpipe) || pipe(errpipe)) {
        PIPE_CLOSE(inpipe);
        PIPE_CLOSE(outpipe);
        PIPE_CLOSE(errpipe);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        PIPE_CLOSE(inpipe);
        PIPE_CLOSE(outpipe);
        PIPE_CLOSE(errpipe);
        return -1;
    }
    if (pid == 0) {
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        dup2(errpipe[1], STDERR_FILENO);

        close(inpipe[1]);
        close(outpipe[0]);
        close(errpipe[0]);

        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(127);
    }

    close(inpipe[0]);
    close(outpipe[1]);
    close(errpipe[1]);

    fcntl(errpipe[0], F_SETFL, O_NONBLOCK);
    fcntl(outpipe[0], F_SETFL, O_NONBLOCK);
    fcntl(inpipe[1], F_SETFL, O_NONBLOCK);

    p->child_pid = pid;
    p->child_stdin_fd = inpipe[1];
    p->child_stdout_fd = outpipe[0];
    p->child_stderr_fd = errpipe[0];
    p->child_running = true;

    return 0;
}

/* -------------------------------------------------- */
/* Pipeline build                                     */
/* -------------------------------------------------- */

static const char *rawvideoparse_format(esp_video_render_format_t fmt);

static esp_video_render_err_t build_pipeline_cmd(
    gstcli_proc_t *p, char *cmd, size_t sz)
{
    if (!g_gst_launch) {
        g_gst_launch = find_tool_cached("gst-launch-1.0");
    }
    if (!g_gst_launch) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }

    int ow = p->scale_el.dst.width ? p->scale_el.dst.width : p->out.width;
    int oh = p->scale_el.dst.height ? p->scale_el.dst.height : p->out.height;

    int deg = (int)p->rotate_el.degree % 360;
    if (deg < 0) {
        deg += 360;
    }

    if (ow <= 0 || oh <= 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    char src[512];
    int crop_left = p->crop_el.crop.x;
    int crop_top = p->crop_el.crop.y;
    int crop_right = 0;
    int crop_bottom = 0;

    if (p->in.format == ESP_VIDEO_RENDER_FORMAT_MJPEG) {
        snprintf(src, sizeof(src),
                 "fdsrc fd=0 ! jpegparse ! jpegdec ! queue");
        // For decoded formats, crop is relative to decoded dimensions (unknown at build time)
        // Use crop_left/top only, set right/bottom to 0
    } else if (p->in.format == ESP_VIDEO_RENDER_FORMAT_H264) {
        snprintf(src, sizeof(src),
                 "fdsrc fd=0 ! typefind ! decodebin name=d "
                 "d. ! queue");
        // For decoded formats, crop is relative to decoded dimensions (unknown at build time)
        // Use crop_left/top only, set right/bottom to 0
    } else {
        // Raw format: need input dimensions for rawvideoparse and crop calculation
        const char *rfmt = rawvideoparse_format(p->in.format);
        if (!rfmt) {
            return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        }

        int in_w = (int)p->in.width;
        int in_h = (int)p->in.height;
        if (in_w <= 0 || in_h <= 0) {
            return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
        }

        int fps = (p->in.fps > 0) ? (int)p->in.fps : 30;
        snprintf(src, sizeof(src),
                 "fdsrc fd=0 ! rawvideoparse format=%s width=%d height=%d framerate=%d/1 ! queue",
                 rfmt, in_w, in_h, fps);

        // Calculate crop_right and crop_bottom for raw formats
        int crop_w = p->crop_el.crop.width;
        int crop_h = p->crop_el.crop.height;
        if (crop_w <= 0) {
            crop_w = in_w;
        }
        if (crop_h <= 0) {
            crop_h = in_h;
        }

        if (crop_left < 0) {
            crop_left = 0;
        }
        if (crop_top < 0) {
            crop_top = 0;
        }
        if (crop_left + crop_w > in_w) {
            crop_w = in_w - crop_left;
        }
        if (crop_top + crop_h > in_h) {
            crop_h = in_h - crop_top;
        }

        crop_right = in_w - (crop_left + crop_w);
        crop_bottom = in_h - (crop_top + crop_h);
        if (crop_right < 0) {
            crop_right = 0;
        }
        if (crop_bottom < 0) {
            crop_bottom = 0;
        }
    }

    /* Use videoflip (gst-plugins-good/base) for 90° steps — videorotate is in gst-plugins-bad
     * and is often missing (same semantics as former videorotate angle=90|180|270). */
    const char *flip_method = NULL;
    if (deg != 0) {
        switch (deg) {
            case 90:
                flip_method = "clockwise";
                break;
            case 180:
                flip_method = "rotate-180";
                break;
            case 270:
                flip_method = "counterclockwise";
                break;
            default:
                return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        }
    }

    int out_rot_w = ow;
    int out_rot_h = oh;
    if (deg == 90 || deg == 270) {
        out_rot_w = oh;
        out_rot_h = ow;
    }
    printf("After rotate:%d %d %d\n", deg, out_rot_w, out_rot_h);

    if (deg == 0) {
        snprintf(cmd, sz,
                 "%s -q -e "
                 "%s ! "
                 "videocrop left=%d top=%d right=%d bottom=%d ! "
                 "videoscale ! videoconvert ! "
                 "video/x-raw,format=RGB16,width=%d,height=%d ! "
                 "queue max-size-buffers=1 leaky=downstream ! "
                 "fdsink fd=1 sync=false async=false",
                 g_gst_launch,
                 src,
                 crop_left,
                 crop_top,
                 crop_right,
                 crop_bottom,
                 ow, oh);
    } else {
        /* videoflip does not accept RGB16; use BGRx (force with capsfilter) then RGB565 out. */
        snprintf(cmd, sz,
                 "%s -q -e "
                 "%s ! "
                 "videocrop left=%d top=%d right=%d bottom=%d ! "
                 "videoscale ! videoconvert ! "
                 "capsfilter caps=video/x-raw,format=BGRx,width=%d,height=%d ! "
                 "videoflip method=%s ! "
                 "videoconvert ! "
                 "capsfilter caps=video/x-raw,format=RGB16,width=%d,height=%d ! "
                 "queue max-size-buffers=1 leaky=downstream ! "
                 "fdsink fd=1 sync=false async=false",
                 g_gst_launch,
                 src,
                 crop_left,
                 crop_top,
                 crop_right,
                 crop_bottom,
                 ow, oh,
                 flip_method,
                 out_rot_w,
                 out_rot_h);
    }

    p->out.format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    p->out.width = (uint16_t)out_rot_w;
    p->out.height = (uint16_t)out_rot_h;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static bool check_caps_line(int fd, int *w, int *h)
{
    char line[4096];
    ssize_t n = read(fd, line, sizeof(line) - 1);
    if (n <= 0) {
        return false;
    }
    line[n] = 0;
    const char *pw = strstr(line, "width=(int)");
    const char *ph = strstr(line, "height=(int)");
    if (pw && ph) {
        *w = atoi(pw + strlen("width=(int)"));
        *h = atoi(ph + strlen("height=(int)"));
        return (*w > 0 && *h > 0);
    }
    return false;
}

static int run_cmd_detect_dims(const char *cmd,
                               const uint8_t *in, size_t in_len,
                               int *out_w, int *out_h)
{
    if (out_w) {
        *out_w = 0;
    }
    if (out_h) {
        *out_h = 0;
    }
    gstcli_proc_t tmp_proc = {0};
    // Start child process using existing function
    if (gstcli_start_child(&tmp_proc, cmd) != 0) {
        return -1;
    }
    size_t send_len = in_len;
    int w = 0, h = 0;
    bool found = false;
    if (send_len > 200 * 1024) {
        send_len = 200 * 1024;
    }
    int ofst = 0;
    while (ofst < send_len) {
        int to_send = send_len - ofst > 1024 ? 1024 : send_len - ofst;
        ssize_t wr = write(tmp_proc.child_stdin_fd, in + ofst, to_send);
        if (wr < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        ofst += wr;
        if (check_caps_line(tmp_proc.child_stdout_fd, &w, &h) ||
            check_caps_line(tmp_proc.child_stderr_fd, &w, &h)) {
            found = true;
            goto done;
        }
    }
    close(tmp_proc.child_stdin_fd);
    tmp_proc.child_stdin_fd = -1;

    int timeout = 200;
    while (found == false && timeout > 0) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(tmp_proc.child_stdout_fd, &rfds);
        FD_SET(tmp_proc.child_stderr_fd, &rfds);
        struct timeval tv = {.tv_sec = 0, .tv_usec = 50000};
        int max_fd = tmp_proc.child_stdout_fd > tmp_proc.child_stderr_fd ? tmp_proc.child_stdout_fd : tmp_proc.child_stderr_fd;
        int r = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (r <= 0) {
            timeout -= 50;
            continue;
        }
        if (FD_ISSET(tmp_proc.child_stdout_fd, &rfds) &&
            check_caps_line(tmp_proc.child_stdout_fd, &w, &h)) {
            found = true;
            goto done;
        }
        if (FD_ISSET(tmp_proc.child_stderr_fd, &rfds) &&
            check_caps_line(tmp_proc.child_stderr_fd, &w, &h)) {
            found = true;
            goto done;
        }
    }
done:
    if (tmp_proc.child_running) {
        gstcli_stop_child(&tmp_proc);
    }
    if (found == false || w <= 0 || h <= 0) {
        ESP_LOGE(TAG, "Failed to detect resolution");
        return -1;
    }
    if (out_w) {
        *out_w = w;
    }
    if (out_h) {
        *out_h = h;
    }
    return 0;
}

static const char *rawvideoparse_format(esp_video_render_format_t fmt)
{
    switch (fmt) {
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
            return "rgb16";
        case ESP_VIDEO_RENDER_FORMAT_RGB888:
            return "rgb";
        case ESP_VIDEO_RENDER_FORMAT_BGR888:
            return "bgr";
        default:
            return NULL;
    }
}

static esp_video_render_err_t build_detect_cmd(gstcli_proc_t *p, char *out_cmd, size_t out_cmd_sz)
{
    if (!p || !out_cmd) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (!g_gst_launch) {
        g_gst_launch = find_tool_cached("gst-launch-1.0");
    }
    if (!g_gst_launch) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    snprintf(out_cmd, out_cmd_sz,
             "%s -v -e fdsrc fd=0 ! "
             "typefind ! decodebin ! capsfilter name=res_caps ! "
             "fakesink sync=false",
             g_gst_launch);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_create(esp_gmf_pool_handle_t pool, video_render_proc_handle_t *proc)
{
    (void)pool;
    if (!proc) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    gstcli_proc_t *p = (gstcli_proc_t *)video_render_calloc(1, sizeof(*p));
    if (!p) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    p->crop_el.kind = EMU_GMF_VIDEO_EL_CROP;
    p->scale_el.kind = EMU_GMF_VIDEO_EL_SCALE;
    p->rotate_el.kind = EMU_GMF_VIDEO_EL_ROTATE;

    // default crop = full, scale = out_info
    p->child_pid = -1;
    p->child_stdin_fd = -1;
    p->child_stdout_fd = -1;
    p->child_stderr_fd = -1;
    p->child_running = false;
    p->need_rebuild = true;
    p->last_cmd[0] = 0;
    p->need_detect_out = false;
    p->detected_out = false;
    *proc = (video_render_proc_handle_t)p;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_add(video_render_proc_handle_t proc, esp_video_render_proc_type_t *proc_type, uint8_t proc_num)
{
    (void)proc;
    (void)proc_type;
    (void)proc_num;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_open(video_render_proc_handle_t proc,
                                              esp_video_render_frame_info_t *in_frame_info,
                                              esp_video_render_frame_info_t *out_frame_info)
{
    if (!proc || !in_frame_info || !out_frame_info) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    gstcli_proc_t *p = (gstcli_proc_t *)proc;
    if (p->opened) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }

    p->in = *in_frame_info;
    p->out = *out_frame_info;
    p->opened = true;
    p->need_rebuild = true;
    p->crop_el.crop_dirty = true;
    p->scale_el.dst_dirty = true;
    p->need_detect_out = (p->out.width == 0 || p->out.height == 0) ? true : false;
    p->detected_out = false;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_gmf_element_handle_t video_render_proc_get_element(video_render_proc_handle_t proc, esp_video_render_proc_type_t type)
{
    if (!proc) {
        return NULL;
    }
    gstcli_proc_t *p = (gstcli_proc_t *)proc;
    if (type == ESP_VIDEO_RENDER_PROC_CROP) {
        return (esp_gmf_element_handle_t)&p->crop_el;
    }
    if (type == ESP_VIDEO_RENDER_PROC_SCALE) {
        return (esp_gmf_element_handle_t)&p->scale_el;
    }
    if (type == ESP_VIDEO_RENDER_PROC_ROTATE) {
        return (esp_gmf_element_handle_t)&p->rotate_el;
    }
    return NULL;
}

esp_video_render_err_t video_render_proc_get_out_frame_info(video_render_proc_handle_t proc, esp_video_render_frame_info_t *info)
{
    if (!proc || !info) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    gstcli_proc_t *p = (gstcli_proc_t *)proc;
    *info = p->out;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_set_writer(video_render_proc_handle_t proc, esp_video_render_write_cb_t writer, void *ctx)
{
    if (!proc || !writer) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    gstcli_proc_t *p = (gstcli_proc_t *)proc;
    p->writer = writer;
    p->writer_ctx = ctx;
    p->out_acquire = NULL;
    p->out_release = NULL;
    p->out_ctx = NULL;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_set_out_port(video_render_proc_handle_t handle, port_acquire acquire, port_release release, void *ctx)
{
    if (!handle) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    gstcli_proc_t *p = (gstcli_proc_t *)handle;
    p->out_acquire = acquire;
    p->out_release = release;
    p->out_ctx = ctx;
    p->writer = NULL;
    p->writer_ctx = NULL;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_write(video_render_proc_handle_t proc, esp_video_render_frame_t *frame)
{
    if (!proc || !frame || !frame->data || frame->size == 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    gstcli_proc_t *p = (gstcli_proc_t *)proc;
    if (!p->opened) {
        fprintf(stderr, TAG ": proc not opened\n");
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    if (!p->writer && !(p->out_acquire && p->out_release)) {
        fprintf(stderr, TAG ": no writer/out_port set\n");
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }

    // Update input dims from frame if provided (helps when open() used width/height=0)
    if (frame->width > 0 && frame->width != p->in.width) {
        p->in.width = frame->width;
        p->need_rebuild = true;
    }
    if (frame->height > 0 && frame->height != p->in.height) {
        p->in.height = frame->height;
        p->need_rebuild = true;
    }

    if (p->crop_el.crop_dirty || p->scale_el.dst_dirty || p->rotate_el.degree_dirty) {
        p->need_rebuild = true;
    }

    // One-time output resolution detection (when user set out_info to 0x0)
    if (p->need_detect_out && !p->detected_out) {
        char dcmd[2048];
        esp_video_render_err_t drc = build_detect_cmd(p, dcmd, sizeof(dcmd));
        if (drc != ESP_VIDEO_RENDER_ERR_OK) {
            fprintf(stderr, TAG ": build_detect_cmd failed rc=%d\n", drc);
            return drc;
        }
        printf("Detect cmd %s\n", dcmd);
        int dw = 0, dh = 0;
        if (run_cmd_detect_dims(dcmd, frame->data, frame->size, &dw, &dh) != 0) {
            fprintf(stderr, TAG ": failed to detect decoded resolution from gst-launch\n");
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
        ESP_LOGI(TAG, "Detected resolution: %dx%d", dw, dh);
        if (p->out.width == 0) {
            p->out.width = (uint16_t)dw;
        }
        if (p->out.height == 0) {
            p->out.height = (uint16_t)dh;
        }
        p->detected_out = true;
        p->need_rebuild = true;  // ensure pipeline uses new out dims
    }

    char cmd[2048];
    esp_video_render_err_t rc = build_pipeline_cmd(p, cmd, sizeof(cmd));
    if (rc != ESP_VIDEO_RENDER_ERR_OK) {
        fprintf(stderr, TAG ": build_pipeline_cmd failed rc=%d (in=%ux%u fmt=0x%08x out=%ux%u)\n",
                rc, (unsigned)p->in.width, (unsigned)p->in.height, (unsigned)p->in.format,
                (unsigned)p->out.width, (unsigned)p->out.height);
        return rc;
    }
    bool rebuilt = false;
    // (Re)spawn pipeline if needed
    if (!p->child_running || p->need_rebuild || strcmp(p->last_cmd, cmd) != 0) {
        gstcli_stop_child(p);
        if (gstcli_start_child(p, cmd) != 0) {
            fprintf(stderr, TAG ": failed to start gst-launch cmd: %s\n", cmd);
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
        strncpy(p->last_cmd, cmd, sizeof(p->last_cmd) - 1);
        p->last_cmd[sizeof(p->last_cmd) - 1] = 0;
        p->need_rebuild = false;
        p->crop_el.crop_dirty = false;
        p->scale_el.dst_dirty = false;
        p->rotate_el.degree_dirty = false;
        rebuilt = true;
    }
    if (p->out_buf == NULL || rebuilt) {
        /* build_pipeline_cmd() sets p->out to final RGB565 size (including 90/270 swap). */
        int out_w = (int)p->out.width;
        int out_h = (int)p->out.height;
        // Allocate output buffer according to requested out (may be updated by scale_el later).
        uint32_t sz = frame_size_bytes(ESP_VIDEO_RENDER_FORMAT_RGB565, out_w, out_h);
        if (sz == 0) {
            return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        }
        if (sz > p->out_buf_size || p->out_buf == NULL) {
            if (p->out_buf) {
                video_render_free(p->out_buf);
            }
            p->out_buf_size = 0;
            p->out_buf = (uint8_t *)video_render_malloc_align(sz, 64);
            if (!p->out_buf) {
                return ESP_VIDEO_RENDER_ERR_NO_MEM;
            }
            p->out_buf_size = sz;
        }
    }

    // Non-blocking I/O with select() timeout for reading and writing
    int timeout_ms = 10000;  // 10 second total timeout
    int read_filled = 0;
    int write_consumed = 0;
    int maxfd = p->child_stdout_fd > p->child_stdin_fd ? p->child_stdout_fd : p->child_stdin_fd;

    while (timeout_ms > 0) {
        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);

        // Set up read fd if we still need to read
        if (read_filled < (int)p->out_buf_size) {
            FD_SET(p->child_stdout_fd, &rfds);
        }

        // Set up write fd if we still need to write
        if (write_consumed < (int)frame->size) {
            FD_SET(p->child_stdin_fd, &wfds);
        }

        struct timeval tv = {.tv_sec = 0, .tv_usec = 50000};  // 50ms timeout per select
        int r = select(maxfd + 1, &rfds, &wfds, NULL, &tv);
        bool has_err = false;
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            has_err = true;
            break;
        } else if (r == 0) {
            timeout_ms -= 50;  // Decrement timeout
        }
        // Write data if pipe is writable
        if (FD_ISSET(p->child_stdin_fd, &wfds) && write_consumed < (int)frame->size) {
            int left = (int)frame->size - write_consumed;
            int to_write = left > 64 * 1024 ? 64 * 1024 : left;  // Write in 64KB chunks
            ssize_t wr = write(p->child_stdin_fd, frame->data + write_consumed, (size_t)to_write);
            if (wr > 0) {
                write_consumed += (int)wr;
            } else if (wr < 0) {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                    has_err = true;
                }
            }
        }

        // Read data if pipe is readable
        if (FD_ISSET(p->child_stdout_fd, &rfds) && read_filled < (int)p->out_buf_size) {
            int to_read = (int)p->out_buf_size - read_filled;
            ssize_t rd = read(p->child_stdout_fd, p->out_buf + read_filled, (size_t)to_read);
            if (rd > 0) {
                read_filled += (int)rd;
                if (read_filled == (int)p->out_buf_size) {
                    break;
                }
            } else if (rd == 0) {
                has_err = true;
            } else {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                    has_err = true;
                }
            }
        }
        if (has_err) {
            ESP_LOGE(TAG, "gst-launch I/O error %d", errno);
            gstcli_stop_child(p);
            p->need_rebuild = true;
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
    }

    if (read_filled != (int)p->out_buf_size) {
        ESP_LOGE(TAG, "gst-launch read timeout %d", read_filled);
        gstcli_stop_child(p);
        p->need_rebuild = true;
        return ESP_VIDEO_RENDER_ERR_TIMEOUT;
    }
    // Deliver to writer / outport
    if (p->writer) {
        esp_video_render_frame_t out_frame = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = p->out.width,
            .height = p->out.height,
            .data = p->out_buf,
            .size = p->out_buf_size,
        };
        (void)p->writer(&out_frame, p->writer_ctx);
        if (out_frame.data == NULL) {
            p->is_borrowed = true;
        }
    } else {
        esp_gmf_payload_t payload = {0};
        if (p->out_acquire(p->out_ctx, &payload, p->out_buf_size, 0) != ESP_GMF_IO_OK) {
            return ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
        }
        if (payload.buf == NULL) {
            payload.buf = p->out_buf;
            payload.buf_length = p->out_buf_size;
        } else {
            memcpy(payload.buf, p->out_buf, p->out_buf_size);
        }
        payload.valid_size = p->out_buf_size;
        (void)p->out_release(p->out_ctx, &payload, 0);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_close(video_render_proc_handle_t proc)
{
    if (!proc) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    gstcli_proc_t *p = (gstcli_proc_t *)proc;
    gstcli_stop_child(p);
    p->opened = false;
    // Do not free when output buffer is borrowed by writer
    if (p->out_buf && !p->is_borrowed) {
        video_render_free(p->out_buf);
        p->out_buf = NULL;
        p->out_buf_size = 0;
    }
    p->is_borrowed = false;
    return ESP_VIDEO_RENDER_ERR_OK;
}

void video_render_proc_destroy(video_render_proc_handle_t proc)
{
    if (!proc) {
        return;
    }
    (void)video_render_proc_close(proc);
    video_render_free(proc);
}

int esp_gmf_video_param_set_cropped_region(esp_gmf_element_handle_t element, const esp_gmf_video_rgn_t *region)
{
    if (!element || !region) {
        return -1;
    }
    emu_gmf_video_element_t *el = (emu_gmf_video_element_t *)element;
    if (el->kind != EMU_GMF_VIDEO_EL_CROP) {
        return -1;
    }
    el->crop = *region;
    el->crop_dirty = true;
    return 0;
}

int esp_gmf_video_param_set_rotate_angle(esp_gmf_element_handle_t element, uint16_t degree)
{
    if (!element) {
        return -1;
    }
    emu_gmf_video_element_t *el = (emu_gmf_video_element_t *)element;
    if (el->kind != EMU_GMF_VIDEO_EL_ROTATE) {
        return -1;
    }
    el->degree = degree;
    el->degree_dirty = true;
    return 0;
}

int esp_gmf_video_param_set_dst_resolution(esp_gmf_element_handle_t element, const esp_gmf_video_resolution_t *res)
{
    if (!element || !res) {
        return -1;
    }
    emu_gmf_video_element_t *el = (emu_gmf_video_element_t *)element;
    if (el->kind != EMU_GMF_VIDEO_EL_SCALE) {
        return -1;
    }
    el->dst = *res;
    el->dst_dirty = true;
    return 0;
}
