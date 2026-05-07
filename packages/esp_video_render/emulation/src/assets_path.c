/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "assets_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static char *strrstr(char *s, const char *sub)
{
    int len = strlen(sub);
    char *matched = NULL;
    char *p = NULL;
    while (p = strstr(s, sub)) {
        matched = p;
        s = p + len;
    }
    return matched;
}

int get_assets_path(const char *filename, char *buffer, size_t buffer_size)
{
    if (!filename || !buffer || buffer_size == 0) {
        return -1;
    }
    char *s = getcwd(buffer, buffer_size);
    if (s == NULL) {
        return -1;
    }
    char *render_dir = strrstr(buffer, "esp_video_render");
    if (render_dir == NULL) {
        return -1;
    }
    int file_len = strlen(filename);
    int len = (render_dir - buffer) + sizeof("esp_video_render") + sizeof("assets") + file_len;
    if (len + 1 > buffer_size) {
        return -1;
    }
    render_dir += sizeof("esp_video_render");
    snprintf(render_dir, buffer_size - (render_dir - buffer), "/assets/%s", filename);
    return 0;
}
