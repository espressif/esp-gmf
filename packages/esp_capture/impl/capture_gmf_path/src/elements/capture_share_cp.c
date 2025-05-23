/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_element.h"
#include "capture_share_copy_el.h"
#include "esp_gmf_err.h"
#include "share_q.h"

static const char *TAG = "GMF_SHARE_COPIER";

// TODO currently only support 2 outputs
#define MAX_QUEUE_NUM     (2)
#define DEFAULT_COPY_NUM  (2)

struct gmf_share_copy;

typedef struct {
    esp_gmf_port_handle_t   port_handle;     /*!< GMF port */
    struct gmf_share_copy  *parent;          /*!< Refer to parent for easy access */
    uint8_t                 fetch_once : 1;  /*!< Whether fetch only once for this port */
} gmf_share_copy_out_port_t;

typedef struct gmf_share_copy {
    struct esp_gmf_element      parent;     /*!< GMF element base  */
    share_q_handle_t            share_q;    /*!< Share queue handle */
    bool                        done;       /*!<Whether already done */
    uint8_t                     copy_num;   /*!< Number of copies */
    gmf_share_copy_out_port_t  *out_ports;  /*!< Refer to parent for easy access */
} gmf_share_copy_t;

static void *simple_get_payload_data(void *item)
{
    esp_gmf_payload_t *load = (esp_gmf_payload_t *)item;
    return load ? load->buf : NULL;
}

static int simple_release_payload(void *item, void *ctx)
{
    esp_gmf_payload_t *load = (esp_gmf_payload_t *)item;
    gmf_share_copy_t *cp = (gmf_share_copy_t *)ctx;
    if (load && cp) {
        esp_gmf_element_handle_t hd = (esp_gmf_element_handle_t)ctx;
        if (load->is_done) {
            cp->done = true;
        }
        esp_gmf_port_t *in = ESP_GMF_ELEMENT_GET(hd)->in;
        if (in->ref_port && in->ref_port->ops.release) {
            // Do release now
            in->ref_port->ops.release(in->ref_port->ctx, load, 0);
        }
        return 0;
    }
    return -1;
}

static esp_gmf_err_io_t in_acquire_fb(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    gmf_share_copy_out_port_t *out_port = (gmf_share_copy_out_port_t *)handle;
    uint8_t port_idx = (uint8_t)(out_port - out_port->parent->out_ports);
    if (!share_q_is_enabled(out_port->parent->share_q, port_idx)) {
        // Report done force pipeline stop
        load->is_done = true;
        load->valid_size = 0;
        return ESP_GMF_IO_OK;
    }
    int ret = share_q_recv(out_port->parent->share_q, port_idx, load);
    if (ret != 0) {
        // Force to be done if receive failed
        load->is_done = true;
        load->valid_size = 0;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t in_release_fb(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    gmf_share_copy_out_port_t *out_port = (gmf_share_copy_out_port_t *)handle;
    if (load->is_done && load->valid_size == 0) {
        return ESP_GMF_IO_OK;
    }
    // No matter port enable or not must release it or will leave acquired but not released
    int ret = share_q_release(out_port->parent->share_q, load);
    return ret;
}

static esp_gmf_job_err_t gmf_share_copy_open(esp_gmf_element_handle_t self, void *para)
{
    gmf_share_copy_t *cp = (gmf_share_copy_t *)self;
    cp->done = false;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t gmf_share_copy_process(esp_gmf_element_handle_t self, void *para)
{
    gmf_share_copy_t *cp = (gmf_share_copy_t *)self;
    esp_gmf_element_handle_t hd = (esp_gmf_element_handle_t)self;
    esp_gmf_port_t *in = ESP_GMF_ELEMENT_GET(hd)->in;
    esp_gmf_payload_t *in_load = NULL;
    // Acquire data from in port
    // TODO force to use 1024 to all right in buf length not set yet
    int ret = esp_gmf_port_acquire_in(in, &in_load, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT, ESP_GMF_MAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Acquire size:%d on in port:%p, ret:%d", (int)in->data_length, in, ret);
        return ret == ESP_GMF_IO_ABORT ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    ret = share_q_add(cp->share_q, in_load);
    if (ret != 0) {
        esp_gmf_port_release_in(in, in_load, 0);
        return ESP_GMF_IO_ABORT;
    }
    if (cp->done) {
        ret = ESP_GMF_JOB_ERR_DONE;
    }
    // Workaround：
    //   Manual add reference count to avoid release and manually release when share q released
    if (in->ref_port && in->ref_port->ops.release) {
        in->ref_port->ref_count++;
    }
    esp_gmf_port_release_in(in, in_load, 0);
    // Manual add reference count
    if (in->ref_port && in->ref_port->ops.release) {
        in->ref_port->ref_count--;
    } else {
        // Wait for all users to release
        share_q_wait_empty(cp->share_q);
    }
    return ret;
}

static esp_gmf_job_err_t gmf_share_copy_close(esp_gmf_element_handle_t self, void *para)
{
    gmf_share_copy_t *cp = (gmf_share_copy_t *)self;
    cp->done = false;
    return ESP_GMF_ERR_OK;
}

static esp_err_t gmf_share_copy_destroy(esp_gmf_element_handle_t self)
{
    gmf_share_copy_t *cp = (gmf_share_copy_t *)self;
    if (cp->share_q) {
        share_q_destroy(cp->share_q);
        cp->share_q = NULL;
    }
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_element_deinit(self);
    if (cp->out_ports) {
        esp_gmf_oal_free(cp->out_ports);
    }
    esp_gmf_oal_free(self);
    return ESP_GMF_ERR_OK;
}

static esp_err_t gmf_share_copy_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return capture_share_copy_el_init((capture_share_copy_el_cfg_t *)cfg, handle);
}

static void gmf_share_copy_enable_fetch_once(gmf_share_copy_t *cp, uint8_t port)
{
    if (cp->out_ports[port].port_handle) {
        share_q_enable_once(cp->share_q, port, cp->out_ports[port].fetch_once);
    }
}

esp_gmf_err_t capture_share_copy_el_enable(esp_gmf_element_handle_t self, uint8_t port, bool enable)
{
    gmf_share_copy_t *cp = (gmf_share_copy_t *)self;
    if (cp == NULL || cp->share_q == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    share_q_enable(cp->share_q, port, enable);
    return ESP_GMF_ERR_OK;
}

esp_gmf_port_handle_t capture_share_copy_el_new_out_port(esp_gmf_element_handle_t self, uint8_t port)
{
    gmf_share_copy_t *cp = (gmf_share_copy_t *)self;
    if (port < cp->copy_num) {
        cp->out_ports[port].port_handle = NEW_ESP_GMF_PORT_IN_BLOCK(in_acquire_fb, in_release_fb,
                                                                    NULL, &cp->out_ports[port], 0, ESP_GMF_MAX_DELAY);
        gmf_share_copy_enable_fetch_once(cp, port);
        return cp->out_ports[port].port_handle;
    }
    return NULL;
}

esp_gmf_err_t capture_share_copy_el_init(capture_share_copy_el_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_ERR_INVALID_ARG);
    gmf_share_copy_t *copier = esp_gmf_oal_calloc(1, sizeof(gmf_share_copy_t));
    ESP_GMF_MEM_CHECK(TAG, copier, return ESP_GMF_ERR_MEMORY_LACK);

    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)copier;
    obj->new_obj = gmf_share_copy_new;
    obj->del_obj = gmf_share_copy_destroy;

    int ret = ESP_GMF_ERR_OK;
    if (config) {
        capture_share_copy_el_cfg_t *copy_cfg = esp_gmf_oal_calloc(1, sizeof(capture_share_copy_el_cfg_t));
        ESP_GMF_MEM_CHECK(TAG, copy_cfg, goto COPIER_FAIL);
        memcpy(copy_cfg, config, sizeof(capture_share_copy_el_cfg_t));
        esp_gmf_obj_set_config(obj, copy_cfg, sizeof(*config));
    }
    ret = esp_gmf_obj_set_tag(obj, "share_copier");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto COPIER_FAIL, "Failed set OBJ tag");

    ESP_GMF_ELEMENT_GET(obj)->ops.open = gmf_share_copy_open;
    ESP_GMF_ELEMENT_GET(obj)->ops.process = gmf_share_copy_process;
    ESP_GMF_ELEMENT_GET(obj)->ops.close = gmf_share_copy_close;

    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_MULTI, 0, 0,
                                      ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ret = esp_gmf_element_init(copier, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto COPIER_FAIL, "Failed to init element");

    // Create share queue resources
    int q_num = config && config->q_number ? config->q_number : MAX_QUEUE_NUM;
    int copy_num = config && config->copies ? config->copies : DEFAULT_COPY_NUM;
    copier->out_ports = (gmf_share_copy_out_port_t *)esp_gmf_oal_calloc(copy_num, sizeof(gmf_share_copy_out_port_t));
    ESP_GMF_MEM_CHECK(TAG, copier->out_ports, goto COPIER_FAIL);

    share_q_cfg_t cfg = {
        .user_count = copy_num,
        .q_count = q_num,
        .item_size = sizeof(esp_gmf_payload_t),
        .get_frame_data = simple_get_payload_data,
        .release_frame = simple_release_payload,
        .ctx = copier,
    };
    copier->share_q = share_q_create(&cfg);
    ESP_GMF_MEM_CHECK(TAG, copier->share_q, goto COPIER_FAIL);

    for (int i = 0; i < copy_num; i++) {
        copier->out_ports[i].parent = copier;
    }
    copier->copy_num = copy_num;
    *handle = obj;
    return ESP_GMF_ERR_OK;
COPIER_FAIL:
    esp_gmf_obj_delete(obj);
    return ESP_GMF_ERR_MEMORY_LACK;
}

esp_gmf_err_t capture_share_copy_el_set_single_fetch(esp_gmf_element_handle_t handle, uint8_t port, bool enable)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_ERR_INVALID_ARG);
    gmf_share_copy_t *cp = (gmf_share_copy_t *)handle;
    if (port < cp->copy_num) {
        cp->out_ports[port].fetch_once = enable;
        gmf_share_copy_enable_fetch_once(cp, port);
        return ESP_GMF_ERR_OK;
    }
    return ESP_GMF_ERR_NOT_FOUND;
}
