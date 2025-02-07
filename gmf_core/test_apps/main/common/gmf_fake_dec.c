/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_info.h"
#include "gmf_fake_dec.h"
#include "mock_dec.h"

static const char *TAG = "FAKE_DEC";

typedef struct {
    esp_gmf_audio_element_t parent;
    bool                    is_opened;
    uint64_t                data_size;
    uint64_t                filter[2];
    mock_dec_handle_t       mock_hd;
    char                    fake_name[32];
    mock_dec_el_args_t      args;
} fake_decoder_t;

static esp_gmf_job_err_t fake_dec_open(esp_gmf_audio_element_handle_t self, void *para)
{
    ESP_LOGW(TAG, "fake_dec_open, %p-%s", self, OBJ_GET_TAG(self));
    fake_decoder_t *dec = (fake_decoder_t *)self;
    mock_dec_open(&dec->mock_hd);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t fake_dec_process(esp_gmf_audio_element_handle_t self, void *para)
{
    esp_gmf_element_handle_t hd = (esp_gmf_element_handle_t)self;
    esp_gmf_port_t *in_port = ESP_GMF_ELEMENT_GET(hd)->in;
    esp_gmf_port_t *out_port = ESP_GMF_ELEMENT_GET(hd)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    fake_dec_cfg_t *cfg = OBJ_GET_CFG(hd);
    if (cfg->is_shared == false) {
        esp_gmf_port_enable_payload_share(in_port, false);
    }
    int ret = esp_gmf_port_acquire_in(in_port, &in_load, ESP_GMF_ELEMENT_GET(self)->in_attr.size, portMAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Read data error, port:%p, ret:%d", in_port, ret);
        return ret == ESP_GMF_IO_ABORT ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    if (cfg->is_pass) {
        out_load = in_load;
    }
    ret = esp_gmf_port_acquire_out(out_port, &out_load, ESP_GMF_ELEMENT_GET(self)->out_attr.size, portMAX_DELAY);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ret = esp_gmf_port_release_out(out_port, out_load, portMAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Out port get error, %p,ret:%d", out_port, ret);
        return ret == ESP_GMF_IO_ABORT ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    ESP_LOGD(TAG, "[%p-%s]I:%p,b:%p,s:%d, done:%d; O:%p,b:%p,s:%d, done:%d", hd, OBJ_GET_TAG(hd), in_port, in_load->buf, in_load->valid_size, in_load->is_done,
             out_port, out_load->buf, out_load->valid_size, out_load->is_done);
    ret = esp_gmf_port_release_in(in_port, in_load, portMAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "In port get error, %p,ret:%d", out_port, ret);
        return ret == ESP_GMF_IO_ABORT ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    if (in_load->valid_size > 0) {
        esp_gmf_audio_el_update_file_pos(hd, in_load->valid_size);
    }
    return in_load->is_done ? ESP_GMF_JOB_ERR_DONE : ret;
}

static esp_gmf_job_err_t fake_dec_close(esp_gmf_audio_element_handle_t self, void *para)
{
    ESP_LOGW(TAG, "Closed, %p", self);
    fake_decoder_t *dec = (fake_decoder_t *)self;
    dec->is_opened = false;
    mock_dec_close(dec->mock_hd);
    return ESP_OK;
}

static esp_err_t fake_dec_destroy(esp_gmf_audio_element_handle_t self)
{
    ESP_LOGW(TAG, "Destroyed, %p", self);
    esp_gmf_oal_free(OBJ_GET_CFG(self));
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(self);
    return ESP_OK;
}

static esp_err_t fake_dec_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, cfg, return ESP_ERR_INVALID_ARG);
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_ERR_INVALID_ARG);

    fake_dec_cfg_t *fake_cfg = (fake_dec_cfg_t *)cfg;
    esp_gmf_obj_handle_t new_obj = NULL;
    int ret = fake_dec_init(fake_cfg, &new_obj);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = fake_dec_cast(fake_cfg, new_obj);
    if (ret != ESP_OK) {
        return ret;
    }
    *handle = (void *)new_obj;
    ESP_LOGI(TAG, "New an object,%s-%p", OBJ_GET_TAG(new_obj), new_obj);

    return ESP_OK;
}

esp_err_t fake_dec_init(fake_dec_cfg_t *config, esp_gmf_obj_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, config, return ESP_ERR_INVALID_ARG);
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_ERR_INVALID_ARG);
    fake_decoder_t *fake = esp_gmf_oal_calloc(1, sizeof(fake_decoder_t));
    ESP_GMF_MEM_CHECK(TAG, fake, return ESP_ERR_NO_MEM);
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)fake;
    int ret = esp_gmf_obj_set_config(obj, config, sizeof(*config));

    fake_dec_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_CHECK(TAG, cfg, {esp_gmf_oal_free(fake); return ESP_GMF_ERR_MEMORY_LACK;});
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*cfg));

    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto WAV_DEC_FAIL, "Failed set OBJ configuration");
    if (config->name) {
        ret = esp_gmf_obj_set_tag(obj, config->name);
    } else {
        ret = esp_gmf_obj_set_tag(obj, "fake_dec");
    }
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto WAV_DEC_FAIL, "Failed set OBJ tag");

    obj->new_obj = fake_dec_new;
    obj->del_obj = fake_dec_destroy;
    *handle = obj;

    esp_gmf_element_cfg_t el_cfg = {
        .cb = config->cb,
        .in_attr.cap = ESP_GMF_EL_PORT_CAP_SINGLE,
        .out_attr.cap = ESP_GMF_EL_PORT_CAP_SINGLE,
        .in_attr.type = ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE,
        .out_attr.type = ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE,
        .in_attr.size = config->in_buf_size,
        .out_attr.size = config->out_buf_size,
    };
    ret = esp_gmf_audio_el_init(fake, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto WAV_DEC_FAIL, "Failed Initialize audio el");
    ESP_LOGE(TAG, "Create fake dec,%s-%p, in:%d, out:%d", OBJ_GET_TAG(obj), obj, config->in_buf_size, config->out_buf_size);
    return ESP_OK;

WAV_DEC_FAIL:
    esp_gmf_obj_delete(obj);
    esp_gmf_oal_free(fake);
    return ret;
}

static esp_gmf_err_t __set_para(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *para_desc = arg_desc;
    uint8_t idx = 0;
    memcpy(&idx, buf + para_desc->offset, para_desc->size);
    para_desc = para_desc->next;
    mock_para_t para = {0};
    memcpy(&para, buf + para_desc->offset, sizeof(para));
    ESP_LOGI(TAG, "%s, idx:%d, fc:%ld, type:%ld, %f, %f", __func__, idx, para.fc, para.type, para.q, para.gain);
    int ret = mock_dec_set_para(el->mock_hd, idx, &para);
    return ret;
}

static esp_gmf_err_t __get_para(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *para_desc = arg_desc;

    uint8_t *idx = (uint8_t *)(buf + para_desc->offset);
    para_desc = para_desc->next;
    mock_para_t *para = (mock_para_t *)(buf + para_desc->offset);
    int ret = mock_dec_get_para(el->mock_hd, *idx, para);
    ESP_LOGI(TAG, "%s, idx:%d, fc:%ld, type:%ld, %f, %f", __func__, *idx, para->fc, para->type, para->q, para->gain);
    return ret;
}

static esp_gmf_err_t __set_args(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *args_desc = arg_desc;
    memcpy(&el->args.desc, buf + args_desc->offset, args_desc->size);
    args_desc = args_desc->next;
    memcpy(&el->args.label, buf + args_desc->offset, args_desc->size);
    ESP_LOGI(TAG, "%s, f[a:%x,b:%lx,c:%x],s[a:%x,b:%lx,c:%x],val:%x, name:%s", __func__, el->args.desc.first.a, el->args.desc.first.b, el->args.desc.first.c, el->args.desc.second.d, el->args.desc.second.e, el->args.desc.second.f, el->args.desc.value, el->args.label);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t __get_args(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *args_desc = arg_desc;

    memcpy(buf + args_desc->offset, &el->args.desc, args_desc->size);
    args_desc = args_desc->next;
    memcpy(buf + args_desc->offset, &el->args.label, args_desc->size);
    ESP_LOGI(TAG, "%s, f[a:%x,b:%lx,c:%x],s[a:%x,b:%lx,c:%x],val:%x, name:%s", __func__, el->args.desc.first.a, el->args.desc.first.b, el->args.desc.first.c, el->args.desc.second.d, el->args.desc.second.e, el->args.desc.second.f, el->args.desc.value, el->args.label);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t __set_info(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *info_desc = arg_desc;

    uint32_t sample_rate = 0;
    memcpy(&sample_rate, buf + info_desc->offset, info_desc->size);
    uint16_t channel = 0;
    info_desc = info_desc->next;
    memcpy(&channel, buf + info_desc->offset, info_desc->size);
    uint16_t bits = 0;
    info_desc = info_desc->next;
    memcpy(&bits, buf + info_desc->offset, info_desc->size);
    ESP_LOGI(TAG, "%s, rate:%ld, ch:%d, bit:%d", __func__, sample_rate, channel, bits);
    int ret = mock_dec_set_info(el->mock_hd, sample_rate, channel, bits);
    return ret;
}

static esp_gmf_err_t __get_info(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *info_desc = arg_desc;

    uint32_t *sample_rate = (uint32_t *)(buf + info_desc->offset);
    info_desc = info_desc->next;
    uint16_t *channel = (uint16_t *)(buf + info_desc->offset);
    info_desc = info_desc->next;
    uint16_t *bits = (uint16_t *)(buf + info_desc->offset);
    int ret = mock_dec_get_info(el->mock_hd, sample_rate, channel, bits);
    ESP_LOGI(TAG, "%s, rate:%ld, ch:%d, bit:%d", __func__, *sample_rate, *channel, *bits);
    return ret;
}

static esp_gmf_err_t __set_name(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *name_desc = arg_desc;
    strlcpy(el->fake_name, (char *)buf, name_desc->size);
    ESP_LOGI(TAG, "%s, name:%s", __func__, el->fake_name);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t __get_name(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *name_desc = arg_desc;
    strncpy((char *)buf, el->fake_name, name_desc->size);
    ESP_LOGI(TAG, "%s, name:%s", __func__, el->fake_name);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t __set_size(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *size_desc = arg_desc;

    memcpy(&el->data_size, buf + size_desc->offset, size_desc->size);
    ESP_LOGI(TAG, "%s, data_size:%llx", __func__, el->data_size);
    return ESP_OK;
}

static esp_gmf_err_t __get_size(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *size_desc = arg_desc;

    memcpy(buf + size_desc->offset, &el->data_size, sizeof(size_desc->size));
    ESP_LOGI(TAG, "%s, data_size:%llx", __func__, el->data_size);
    return ESP_OK;
}

static esp_gmf_err_t __set_filter(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *filter_desc = arg_desc;

    uint8_t idx = 0;
    memcpy(&idx, buf + filter_desc->offset, filter_desc->size);
    filter_desc = filter_desc->next;
    memcpy(&el->filter[idx], buf + filter_desc->offset, filter_desc->size);
    ESP_LOGI(TAG, "%s, idx:%d, filter:%llx", __func__, idx, el->filter[idx]);

    return ESP_OK;
}

static esp_gmf_err_t __get_filter(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    fake_decoder_t *el = (fake_decoder_t *)handle;
    esp_gmf_args_desc_t *filter_desc = arg_desc;

    uint8_t idx = 0;
    memcpy(&idx, buf + filter_desc->offset, filter_desc->size);
    filter_desc = filter_desc->next;
    memcpy(buf + filter_desc->offset, &el->filter[idx], filter_desc->size);
    ESP_LOGI(TAG, "%s, idx:%d, filter:%llx", __func__, idx, el->filter[idx]);
    return ESP_OK;
}

esp_err_t fake_dec_cast(fake_dec_cfg_t *config, esp_gmf_obj_handle_t handle)
{
    ESP_GMF_MEM_CHECK(TAG, config, return ESP_ERR_INVALID_ARG);
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_ERR_INVALID_ARG);
    esp_gmf_audio_element_t *fake_el = (esp_gmf_audio_element_t *)handle;
    fake_el->base.ops.open = fake_dec_open;
    fake_el->base.ops.process = fake_dec_process;
    fake_el->base.ops.close = fake_dec_close;

    fake_dec_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_CHECK(TAG, cfg, return ESP_GMF_ERR_MEMORY_LACK);
    memcpy(cfg, config, sizeof(*config));
    // Free memory before overwriting
    esp_gmf_oal_free(OBJ_GET_CFG(handle));
    esp_gmf_obj_set_config(handle, cfg, sizeof(*config));

    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_args_desc_t *pointer_args = NULL;
    esp_gmf_args_desc_t *get_args = NULL;

    //// Register structure, two parameters, one for in, one for out

    esp_gmf_args_desc_append(&pointer_args, "filter_type", ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), offsetof(mock_para_t, type));
    esp_gmf_args_desc_append(&pointer_args, "fc", ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), offsetof(mock_para_t, fc));
    esp_gmf_args_desc_append(&pointer_args, "q", ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), offsetof(mock_para_t, q));
    esp_gmf_args_desc_append(&pointer_args, "gain", ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), offsetof(mock_para_t, gain));
    ESP_GMF_ARGS_DESC_PRINT(pointer_args);

    esp_gmf_args_desc_append(&set_args, "index", ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    esp_gmf_args_desc_append_array(&set_args, "para", pointer_args, sizeof(mock_para_t), sizeof(uint8_t));
    esp_gmf_element_register_method(fake_el, "set_para", __set_para, set_args);
    ESP_GMF_ARGS_DESC_PRINT(set_args);

    esp_gmf_args_desc_copy(set_args, &get_args);
    esp_gmf_element_register_method(fake_el, "get_para", __get_para, get_args);
    ESP_GMF_ARGS_DESC_PRINT(get_args);

    //// Register nested structure
    pointer_args = NULL;
    set_args = NULL;
    esp_gmf_args_desc_t *nested_args = NULL;

    esp_gmf_args_desc_append(&pointer_args, "a", ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), offsetof(mock_args_ldata_t, a));
    esp_gmf_args_desc_append(&pointer_args, "b", ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), offsetof(mock_args_ldata_t, b));
    esp_gmf_args_desc_append(&pointer_args, "c", ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), offsetof(mock_args_ldata_t, c));
    esp_gmf_args_desc_append_array(&nested_args, "first", pointer_args, sizeof(mock_args_ldata_t), offsetof(mock_dec_desc_t, first));
    ESP_GMF_ARGS_DESC_PRINT(nested_args);

    pointer_args = NULL;
    esp_gmf_args_desc_append(&pointer_args, "d", ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), offsetof(mock_args_hdata_t, d));
    esp_gmf_args_desc_append(&pointer_args, "e", ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), offsetof(mock_args_hdata_t, e));
    esp_gmf_args_desc_append(&pointer_args, "f", ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), offsetof(mock_args_hdata_t, f));
    esp_gmf_args_desc_append_array(&nested_args, "second", pointer_args, sizeof(mock_args_hdata_t), offsetof(mock_dec_desc_t, second));
    esp_gmf_args_desc_append(&nested_args, "value", ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), offsetof(mock_dec_desc_t, value));
    ESP_GMF_ARGS_DESC_PRINT(nested_args);

    esp_gmf_args_desc_append_array(&set_args, "desc", nested_args, sizeof(mock_dec_desc_t), offsetof(mock_dec_el_args_t, desc));
    esp_gmf_args_desc_append(&set_args, "label", ESP_GMF_ARGS_TYPE_INT8, sizeof(((fake_decoder_t *)fake_el)->args.label), offsetof(mock_dec_el_args_t, label));
    esp_gmf_element_register_method(fake_el, "set_args", __set_args, set_args);
    ESP_GMF_ARGS_DESC_PRINT(set_args);

    esp_gmf_args_desc_copy(set_args, &get_args);
    esp_gmf_element_register_method(fake_el, "get_args", __get_args, get_args);
    ESP_GMF_ARGS_DESC_PRINT(get_args);

    // Register integer parameters
    set_args = NULL;
    esp_gmf_args_desc_append(&set_args, "rate", ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), 0);
    esp_gmf_args_desc_append(&set_args, "ch", ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), sizeof(uint32_t));
    esp_gmf_args_desc_append(&set_args, "bits", ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), sizeof(uint16_t) + sizeof(uint32_t));
    esp_gmf_args_desc_copy(set_args, &get_args);
    esp_gmf_element_register_method(fake_el, "set_info", __set_info, set_args);
    ESP_GMF_ARGS_DESC_PRINT(get_args);
    esp_gmf_element_register_method(fake_el, "get_info", __get_info, get_args);

    // Register string parameters
    set_args = NULL;
    esp_gmf_args_desc_append(&set_args, "dec_name", ESP_GMF_ARGS_TYPE_INT8, sizeof(((fake_decoder_t *)fake_el)->fake_name), 0);
    esp_gmf_args_desc_copy(set_args, &get_args);
    esp_gmf_element_register_method(fake_el, "set_name", __set_name, set_args);
    ESP_GMF_ARGS_DESC_PRINT(get_args);
    esp_gmf_element_register_method(fake_el, "get_name", __get_name, get_args);

    // Register 64bits integer parameters
    set_args = NULL;
    esp_gmf_args_desc_append(&set_args, "size", ESP_GMF_ARGS_TYPE_UINT64, sizeof(((fake_decoder_t *)fake_el)->data_size), 0);
    esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_ARGS_DESC_PRINT(set_args);
    esp_gmf_element_register_method(fake_el, "set_size", __set_size, set_args);
    esp_gmf_element_register_method(fake_el, "get_size", __get_size, get_args);

    // Register two parameters, one for in, one for out
    set_args = NULL;
    esp_gmf_args_desc_append(&set_args, "index", ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    esp_gmf_args_desc_append(&set_args, "filter", ESP_GMF_ARGS_TYPE_UINT64, sizeof(((fake_decoder_t *)fake_el)->filter[0]), sizeof(uint8_t));
    esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_ARGS_DESC_PRINT(set_args);
    esp_gmf_element_register_method(fake_el, "set_filter", __set_filter, set_args);
    esp_gmf_element_register_method(fake_el, "get_filter", __get_filter, get_args);

    return ESP_OK;
}
