/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "host/ble_hs_adv.h"

#include "bt_audio_le_adv_builder.h"

static const char *TAG = "ADV_BUILDER";

/* Maximum single AD element payload (Bluetooth Core Spec) and UUID16 list capacity */
#define EXT_ADV_BUILDER_BLE_AD_PAYLOAD_MAX  254U

/**
 * @brief  Mutable extended advertising data builder.
 */
struct bt_audio_le_adv_builder {
    uint8_t *buffer;         /*!< Advertising data buffer */
    size_t   size;           /*!< Advertising data buffer size */
    size_t   position;       /*!< Current write position */
    uint8_t *uuid16_buffer;  /*!< Deferred 16-bit UUID list buffer */
    size_t   uuid16_len;     /*!< Deferred 16-bit UUID list length */
};

esp_err_t bt_audio_le_adv_builder_add_field(bt_audio_le_adv_builder_t builder, uint8_t type,
                                            const uint8_t *value, size_t value_len)
{
    if (!builder) {
        ESP_LOGE(TAG, "Add field failed: builder is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (value_len > 0 && value == NULL) {
        ESP_LOGE(TAG, "Add field failed: value is NULL with non-zero length");
        return ESP_ERR_INVALID_ARG;
    }
    if (value_len > EXT_ADV_BUILDER_BLE_AD_PAYLOAD_MAX) {
        ESP_LOGE(TAG, "Add field failed: value length %u exceeds maximum", (unsigned)value_len);
        return ESP_ERR_INVALID_ARG;
    }
    size_t required = 1  /* len */ + 1  /* type */ + value_len;
    if (builder->position + required > builder->size) {
        ESP_LOGE(TAG, "Add field failed: advertising buffer full");
        return ESP_ERR_NO_MEM;
    }

    builder->buffer[builder->position++] = (uint8_t)(1 + value_len);
    builder->buffer[builder->position++] = type;
    if (value_len) {
        memcpy(builder->buffer + builder->position, value, value_len);
        builder->position += value_len;
    }
    return ESP_OK;
}

esp_err_t bt_audio_le_adv_builder_init(size_t size, bt_audio_le_adv_builder_t *out_builder)
{
    if (!out_builder) {
        ESP_LOGE(TAG, "Init failed: builder output is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    *out_builder = NULL;

    bt_audio_le_adv_builder_t builder =
        (bt_audio_le_adv_builder_t)heap_caps_calloc_prefer(1, sizeof(struct bt_audio_le_adv_builder), 2,
                                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!builder) {
        ESP_LOGE(TAG, "Init failed: no memory for builder context");
        return ESP_ERR_NO_MEM;
    }
    builder->buffer = heap_caps_calloc_prefer(1, size,
                                              2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL);
    if (!builder->buffer) {
        ESP_LOGE(TAG, "Init failed: no memory for advertising buffer");
        heap_caps_free(builder);
        builder = NULL;
        return ESP_ERR_NO_MEM;
    }
    builder->size = size;
    builder->position = 0;
    builder->uuid16_buffer = heap_caps_calloc_prefer(1, EXT_ADV_BUILDER_BLE_AD_PAYLOAD_MAX,
                                                     2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL);
    if (!builder->uuid16_buffer) {
        ESP_LOGE(TAG, "Init failed: no memory for UUID16 pool");
        heap_caps_free(builder->buffer);
        builder->buffer = NULL;
        heap_caps_free(builder);
        builder = NULL;
        return ESP_ERR_NO_MEM;
    }
    builder->uuid16_len = 0;
    *out_builder = builder;
    return ESP_OK;
}

esp_err_t bt_audio_le_adv_builder_add_service_data(bt_audio_le_adv_builder_t builder, const uint8_t *data, size_t len)
{
    return bt_audio_le_adv_builder_add_field(builder, BLE_HS_ADV_TYPE_SVC_DATA_UUID16, data, len);
}

esp_err_t bt_audio_le_adv_builder_add_service_uuid16(bt_audio_le_adv_builder_t builder, uint16_t uuid)
{
    if (!builder) {
        ESP_LOGE(TAG, "Add UUID16 failed: builder is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (builder->uuid16_len + 2 > EXT_ADV_BUILDER_BLE_AD_PAYLOAD_MAX) {
        ESP_LOGE(TAG, "Add UUID16 failed: UUID list full");
        return ESP_ERR_NO_MEM;
    }
    builder->uuid16_buffer[builder->uuid16_len++] = (uint8_t)(uuid & 0xFF);
    builder->uuid16_buffer[builder->uuid16_len++] = (uint8_t)((uuid >> 8) & 0xFF);
    return ESP_OK;
}

esp_err_t bt_audio_le_adv_builder_add_manufacturer_data(bt_audio_le_adv_builder_t builder,
                                                        const uint8_t *data, size_t len)
{
    return bt_audio_le_adv_builder_add_field(builder, BLE_HS_ADV_TYPE_MFG_DATA, data, len);
}

esp_err_t bt_audio_le_adv_builder_add_name(bt_audio_le_adv_builder_t builder, const char *name)
{
    if (!builder || !name) {
        ESP_LOGE(TAG, "Add name failed: invalid argument");
        return ESP_ERR_INVALID_ARG;
    }
    size_t name_len = strlen(name);
    return bt_audio_le_adv_builder_add_field(builder, BLE_HS_ADV_TYPE_COMP_NAME, (const uint8_t *)name, name_len);
}

esp_err_t bt_audio_le_adv_builder_add_appearance(bt_audio_le_adv_builder_t builder, uint16_t appearance)
{
    if (!builder) {
        ESP_LOGE(TAG, "Add appearance failed: builder is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t v[2];
    v[0] = (uint8_t)(appearance & 0xFF);
    v[1] = (uint8_t)((appearance >> 8) & 0xFF);
    return bt_audio_le_adv_builder_add_field(builder, BLE_HS_ADV_TYPE_APPEARANCE, v, sizeof(v));
}

esp_err_t bt_audio_le_adv_builder_add_tx_power(bt_audio_le_adv_builder_t builder, int8_t tx_power)
{
    if (!builder) {
        ESP_LOGE(TAG, "Add TX power failed: builder is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t v = (uint8_t)tx_power;
    return bt_audio_le_adv_builder_add_field(builder, BLE_HS_ADV_TYPE_TX_PWR_LVL, &v, 1);
}

esp_err_t bt_audio_le_adv_builder_add_flags(bt_audio_le_adv_builder_t builder, uint8_t flags)
{
    if (!builder) {
        ESP_LOGE(TAG, "Add flags failed: builder is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    return bt_audio_le_adv_builder_add_field(builder, BLE_HS_ADV_TYPE_FLAGS, &flags, 1);
}

esp_err_t bt_audio_le_adv_builder_get_total_len(bt_audio_le_adv_builder_t builder, size_t *size)
{
    if (!builder || !size) {
        ESP_LOGE(TAG, "Get total length failed: invalid argument");
        return ESP_ERR_INVALID_ARG;
    }
    size_t extra = (builder->uuid16_len > 0) ? (1  /* len */ + 1  /* type */ + builder->uuid16_len) : 0;
    *size = builder->position + extra;
    return ESP_OK;
}

esp_err_t bt_audio_le_adv_builder_get_buffer(bt_audio_le_adv_builder_t builder, uint8_t *out_buffer,
                                             size_t out_buffer_size, size_t *out_size)
{
    if (!builder || !out_buffer || !out_size) {
        ESP_LOGE(TAG, "Get buffer failed: invalid argument");
        return ESP_ERR_INVALID_ARG;
    }
    size_t extra = (builder->uuid16_len > 0) ? (1  /* len */ + 1  /* type */ + builder->uuid16_len) : 0;
    size_t required_total = builder->position + extra;
    if (required_total > out_buffer_size) {
        ESP_LOGE(TAG, "Get buffer failed: output buffer too small");
        return ESP_ERR_NO_MEM;
    }
    if (out_buffer == builder->buffer && required_total > builder->size) {
        ESP_LOGE(TAG, "Get buffer failed: in-place buffer too small for UUID16 merge");
        return ESP_ERR_NO_MEM;
    }

    if (builder->position > 0) {
        if (out_buffer != builder->buffer) {
            memcpy(out_buffer, builder->buffer, builder->position);
        }
    }

    size_t p = builder->position;
    if (builder->uuid16_len > 0) {
        out_buffer[p++] = (uint8_t)(1 + builder->uuid16_len);
        out_buffer[p++] = BLE_HS_ADV_TYPE_INCOMP_UUIDS16;
        memcpy(out_buffer + p, builder->uuid16_buffer, builder->uuid16_len);
        p += builder->uuid16_len;
        builder->uuid16_len = 0;
    }
    *out_size = p;
    return ESP_OK;
}

void bt_audio_le_adv_builder_deinit(bt_audio_le_adv_builder_t builder)
{
    if (builder) {
        if (builder->buffer) {
            heap_caps_free(builder->buffer);
            builder->buffer = NULL;
        }
        if (builder->uuid16_buffer) {
            heap_caps_free(builder->uuid16_buffer);
            builder->uuid16_buffer = NULL;
        }
        heap_caps_free(builder);
        builder = NULL;
    }
}
