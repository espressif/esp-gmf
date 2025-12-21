/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#if CONFIG_SOC_LCDCAM_CAM_SUPPORTED
#include "esp_cam_ctlr_dvp.h"
#endif  /* CONFIG_SOC_LCDCAM_CAM_SUPPORTED */

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  CSI configuration structure (placeholder)
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         a camera device over CSI interface, including I2C configuration and GPIO pins.
 */
typedef struct {
    const char  *i2c_name;       /*!< I2C bus name */
    uint32_t     i2c_freq;       /*!< I2C frequency */
    gpio_num_t   reset_io;       /*!< GPIO io for reset signal */
    gpio_num_t   pwdn_io;        /*!< GPIO io for power down signal */
    bool         dont_init_ldo;  /*!< If true, MIPI-CSI video device will not initialize the LDO; otherwise, MIPI-CSI video device will initialize the LDO */
} dev_camera_sub_csi_cfg;

/**
 * @brief  SPI configuration structure (placeholder)
 *
 *         This structure is intended to contain configuration parameters for an SPI interface.
 *         Currently, it is a placeholder and will be implemented in the future.
 */
typedef struct {
    // TODO
} dev_camera_sub_spi_cfg;

/**
 * @brief  USB UVC configuration structure (placeholder)
 *
 *         This structure is intended to contain configuration parameters for a USB UVC interface.
 *         Currently, it is a placeholder and will be implemented in the future.
 */
typedef struct {
    // TODO
} dev_camera_sub_usb_uvc_cfg;

/**
 * @brief  Camera configuration structure for DVP interface
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         an camera device over DVP interface, including IO configuration and XCLK frequency.
 */
#ifdef CONFIG_SOC_LCDCAM_CAM_SUPPORTED
typedef struct {
    const char                    *i2c_name;   /*!< I2C bus name */
    uint32_t                       i2c_freq;   /*!< I2C frequency */
    gpio_num_t                     reset_io;   /*!< GPIO io for reset signal */
    gpio_num_t                     pwdn_io;    /*!< GPIO io for power down signal */
    esp_cam_ctlr_dvp_pin_config_t  dvp_io;     /*!< DVP io configuration structure */
    uint32_t                       xclk_freq;  /*!< XCLK frequency in Hz */
} dev_camera_sub_dvp_cfg;
#endif  /* CONFIG_SOC_LCDCAM_CAM_SUPPORTED */

/**
 * @brief  Camera device configuration structure
 *
 *         This structure contains general configuration parameters for an camera device,
 *         including device identification, bus type, and union of interface-specific configurations.
 */
typedef struct {
    const char  *name;      /*!< Device name */
    const char  *type;      /*!< Device type */
    const char  *sub_type;  /*!< Bus type (e.g., "csi", "spi", "dvp", "usb_uvc" etc.) */
    union {
#ifdef CONFIG_SOC_LCDCAM_CAM_SUPPORTED
        dev_camera_sub_dvp_cfg  dvp;          /*!< DVP interface configuration */
#endif /* CONFIG_SOC_LCDCAM_CAM_SUPPORTED */
        dev_camera_sub_csi_cfg      csi;      /*!< CSI interface configuration (placeholder) */
        dev_camera_sub_spi_cfg      spi;      /*!< SPI interface configuration (placeholder) */
        dev_camera_sub_usb_uvc_cfg  usb_uvc;  /*!< USB UVC interface configuration (placeholder) */
    } sub_cfg;
} dev_camera_config_t;

/**
 * @brief  Camera device handle structure
 *
 *         This structure contains the handle for an initialized camera device,
 *         typically used to reference the device in subsequent operations.
 *         The definition of device path can be queried from `esp-video-components`,
 *         for example, when using a CSI camera, you can read the camera's raw data from the dev_path `/dev/video0`.
 *         If the chip supports hardware H.264 encoding, the encoder can be enabled,
 *         and additional mate info can be obtained from the meta_path `/dev/video11`.
 */
typedef struct {
    const char  *dev_path;   /*!< Camera device path or identifier */
    const char  *meta_path;  /*!< Camera metadata path or identifier (if applicable) */
                             /*!< For csi camera, meta_path is ISP path */
} dev_camera_handle_t;

/**
 * @brief  Initialize the camera device with the given configuration
 *
 *         This function initializes an camera device using the provided configuration structure.
 *         It sets up the necessary hardware interfaces (e.g., DVP, CSI, SPI, USB_UVC) and allocates resources
 *         for the device. The resulting device handle can be used for further camera operations.
 *
 * @param[in]   cfg            Pointer to the camera device configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to a variable to receive the dev_camera_handle_t handle
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   error codes   On failure (see esp_err_t for details)
 */
esp_err_t dev_camera_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize the camera device
 *
 *         This function deinitializes the camera device and frees the allocated resources.
 *         It should be called when the device is no longer needed to prevent memory leaks.
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   error codes   On failure (see esp_err_t for details)
 */
esp_err_t dev_camera_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
