/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <wchar.h>
#include <stdint.h>
#include "esp_err.h"
#include <freertos/FreeRTOS.h>

#include "usb/usb_host.h"
#include "hid.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum string descriptor length returned by the HID host driver.
 *
 * The maximum possible number of characters in an embedded string is device
 * specific. For USB devices, the maximum string length is 126 wide characters,
 * not including the terminating NUL character.
 *
 * This driver stores at most 32 wide characters per string to reduce memory
 * usage when filling hid_host_dev_info_t via hid_host_get_device_info().
 */
#define HID_STR_DESC_MAX_LENGTH           32

// For backward compatibility with IDF versions which do not have suspend/resume api
#ifdef USB_HOST_LIB_EVENT_FLAGS_AUTO_SUSPEND
/** @brief Indicates that suspend and resume events are available in this build. */
#define HID_HOST_SUSPEND_RESUME_API_SUPPORTED
#endif

// For backward compatibility with IDF versions which do not have remote wakeup HAL changes
#ifdef REMOTE_WAKE_HAL_SUPPORTED
/** @brief Indicates that the remote wakeup API is available in this build. */
#define HID_HOST_REMOTE_WAKE_SUPPORTED
#endif

typedef struct hid_interface *hid_host_device_handle_t;    /*!< Device Handle. Handle to a particular HID interface */

// ------------------------ USB HID Host events --------------------------------
/**
 * @brief USB HID host driver event identifier.
 */
typedef enum {
    HID_HOST_DRIVER_EVENT_CONNECTED = 0x00,        /*!< At least one HID interface was found on a connected device. */
} hid_host_driver_event_t;

/**
 * @brief USB HID host interface event identifier.
 */
typedef enum {
    HID_HOST_INTERFACE_EVENT_INPUT_REPORT = 0x00,     /*!< HID Device input report */
    HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR,          /*!< HID Device transfer error */
    HID_HOST_INTERFACE_EVENT_DISCONNECTED,            /*!< HID Device has been disconnected */
#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED
    HID_HOST_INTERFACE_EVENT_SUSPENDED,               /*!< HID Device has been suspended */
    HID_HOST_INTERFACE_EVENT_RESUMED,                 /*!< HID Device has been resumed */
#endif // HID_HOST_SUSPEND_RESUME_API_SUPPORTED
} hid_host_interface_event_t;

/**
 * @brief HID device descriptor information.
 */
typedef struct {
    uint16_t VID;                                      /*!< Vendor ID. */
    uint16_t PID;                                      /*!< Product ID. */
    wchar_t iManufacturer[HID_STR_DESC_MAX_LENGTH];    /*!< Manufacturer string. */
    wchar_t iProduct[HID_STR_DESC_MAX_LENGTH];         /*!< Product string. */
    wchar_t iSerialNumber[HID_STR_DESC_MAX_LENGTH];    /*!< Serial number string. */
} hid_host_dev_info_t;

/**
 * @brief USB HID host interface parameters.
 */
typedef struct {
    uint8_t addr;                       /*!< USB Address of connected HID device */
    uint8_t iface_num;                  /*!< HID Interface Number */
    uint8_t sub_class;                  /*!< HID Interface SubClass */
    uint8_t proto;                      /*!< HID Interface Protocol */
} hid_host_dev_params_t;

// ------------------------ USB HID Host callbacks -----------------------------

/**
 * @brief USB HID driver event callback.
 *
 * @param[in] hid_device_handle HID device handle for the reported interface.
 * @param[in] event HID driver event.
 * @param[in] arg User argument from the HID driver configuration structure.
 */
typedef void (*hid_host_driver_event_cb_t)(hid_host_device_handle_t hid_device_handle,
                                           const hid_host_driver_event_t event,
                                           void *arg);

/**
 * @brief USB HID Interface event callback.
 *
 * @param[in] hid_device_handle HID device handle for the reported interface.
 * @param[in] event HID interface event.
 * @param[in] arg User argument.
 */
typedef void (*hid_host_interface_event_cb_t)(hid_host_device_handle_t hid_device_handle,
                                              const hid_host_interface_event_t event,
                                              void *arg);

// ----------------------------- Public ---------------------------------------
/**
 * @brief HID driver configuration structure.
 */
typedef struct {
    bool create_background_task;            /*!< When set to true, background task handling USB events is created.
                                             Otherwise user has to periodically call hid_host_handle_events(). */
    size_t task_priority;                   /*!< Task priority of the created background task. */
    size_t stack_size;                      /*!< Stack size of the created background task. */
    BaseType_t core_id;                     /*!< Core affinity of the created background task, or tskNO_AFFINITY. */
    hid_host_driver_event_cb_t callback;    /*!< Callback invoked when HID driver event occurs. Must not be NULL. */
    void *callback_arg;                     /*!< User-provided argument passed to callback. */
} hid_host_driver_config_t;

/**
 * @brief HID interface configuration structure.
 */
typedef struct {
    hid_host_interface_event_cb_t callback;     /*!< Callback invoked when an HID interface event occurs. */
    void *callback_arg;                         /*!< User-provided argument passed to callback. */
} hid_host_device_config_t;

/**
 * @brief Install the USB Host HID class driver.
 *
 * @param[in] config HID driver configuration.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if config is NULL or invalid
 *      - ESP_ERR_INVALID_STATE if the driver is already installed
 *      - ESP_ERR_NO_MEM if memory allocation fails
 *      - Other error codes from the USB Host library
 */
esp_err_t hid_host_install(const hid_host_driver_config_t *config);

/**
 * @brief Uninstall the USB Host HID class driver.
 *
 * @note This function returns ESP_OK when the driver is already uninstalled.
 *
 * @return
 *      - ESP_OK on success, or if the driver is already uninstalled
 *      - ESP_ERR_INVALID_STATE if a device or interface is still open
 */
esp_err_t hid_host_uninstall(void);

/**
 * @brief Open an HID interface.
 *
 * @param[in] hid_dev_handle Handle of the HID interface to open.
 * @param[in] config HID interface configuration. Must not be NULL.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle is invalid
 *      - ESP_ERR_INVALID_STATE if the driver is not installed or the interface is not idle
 *      - ESP_ERR_TIMEOUT if the open or close lock could not be acquired in time
 *      - Other error codes from the USB Host library
 */
esp_err_t hid_host_device_open(hid_host_device_handle_t hid_dev_handle,
                               const hid_host_device_config_t *config);

/**
 * @brief Close an HID interface.
 *
 * @param[in] hid_dev_handle Handle of the HID interface to close.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle is invalid
 *      - ESP_ERR_INVALID_STATE if the driver is not installed
 *      - ESP_ERR_TIMEOUT if the open or close lock could not be acquired in time
 *      - Other error codes from the USB Host library
 */
esp_err_t hid_host_device_close(hid_host_device_handle_t hid_dev_handle);

/**
 * @brief Handle USB Host events for the HID driver.
 *
 * If hid_host_install() is called with `create_background_task = false`, the
 * application must call this function periodically to dispatch USB Host events.
 * Do not call this function when `create_background_task = true`.
 *
 * @param[in] timeout Timeout in ticks. Use pdMS_TO_TICKS() to convert from milliseconds.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the driver is not installed
 *      - ESP_FAIL if event handling finished because the driver is being uninstalled
 *      - Other error codes returned by usb_host_client_handle_events()
 */
esp_err_t hid_host_handle_events(uint32_t timeout);

/**
 * @brief Get HID interface parameters from a handle.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[out] dev_params Pointer to the structure to fill.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if dev_params is NULL
 *      - ESP_ERR_INVALID_STATE if hid_dev_handle does not reference a known interface
 */
esp_err_t hid_host_device_get_params(hid_host_device_handle_t hid_dev_handle,
                                     hid_host_dev_params_t *dev_params);

/**
 * @brief Copy the latest raw input report data.
 *
 * Call this function after receiving HID_HOST_INTERFACE_EVENT_INPUT_REPORT to
 * copy the raw report data associated with that event.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[out] data Buffer that receives the report data.
 * @param[in] data_length_max Size of data in bytes.
 * @param[out] data_length Number of bytes copied into data.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if data or data_length is NULL
 *      - ESP_ERR_INVALID_STATE if hid_dev_handle does not reference a known interface
 */
esp_err_t hid_host_device_get_raw_input_report_data(hid_host_device_handle_t hid_dev_handle,
                                                    uint8_t *data,
                                                    size_t data_length_max,
                                                    size_t *data_length);

#ifdef HID_HOST_REMOTE_WAKE_SUPPORTED
/**
 * @brief Enable or disable device remote wakeup.
 *
 * @note API availability depends on remote wakeup support in the ESP-IDF HAL
 *       and is guarded by HID_HOST_REMOTE_WAKE_SUPPORTED.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[in] enable Set to true to enable remote wakeup, or false to disable it.
 *
 * @return
 *      - ESP_OK on success, or if the requested state is already applied
 *      - ESP_ERR_INVALID_STATE if the HID driver is not installed
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle is invalid
 *      - ESP_ERR_NOT_FOUND if the interface is no longer tracked by the driver
 *      - ESP_ERR_NOT_SUPPORTED if the device does not support remote wakeup
 *      - ESP_ERR_TIMEOUT if the control transfer times out
 *      - ESP_ERR_INVALID_RESPONSE if the device returns an invalid response
 *      - Other error codes from lower layers
 */
esp_err_t hid_host_enable_remote_wakeup(hid_host_device_handle_t hid_dev_handle, bool enable);

#endif // HID_HOST_REMOTE_WAKE_SUPPORTED

// ------------------------ USB HID Host driver API ----------------------------

/**
 * @brief Start receiving input reports from an HID interface.
 *
 * The configured HID interface callback is invoked when an interface event
 * occurs.
 *
 * @note If the interface is suspended, this function resumes it and starts the
 *       transfer again.
 *
 * @param[in] hid_dev_handle HID device handle.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle is invalid
 *      - ESP_ERR_NOT_FOUND if the interface is no longer tracked by the driver
 *      - ESP_ERR_INVALID_STATE if the interface is not ready or suspended
 *      - Other error codes returned by usb_host_transfer_submit()
 */
esp_err_t hid_host_device_start(hid_host_device_handle_t hid_dev_handle);

/**
 * @brief Stop receiving input reports from an HID interface.
 *
 * @note If the interface is already suspended, this function prevents it from
 *       being restarted automatically when resumed.
 *
 * @param[in] hid_dev_handle HID device handle.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle is invalid
 *      - ESP_ERR_NOT_FOUND if the interface is no longer tracked by the driver
 *      - Other error codes from the HID host driver
 */
esp_err_t hid_host_device_stop(hid_host_device_handle_t hid_dev_handle);

/**
 * @brief Get the HID report descriptor for an interface.
 *
 * The returned pointer is owned by the HID host driver and remains valid until
 * the interface is closed.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[out] report_desc_len Length of the report descriptor in bytes. Must not be NULL.
 *
 * @return Pointer to the report descriptor on success, or NULL if the handle is
 *         invalid or the descriptor cannot be retrieved
 */
uint8_t *hid_host_get_report_descriptor(hid_host_device_handle_t hid_dev_handle,
                                        size_t *report_desc_len);


/**
 * @brief Get HID device descriptor information.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[out] hid_dev_info Pointer to the structure to fill.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle is invalid or hid_dev_info is NULL
 *      - Other error codes from the USB Host library
 */
esp_err_t hid_host_get_device_info(hid_host_device_handle_t hid_dev_handle,
                                   hid_host_dev_info_t *hid_dev_info);

/**
 * @brief Send a HID class-specific Get_Report request.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[in] report_type Report type from hid_report_type_t.
 * @param[in] report_id Report ID.
 * @param[out] report Buffer that receives the report data.
 * @param[in,out] report_length Input: size of report in bytes. Output: number of bytes received.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle or report is invalid
 *      - Other error codes from lower layers
 */
esp_err_t hid_class_request_get_report(hid_host_device_handle_t hid_dev_handle,
                                       uint8_t report_type,
                                       uint8_t report_id,
                                       uint8_t *report,
                                       size_t *report_length);

/**
 * @brief Send a HID class-specific Get_Idle request.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[in] report_id Report ID.
 * @param[out] idle_rate Idle rate reported by the device.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle or idle_rate is invalid
 *      - Other error codes from lower layers
 */
esp_err_t hid_class_request_get_idle(hid_host_device_handle_t hid_dev_handle,
                                     uint8_t report_id,
                                     uint8_t *idle_rate);

/**
 * @brief Send a HID class-specific Get_Protocol request.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[out] protocol Pointer to the current HID report protocol.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle or protocol is invalid
 *      - Other error codes from lower layers
 */
esp_err_t hid_class_request_get_protocol(hid_host_device_handle_t hid_dev_handle,
                                         hid_report_protocol_t *protocol);

/**
 * @brief Send a HID class-specific Set_Report request.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[in] report_type Report type from hid_report_type_t.
 * @param[in] report_id Report ID.
 * @param[in] report Pointer to the report data buffer.
 * @param[in] report_length Report data length in bytes.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle or report is invalid
 *      - Other error codes from lower layers
 */
esp_err_t hid_class_request_set_report(hid_host_device_handle_t hid_dev_handle,
                                       uint8_t report_type,
                                       uint8_t report_id,
                                       uint8_t *report,
                                       size_t report_length);

/**
 * @brief Send a HID class-specific Set_Idle request.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[in] duration Idle duration. Use 0 for an indefinite duration.
 * @param[in] report_id Report ID. Use 0 to apply the idle rate to all input reports.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle is invalid
 *      - Other error codes from lower layers
 */
esp_err_t hid_class_request_set_idle(hid_host_device_handle_t hid_dev_handle,
                                     uint8_t duration,
                                     uint8_t report_id);

/**
 * @brief Send a HID class-specific Set_Protocol request.
 *
 * @param[in] hid_dev_handle HID device handle.
 * @param[in] protocol HID report protocol to select.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if hid_dev_handle is invalid
 *      - Other error codes from lower layers
 */
esp_err_t hid_class_request_set_protocol(hid_host_device_handle_t hid_dev_handle,
                                         hid_report_protocol_t protocol);

#ifdef __cplusplus
}
#endif //__cplusplus
