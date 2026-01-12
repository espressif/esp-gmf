/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @note
 *
 *  The iot_button callback is declared as:
 *     typedef void (*button_cb_t)(void *button_handle, void *usr_data);
 *
 *  With dev_button, implement the callback in your project and register it using:
 *     esp_err_t esp_board_device_callback_register(const char *name,
 *                                                  void *call_back_func,
 *                                                  void *user_data);
 *  This registers the same callback for all button events enabled in the YAML.
 *
 *  To register different callbacks for specific events, use the iot_button API directly:
 *     esp_err_t iot_button_unregister_cb(button_handle_t btn_handle,
 *                                        button_event_t event,
 *                                        button_event_args_t *event_args);
 *
 * See https://components.espressif.com/components/espressif/button/versions/4.1.4/readme for details.
 */

/**
 * @brief  Button device handles structure
 *
 *         This structure contains all the handles needed to interact with a  button device,
 *         including the button handle(s). For single button, num_buttons = 1 and button_handles[0]
 *         is the handle. For multiple buttons, num_buttons > 1 and button_handles array holds all handles.
 */
typedef struct {
    uint8_t          num_buttons;                                               /*!< Number of button handles */
    button_handle_t  button_handles[CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL];  /*!< Flexible array of button handles */
} dev_button_handles_t;

/**
 * @brief  Button events bitfield structure
 *
 *         This structure defines a bitfield for enabling/disabling various button events.
 *         Each bit represents a specific button event that can be enabled or disabled.
 */
typedef struct {
    uint16_t  press_down        : 1;  /*!< Enable press down event */
    uint16_t  press_up          : 1;  /*!< Enable press up event */
    uint16_t  single_click      : 1;  /*!< Enable single click event */
    uint16_t  double_click      : 1;  /*!< Enable double click event */
    uint16_t  multi_click       : 1;  /*!< Enable multi click event */
    uint16_t  long_press_start  : 1;  /*!< Enable long press start event */
    uint16_t  long_press_hold   : 1;  /*!< Enable long press hold event */
    uint16_t  long_press_up     : 1;  /*!< Enable long press up event */
    uint16_t  press_repeat      : 1;  /*!< Enable press repeat event */
    uint16_t  press_repeat_done : 1;  /*!< Enable press repeat done event */
    uint16_t  press_end         : 1;  /*!< Enable press end event */
    uint16_t  reserved          : 5;  /*!< Reserved for future use */
} dev_button_events_flags_t;

/**
 * @brief  Button events configuration structure
 *
 *         This structure contains configuration parameters for button events that require
 *         additional data, such as multi‑click counts and long‑press durations.
 *         The enabled_events bitfield indicates which events are enabled; the corresponding
 *         arrays (click_counts, durations_ms) provide the specific values for those events.
 */
typedef struct {
    dev_button_events_flags_t enabled_events;  /*!< Bitfield of enabled button events */
    struct {
        uint8_t  *click_counts;  /*!< Array of click counts for multi‑click events, like support for event like triple click and so on */
        int       count;         /*!< Number of entries in click_counts array */
    } multi_click;               /*!< Configuration for multi‑click events */
    struct {
        uint16_t  *durations_ms;  /*!< Array of durations (in milliseconds) for long‑press‑start events */
        int        count;         /*!< Number of entries in durations_ms array */
    } long_press_start;           /*!< Configuration for long‑press‑start events */
    struct {
        uint16_t  *durations_ms;  /*!< Array of durations (in milliseconds) for long‑press‑up events */
        int        count;         /*!< Number of entries in durations_ms array */
    } long_press_up;              /*!< Configuration for long‑press‑up events */
} dev_button_events_config_t;

/**
 * @brief  GPIO button sub-configuration structure
 *
 *         This structure contains configuration parameters specific to GPIO-based buttons.
 *         It defines the GPIO peripheral to use and button-specific settings.
 */
typedef struct {
    const char  *gpio_name;          /*!< GPIO peripheral name */
    uint8_t      active_level;       /*!< Active level (0-low, 1-high) */
    bool         enable_power_save;  /*!< Enable power save mode */
    bool         disable_pull;       /*!< Disable internal pull-up/pull-down */
} dev_button_gpio_sub_config_t;

/**
 * @brief  ADC button sub-configuration structure
 *
 *         This structure contains configuration parameters specific to ADC-based buttons.
 *         It defines the ADC peripheral to use and voltage thresholds for button detection.
 */
typedef struct {
    const char *adc_name;  /*!< ADC peripheral name */
    union {
        struct {
            uint8_t   button_index;  /*!< Button index on the channel */
            uint16_t  min_voltage;   /*!< Minimum voltage in mV for button press */
            uint16_t  max_voltage;   /*!< Maximum voltage in mV for button press */
        } single;
        struct {
            uint8_t      button_num;                                               /*!< Number of buttons in the group, should be less than or equal to CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL */
            uint16_t     max_voltage;                                              /*!< Maximum voltage in mV for this ADC channel */
            uint16_t     voltage_range[CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL];  /*!< Array of voltage thresholds in mV */
            const char  *button_labels[CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL];  /*!< Array of button names */
        } multi;
    };
} dev_button_adc_sub_config_t;

/**
 * @brief  Button device configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         a button device, including button type, event configuration, and sub type configuration.
 *         The sub_cfg union allows for type-specific configuration based on the sub_type.
 */
typedef struct {
    const char                 *name;               /*!< Button device name */
    const char                 *sub_type;           /*!< Button type: "gpio", "adc_single", or "adc_multi" */
    button_config_t             button_timing_cfg;  /*!< Button timing configuration */
    dev_button_events_config_t  events_cfg;         /*!< Configuration for button events (enabled events and parameters) */
    union {
        dev_button_gpio_sub_config_t  gpio;  /*!< GPIO button configuration */
        dev_button_adc_sub_config_t   adc;   /*!< ADC button configuration */
    } sub_cfg;                               /*!< Sub device configuration */
} dev_button_config_t;

/**
 * @brief  Initialize the button device with the given configuration
 *
 *         This function initializes a  button device using the provided configuration structure.
 *         It sets up the necessary hardware interfaces (GPIO or ADC) and allocates resources
 *         for the button. The resulting device handle can be used for further button operations.
 *
 * @param[in]   cfg            Pointer to the button configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to a variable to receive the dev_button_handles_t handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_button_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize the button device
 *
 *         This function deinitializes the  button device and frees the allocated resources.
 *         It should be called when the device is no longer needed to prevent memory leaks.
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_button_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
