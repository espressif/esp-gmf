/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_info.h"
#include "esp_gmf_payload.h"
#include "esp_gmf_task.h"
#include "esp_gmf_data_bus.h"

/**
 * @brief  This GMF I/O abstraction can operate in two modes depending on configuration:
 *
 *         1) Asynchronous I/O (data_bus + task configured)
 *         When the I/O is configured with a data bus (buffer configured) and a task (thread stack > 0 in esp_gmf_io_cfg_t), the framework
 *         creates an internal `io_process` task. That task is responsible for driving the data flow between the actual underlying I/O
 *         (open/seek/close callbacks) and the data bus. In this mode, user-facing APIs such as `esp_gmf_io_acquire_*` / `esp_gmf_io_release_*`
 *         interact with the data bus (buffered I/O). The task performs blocking or non-blocking reads/writes against the real I/O device and
 *         pushes/pulls data through the data bus. This model is suitable when the element needs fixed-size transfers or when decoupling
 *         the producer/consumer timing is desired.
 *
 *         2) Synchronous I/O (no data_bus and no task)
 *         If no data bus and no task are configured, the I/O operates in synchronous mode: calls to `esp_gmf_io_acquire_*` / `esp_gmf_io_release_*`
 *         invoke the I/O driver's callbacks directly and perform actual I/O in the caller's context. Use this mode when the element can safely
 *         perform I/O on the caller thread or when buffering is not needed.
 */

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Handle to a GMF I/O
 */
typedef void *esp_gmf_io_handle_t;

#define ESP_GMF_IO_SCORE_NONE      (0)    /*!< No match (The IO does not support this URL) */
#define ESP_GMF_IO_SCORE_STANDARD  (50)   /*!< Standard match (e.g., matching the URL scheme) */
#define ESP_GMF_IO_SCORE_PERFECT   (100)  /*!< Perfect match (e.g., matching both scheme and extension) */

/**
 * @brief  Enumeration for the direction of a GMF I/O (none, reader, writer)
 */
typedef enum {
    ESP_GMF_IO_DIR_NONE   = 0,  /*!< No direction */
    ESP_GMF_IO_DIR_READER = 1,  /*!< Reader direction */
    ESP_GMF_IO_DIR_WRITER = 2,  /*!< Writer direction */
} esp_gmf_io_dir_t;

/**
 * @brief  Enumeration for the type of data handled by a GMF I/O (byte or block)
 */
typedef enum {
    ESP_GMF_IO_TYPE_BYTE  = 1,  /*!< Byte type */
    ESP_GMF_IO_TYPE_BLOCK = 2,  /*!< Block type */
} esp_gmf_io_type_t;

/**
 * @brief  Buffer configuration for GMF I/O
 */
typedef struct {
    size_t  io_size;      /*!< Each time io size in bytes */
    size_t  buffer_size;  /*!< Buffer size in bytes */
    esp_gmf_err_t (*read_filter)(esp_gmf_io_handle_t obj, void *payload, uint32_t wanted_size, int block_ticks);  /*!< Read filter callback function */
} esp_gmf_io_buffer_cfg_t;

/**
 * @brief  Structure representing I/O speed statistics
 */
typedef struct {
    uint64_t  total_bytes;         /*!< Total bytes */
    uint64_t  last_total_bytes;    /*!< Last total bytes count for interval calculation */
    uint64_t  total_time_ms;       /*!< Total time in milliseconds */
    uint64_t  last_total_time_ms;  /*!< Last update total time in milliseconds */
    uint32_t  current_speed_kbps;  /*!< Current speed in Kbps */
    uint32_t  average_speed_kbps;  /*!< Average speed in Kbps */
} esp_gmf_io_speed_stats_t;

/**
 * @brief  Configuration structure for a GMF I/O
 */
typedef struct {
    esp_gmf_task_config_t    thread;                /*!< Task configuration */
    esp_gmf_io_buffer_cfg_t  buffer_cfg;            /*!< Buffer configuration */
    bool                     enable_speed_monitor;  /*!< Enable speed monitor */
} esp_gmf_io_cfg_t;

/**
 * @brief  Structure representing a GMF I/O
 */
typedef struct esp_gmf_io_ {
    struct esp_gmf_obj_  parent;                                                       /*!< Parent object */
    esp_gmf_err_t (*get_score)(esp_gmf_io_handle_t obj, const char *url, int *score);  /*!< Get score callback function */
    esp_gmf_err_t (*open)(esp_gmf_io_handle_t obj);                                    /*!< Open callback function */
    esp_gmf_err_t (*seek)(esp_gmf_io_handle_t obj, uint64_t data);                     /*!< Seek callback function */
    esp_gmf_err_t (*close)(esp_gmf_io_handle_t obj);                                   /*!< Close callback function */
    esp_gmf_err_t (*reset)(esp_gmf_io_handle_t obj);                                   /*!< Reset callback function */
    esp_gmf_err_t (*reload)(esp_gmf_io_handle_t obj, const char *uri);                 /*!< Reload callback function */

    /*!< Previous close callback function
     *   For some block IO instances, this function can be called before the `close` operation
     */
    esp_gmf_err_t (*prev_close)(esp_gmf_io_handle_t handle);

    esp_gmf_err_io_t (*acquire_read)(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks);   /*!< Acquire read callback function */
    esp_gmf_err_io_t (*release_read)(esp_gmf_io_handle_t handle, void *payload, int block_ticks);                         /*!< Release read callback function */
    esp_gmf_err_io_t (*acquire_write)(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks);  /*!< Acquire write callback function */
    esp_gmf_err_io_t (*release_write)(esp_gmf_io_handle_t handle, void *payload, int block_ticks);                        /*!< Release write callback function */

    esp_gmf_io_cfg_t          io_cfg;
    esp_gmf_task_handle_t     task_hd;          /*!< Task handle */
    esp_gmf_db_handle_t       data_bus;         /*!< Data bus handle for buffer */
    esp_gmf_data_bus_block_t  db_block;         /*!< Data bus block for data bus read/write operation */
    esp_gmf_io_speed_stats_t *speed_stats;      /*!< Speed statistics for this I/O (dynamically allocated when enabled) */
    void                     *evt_group;        /*!< Event group for IO control synchronization */
    esp_gmf_io_dir_t          dir;              /*!< I/O direction */
    esp_gmf_io_type_t         type;             /*!< I/O type */
    esp_gmf_info_file_t       attr;             /*!< File attribute */
    uint64_t                  seek_pos;         /*!< Pending seek position (ESP_GMF_IO_SEEK_POS_INVALID = no pending seek) */
    uint8_t                   _is_done;         /*!< Flag indicating if a done signal has been received */
    uint8_t                   _is_abort;        /*!< Flag indicating if an abort signal has been received */
    uint8_t                   _is_hold;         /*!< Flag indicating if the IO task should hold itself */
    int32_t                   task_timeout_ms;  /*!< Timeout for internal IO task operation (default 1000 ms) */
} esp_gmf_io_t;

/**
 * @brief  Initialize a I/O handle with the given configuration
 *         If the stack size is greater than 0, a GMF task is created with the process job registered
 *
 * @param[in]  handle  GMF I/O handle to initialize
 * @param[in]  cfg     Pointer to the configuration structure
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_MEMORY_LACK  Insufficient memory to perform the job registration
 *       - ESP_GMF_ERR_FAIL         Failed to create thread
 */
esp_gmf_err_t esp_gmf_io_init(esp_gmf_io_handle_t handle, esp_gmf_io_cfg_t *cfg);

/**
 * @brief  Deinitialize a GMF I/O handle, freeing associated resources
 *
 * @param[in]  handle  GMF I/O handle to deinitialize
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_deinit(esp_gmf_io_handle_t handle);

/**
 * @brief  Enable or disable speed monitor for an I/O
 *
 * @param[in]  handle   I/O handle
 * @param[in]  enabled  true to enable, false to disable
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_enable_speed_monitor(esp_gmf_io_handle_t handle, bool enabled);

/**
 * @brief  Get speed statistics for an I/O
 *
 * @param[in]   handle  I/O handle
 * @param[out]  stats   Pointer to store speed statistics
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_get_speed_stats(esp_gmf_io_handle_t handle, esp_gmf_io_speed_stats_t *stats);

/**
 * @brief  Evaluate how well the IO matches a given URL
 *         The score ranges from ESP_GMF_IO_SCORE_NONE to ESP_GMF_IO_SCORE_PERFECT:
 *         - ESP_GMF_IO_SCORE_NONE: No match (The IO does not support this URL).
 *         - ESP_GMF_IO_SCORE_STANDARD: Standard match (e.g., matching the URL scheme like "http://" or "file://").
 *         - ESP_GMF_IO_SCORE_PERFECT: Perfect match (e.g., matching both scheme and file extension, or high priority specialized IO).
 *
 * @param[in]   handle  GMF I/O handle
 * @param[in]   url     URL to evaluate
 * @param[out]  score   Pointer to store the calculated score
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_SUPPORT  The IO does not support scoring
 */
esp_gmf_err_t esp_gmf_io_get_score(esp_gmf_io_handle_t handle, const char *url, int *score);

/**
 * @brief  Open the specific I/O handle, run the thread if it is valid
 *
 * @param[in]  handle  GMF I/O handle to open
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_open(esp_gmf_io_handle_t handle);

/**
 * @brief  Seek to a specific byte position in the specific I/O handle
 *         If the IO thread is invalid, the IO's seek function is called directly (synchronous).
 *         If the IO thread is valid, the seek is deferred to the IO task: the seek position is stored
 *         and the data bus is aborted to wake up the task. The IO task will process the seek at the
 *         beginning of its next iteration. The caller will block and wait for the IO task to complete
 *         the seek operation.
 *
 * @param[in]  handle         GMF I/O handle
 * @param[in]  seek_byte_pos  Byte position to seek to
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_SUPPORT  Not support
 *       - Others                   Indicating failure
 */
esp_gmf_err_t esp_gmf_io_seek(esp_gmf_io_handle_t handle, uint64_t seek_byte_pos);

/**
 * @brief  Close a GMF I/O handle
 *         If the IO task is active, this function will block and wait up to `task_timeout_ms` for the task to finish
 *         If both prev_close and thread are valid, they will be called before executing the IO close operation
 *
 * @param[in]  handle  GMF I/O handle to close
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_close(esp_gmf_io_handle_t handle);

/**
 * @brief  Acquire read access to the specific I/O handle
 *
 * @param[in]   handle       GMF I/O handle
 * @param[out]  load         Pointer to store the acquired payload
 * @param[in]   wanted_size  Desired payload size
 * @param[in]   wait_ticks   Timeout value in ticks, in milliseconds
 *
 * @return
 *       - ESP_GMF_IO_OK       Operation successful (check load->is_done to see if IO is done)
 *       - ESP_GMF_IO_FAIL     Operation failed or invalid arguments
 *       - ESP_GMF_IO_ABORT    Operation aborted
 *       - ESP_GMF_IO_TIMEOUT  Operation timed out
 */
esp_gmf_err_io_t esp_gmf_io_acquire_read(esp_gmf_io_handle_t handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);

/**
 * @brief  Release read access to the specific I/O handle
 *
 * @param[in]  handle      GMF I/O handle
 * @param[in]  load        Pointer to the payload to release
 * @param[in]  wait_ticks  Timeout value in ticks
 *
 * @return
 *       - ESP_GMF_IO_OK       Operation successful (check load->is_done to see if IO is done)
 *       - ESP_GMF_IO_FAIL     Operation failed or invalid arguments
 *       - ESP_GMF_IO_ABORT    Operation aborted
 *       - ESP_GMF_IO_TIMEOUT  Operation timed out
 */
esp_gmf_err_io_t esp_gmf_io_release_read(esp_gmf_io_handle_t handle, esp_gmf_payload_t *load, int wait_ticks);

/**
 * @brief  Acquire write access to the specific I/O handle
 *
 * @param[in]   handle       GMF I/O handle
 * @param[out]  load         Pointer to store the acquired payload
 * @param[in]   wanted_size  Desired payload size
 * @param[in]   wait_ticks   Timeout value in ticks, in milliseconds
 *
 * @return
 *       - ESP_GMF_IO_OK       Operation successful (check load->is_done to see if IO is done)
 *       - ESP_GMF_IO_FAIL     Operation failed or invalid arguments
 *       - ESP_GMF_IO_ABORT    Operation aborted
 *       - ESP_GMF_IO_TIMEOUT  Operation timed out
 */
esp_gmf_err_io_t esp_gmf_io_acquire_write(esp_gmf_io_handle_t handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);

/**
 * @brief  Release write access to the specific I/O handle
 *
 * @param[in]  handle      GMF I/O handle
 * @param[in]  load        Pointer to the payload to release
 * @param[in]  wait_ticks  Timeout value in ticks
 *
 * @return
 *       - ESP_GMF_IO_OK       Operation successful (check load->is_done to see if IO is done)
 *       - ESP_GMF_IO_FAIL     Operation failed or invalid arguments
 *       - ESP_GMF_IO_ABORT    Operation aborted
 *       - ESP_GMF_IO_TIMEOUT  Operation timed out
 */
esp_gmf_err_io_t esp_gmf_io_release_write(esp_gmf_io_handle_t handle, esp_gmf_payload_t *load, int wait_ticks);

/**
 * @brief  Set information for the specific I/O handle
 *
 * @param[in]  handle  GMF I/O handle
 * @param[in]  info    Pointer to the file information structure
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_set_info(esp_gmf_io_handle_t handle, esp_gmf_info_file_t *info);

/**
 * @brief  Get information from the specific I/O handle
 *
 * @param[in]   handle  GMF I/O handle
 * @param[out]  info    Pointer to store the file information
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_get_info(esp_gmf_io_handle_t handle, esp_gmf_info_file_t *info);

/**
 * @brief  Set the URI for the specific I/O handle
 *
 * @param[in]  handle  GMF I/O handle
 * @param[in]  uri     Pointer to the URI string
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 */
esp_gmf_err_t esp_gmf_io_set_uri(esp_gmf_io_handle_t handle, const char *uri);

/**
 * @brief  Get the URI from the specific I/O handle
 *
 * @param[in]   handle  GMF I/O handle
 * @param[out]  uri     Pointer to store the URI string
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_get_uri(esp_gmf_io_handle_t handle, char **uri);

/**
 * @brief  Set the byte position for the specific I/O handle
 *
 * @param[in]  handle    GMF I/O handle
 * @param[in]  byte_pos  Byte position to set
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_set_pos(esp_gmf_io_handle_t handle, uint64_t byte_pos);

/**
 * @brief  Update the byte position for the specific I/O handle
 *
 * @param[in]  handle    GMF I/O handle
 * @param[in]  byte_pos  Byte position to update
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_update_pos(esp_gmf_io_handle_t handle, uint64_t byte_pos);

/**
 * @brief  Get the current byte position from the specific I/O handle
 *
 * @param[in]   handle    GMF I/O handle
 * @param[out]  byte_pos  Pointer to store the current byte position
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_get_pos(esp_gmf_io_handle_t handle, uint64_t *byte_pos);

/**
 * @brief  Set the total size for the specific I/O handle
 *
 * @param[in]  handle      GMF I/O handle
 * @param[in]  total_size  Total size to set
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_set_size(esp_gmf_io_handle_t handle, uint64_t total_size);

/**
 * @brief  Get the total size from the specific I/O handle
 *
 * @param[in]   handle      GMF I/O handle
 * @param[out]  total_size  Pointer to store the total size
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_get_size(esp_gmf_io_handle_t handle, uint64_t *total_size);

/**
 * @brief  Get the filled size in the data bus of the specific I/O handle
 *
 * @param[in]   handle       GMF I/O handle
 * @param[out]  filled_size  Pointer to store the filled size in the data bus
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_SUPPORT  Operation not supported, the I/O instance does not have a configured data bus
 */
esp_gmf_err_t esp_gmf_io_get_db_filled_size(esp_gmf_io_handle_t handle, uint32_t *filled_size);

/**
 * @brief  Get the I/O type of the specific I/O handle
 *
 * @param[in]   handle  GMF I/O handle
 * @param[out]  type    Pointer to store the I/O type
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_get_type(esp_gmf_io_handle_t handle, esp_gmf_io_type_t *type);

/**
 * @brief  Reset IO will do reset the position and size, call the reset function if it is valid, then reset the task and reload IO process job
 *
 * @param[in]  handle  GMF I/O handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_reset(esp_gmf_io_handle_t handle);

/**
 * @brief  Reload Io with a URI. Used primarily for seamless transitions (e.g., Hls segments)
 *         Reload is specailly designed to optimized download efficiency
 *         This function is used to avoid reconnection which is time-consuming when download sequential urls from same host
 *         If the IO has an io_process task, this function will reset the done_write (EOF) flag of data_bus and rerun the task
 *
 * @note  This function should be called after the current IO operation (e.g., reading the resource) has finished.
 *
 * @param[in]  handle  GMF I/O handle
 * @param[in]  uri     Pointer to the URI string
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_SUPPORT  Not support
 *       - Others                   Indicating failure
 */
esp_gmf_err_t esp_gmf_io_reload(esp_gmf_io_handle_t handle, const char *uri);

/**
 * @brief  Mark an I/O as done, cause the `esp_gmf_io_acquire_*` / `esp_gmf_io_release_*` input parameter(load) to have the is_done flag set
 *         If the IO has an io_process task, this function will abort the data_bus and hold the task
 *
 * @note  Typical usage: when the current data source has been fully consumed (e.g. end of a track in a playlist),
 *        call this function to signal downstream consumers that no more data is available.
 *        The IO task will be held and stop producing data until `esp_gmf_io_clear_done` is called.
 *        Caller MUST call `esp_gmf_io_clear_done` before reusing the IO for the next data source,
 *        otherwise the IO task remains permanently held and will not process any further data.
 *
 * @param[in]  handle  GMF I/O handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_done(esp_gmf_io_handle_t handle);

/**
 * @brief  Clear the done flag for an I/O, then `esp_gmf_io_acquire_*` / `esp_gmf_io_release_*` will operate normally
 *         Reset the position to 0
 *         If the IO has an io_process task, this function will reset the data_bus and release the held task
 *
 * @note  Typical usage: after `esp_gmf_io_done` has been called and the caller is ready to start a new data source
 *        (e.g. switching to the next track), call this function to clear the done state, reset position to 0,
 *        and release the held IO task so it can resume producing data.
 *        This function should only be called after `esp_gmf_io_done`; calling it without a prior done has no effect.
 *
 * @param[in]  handle  GMF I/O handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_clear_done(esp_gmf_io_handle_t handle);

/**
 * @brief  Mark an I/O as aborted, then `esp_gmf_io_acquire_*` / `esp_gmf_io_release_*` will return ESP_GMF_IO_ABORT
 *         If the IO has an io_process task, this function will abort the data_bus and hold the task
 *
 * @note  Typical usage: when the IO operation needs to be interrupted immediately (e.g. user-initiated stop,
 *        network error recovery, or switching to a different URI). All ongoing and subsequent acquire/release
 *        calls will return ESP_GMF_IO_ABORT until `esp_gmf_io_clear_abort` is called.
 *        The IO task will be held and stop producing/consuming data.
 *        Caller MUST call `esp_gmf_io_clear_abort` to restore normal operation,
 *        otherwise the IO remains permanently in the aborted state and the task stays held.
 *
 * @param[in]  handle  GMF I/O handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_abort(esp_gmf_io_handle_t handle);

/**
 * @brief  Clear the abort flag for an I/O, then `esp_gmf_io_acquire_*` / `esp_gmf_io_release_*` will operate normally
 *         If the IO has an io_process task, this function will clear abort on the data_bus and release the held task
 *
 * @note  Typical usage: after `esp_gmf_io_abort` has been called and the error condition has been resolved
 *        (e.g. network reconnected, new URI set), call this function to clear the abort state and release
 *        the held IO task so it can resume processing data.
 *        This function should only be called after `esp_gmf_io_abort`; calling it without a prior abort has no effect.
 *
 * @param[in]  handle  GMF I/O handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_clear_abort(esp_gmf_io_handle_t handle);

/**
 * @brief  Set the timeout for internal IO task operation
 *
 * @param[in]  handle      GMF I/O handle
 * @param[in]  timeout_ms  Timeout in milliseconds
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_io_set_task_timeout(esp_gmf_io_handle_t handle, int32_t timeout_ms);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
