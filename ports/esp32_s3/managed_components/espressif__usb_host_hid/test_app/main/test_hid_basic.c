/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_idf_version.h"
#include "esp_private/usb_phy.h"
#include "usb/usb_host.h"

#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"

#include "test_hid_basic.h"

#define TEST_EVENT_WAIT_MS          500     // Time to expect driver or device event
#define TEST_DEV_CONN_WAIT_MS       2000    // Time to wait for new device connection

// Global variable to verify user arg passing through callbacks
static uint32_t user_arg_value = 0x8A53E0A4; // Just a constant random number

// Queue and task for possibility to interact with USB device
// IMPORTANT: Interaction is not possible within device/interface callback
static bool time_to_shutdown = false;
static bool time_to_stop_polling = false;
QueueHandle_t hid_host_test_event_queue = NULL;
TaskHandle_t hid_test_task_handle;

// Multiple tasks testing
static SemaphoreHandle_t s_global_hdl_sem;
static hid_host_device_handle_t s_global_hdl;

static const char *test_hid_sub_class_names[] = {
    "NO_SUBCLASS",
    "BOOT_INTERFACE",
};

static const char *test_hid_proto_names[] = {
    "NONE",
    "KEYBOARD",
    "MOUSE"
};

/**
 * @brief Event group type
 */
typedef enum {
    HID_DRIVER_EVENT = 0,       /**< HID Driver event type */
    HID_INTERFACE_EVENT,        /**< HID Interface event type */
} event_group_t;

/**
 * @brief Test event queue
 */
typedef struct {
    event_group_t event_group;                              /**< Event group (Driver or Interface) */
    union {
        struct {
            hid_host_driver_event_t event;                  /**< HID Driver event ID */
            hid_host_device_handle_t hid_device_handle;     /**< HID Device handle */
            void *arg;
        } driver_evt;                                       /**< Driver event */
        struct {
            hid_host_interface_event_t event;               /**< HID Interface event ID */
            hid_host_device_handle_t hid_device_handle;     /**< HID Device handle */
            void *arg;
        } interface_evt;                                    /**< Interface event */
    };
} hid_host_event_queue_t;

#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED

static const hid_host_event_queue_t dev_gone_event = {
    .event_group = HID_INTERFACE_EVENT,
    .driver_evt.event = HID_HOST_INTERFACE_EVENT_DISCONNECTED,
};

static const hid_host_event_queue_t new_dev_event = {
    .event_group = HID_DRIVER_EVENT,
    .driver_evt.event = HID_HOST_DRIVER_EVENT_CONNECTED,
};

static const hid_host_event_queue_t suspend_event = {
    .event_group = HID_INTERFACE_EVENT,
    .driver_evt.event = HID_HOST_INTERFACE_EVENT_SUSPENDED,
};

static const hid_host_event_queue_t resume_event = {
    .event_group = HID_INTERFACE_EVENT,
    .driver_evt.event = HID_HOST_INTERFACE_EVENT_RESUMED,
};
#endif // HID_HOST_SUSPEND_RESUME_API_SUPPORTED

// usb_host_lib_set_root_port_power is used to force toggle connection, primary developed for esp32p4
// esp32p4 is supported from IDF 5.3
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 0)
static usb_phy_handle_t phy_hdl = NULL;

// Force connection/disconnection using PHY
static void force_conn_state(bool connected, TickType_t delay_ticks)
{
    TEST_ASSERT_NOT_EQUAL(NULL, phy_hdl);
    if (delay_ticks > 0) {
        // Delay of 0 ticks causes a yield. So skip if delay_ticks is 0.
        vTaskDelay(delay_ticks);
    }
    ESP_ERROR_CHECK(usb_phy_action(phy_hdl, (connected) ? USB_PHY_ACTION_HOST_ALLOW_CONN : USB_PHY_ACTION_HOST_FORCE_DISCONN));
}

// Initialize the internal USB PHY to connect to the USB OTG peripheral. We manually install the USB PHY for testing
static bool install_phy(void)
{
    usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_HOST,
        .otg_speed = USB_PHY_SPEED_UNDEFINED,   // In Host mode, the speed is determined by the connected device
    };
    TEST_ASSERT_EQUAL(ESP_OK, usb_new_phy(&phy_config, &phy_hdl));
    // Return true, to skip_phy_setup during the usb_host_install()
    return true;
}

static void delete_phy(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, usb_del_phy(phy_hdl)); // Tear down USB PHY
    phy_hdl = NULL;
}
#else

// Force connection/disconnection using root port power
static void force_conn_state(bool connected, TickType_t delay_ticks)
{
    if (delay_ticks > 0) {
        // Delay of 0 ticks causes a yield. So skip if delay_ticks is 0.
        vTaskDelay(delay_ticks);
    }
    ESP_ERROR_CHECK(usb_host_lib_set_root_port_power(connected));
}

static bool install_phy(void)
{
    // Return false, NOT to skip_phy_setup during the usb_host_install()
    return false;
}

static void delete_phy(void) {}
#endif

void hid_host_test_interface_callback(hid_host_device_handle_t hid_device_handle,
                                      const hid_host_interface_event_t event,
                                      void *arg)
{
    uint8_t data[64] = { 0 };
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(hid_device_handle, &dev_params));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&user_arg_value, arg, "User argument has lost");

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        printf("USB port %d, Interface num %d: ",
               dev_params.addr,
               dev_params.iface_num);

        hid_host_device_get_raw_input_report_data(hid_device_handle,
                                                  data,
                                                  64,
                                                  &data_length);

        for (int i = 0; i < data_length; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        printf("USB port %d, iface num %d removed\n",
               dev_params.addr,
               dev_params.iface_num);
        TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_close(hid_device_handle) );
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        printf("USB Host transfer error\n");
        break;
    default:
        TEST_FAIL_MESSAGE("HID Interface unhandled event");
        break;
    }
}

void hid_host_test_callback(hid_host_device_handle_t hid_device_handle,
                            const hid_host_driver_event_t event,
                            void *arg)
{
    hid_host_dev_params_t dev_params;
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(hid_device_handle, &dev_params));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&user_arg_value, arg, "User argument has lost");

    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED:
        printf("USB port %d, interface %d, '%s', '%s'\n",
               dev_params.addr,
               dev_params.iface_num,
               test_hid_sub_class_names[dev_params.sub_class],
               test_hid_proto_names[dev_params.proto]);

        const hid_host_device_config_t dev_config = {
            .callback = hid_host_test_interface_callback,
            .callback_arg = (void *) &user_arg_value
        };

        TEST_ASSERT_EQUAL(ESP_OK,  hid_host_device_open(hid_device_handle, &dev_config) );
        TEST_ASSERT_EQUAL(ESP_OK,  hid_host_device_start(hid_device_handle) );

        break;
    default:
        TEST_FAIL_MESSAGE("HID Driver unhandled event");
        break;
    }
}

void hid_host_test_concurrent(hid_host_device_handle_t hid_device_handle,
                              const hid_host_driver_event_t event,
                              void *arg)
{
    hid_host_dev_params_t dev_params;

    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(hid_device_handle, &dev_params));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&user_arg_value, arg, "User argument has lost");

    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED:
        printf("USB port %d, interface %d, '%s', '%s'\n",
               dev_params.addr,
               dev_params.iface_num,
               test_hid_sub_class_names[dev_params.sub_class],
               test_hid_proto_names[dev_params.proto]);

        const hid_host_device_config_t dev_config = {
            .callback = hid_host_test_interface_callback,
            .callback_arg = &user_arg_value
        };

        TEST_ASSERT_EQUAL(ESP_OK,  hid_host_device_open(hid_device_handle, &dev_config) );
        TEST_ASSERT_EQUAL(ESP_OK,  hid_host_device_start(hid_device_handle) );

        s_global_hdl = hid_device_handle;
        xSemaphoreGive(s_global_hdl_sem);
        break;
    default:
        TEST_FAIL_MESSAGE("HID Driver unhandled event");
        break;
    }
}

#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED

static char err_msg_buf[128];

/**
 * @brief Expect NO interface or device events
 *
 * @param[in] ticks            Ticks to wait for the event
 */
static void hid_host_test_expect_no_event_impl(TickType_t ticks, const char *file, int line)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(hid_host_test_event_queue, "App queue has not been initialized");

    // Expect NO event
    hid_host_event_queue_t event_queue;
    if (pdFALSE == xQueueReceive(hid_host_test_event_queue, &event_queue, ticks)) {
        // Expecting NO event, none delivered, return
        return;
    } else {
        snprintf(err_msg_buf, sizeof(err_msg_buf), "Expecting NO event, but an event delivered at %s:%d\n", file, line);
        TEST_FAIL_MESSAGE(err_msg_buf);
    }
}

/**
 * @brief Expect interface or device events
 *
 * @param[in] expected_event   Pointer to an expected event, NULL to expect NO event
 * @param[in] ticks            Ticks to wait for the event
 * @param[in] event_count      Number of the same events to be expected
 *
 * @return void pointer to a device from which an event was generated
 */
static void *hid_host_test_expect_event_impl(const hid_host_event_queue_t *expected_event, TickType_t ticks, int event_count, const char *file, int line)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(hid_host_test_event_queue, "App queue has not been initialized");

    // Expect event_count number of events
    hid_host_device_handle_t dev_hdl = NULL;
    for (int i = 0; i < event_count; i++) {
        hid_host_event_queue_t event_queue;
        if (pdTRUE == xQueueReceive(hid_host_test_event_queue, &event_queue, ticks)) {
            TEST_ASSERT_EQUAL_MESSAGE(expected_event->event_group, event_queue.event_group, "Unexpected event group");
            if (event_queue.event_group == HID_DRIVER_EVENT) {
                if (event_queue.driver_evt.event != expected_event->driver_evt.event) {
                    snprintf(err_msg_buf, sizeof(err_msg_buf), "Unexpected driver event at %s:%d\n %d expected, %d delivered\n",
                             file, line, expected_event->driver_evt.event, event_queue.driver_evt.event);
                    TEST_FAIL_MESSAGE(err_msg_buf);
                }
                dev_hdl = event_queue.driver_evt.hid_device_handle;
            } else {
                if (event_queue.interface_evt.event != expected_event->interface_evt.event) {
                    snprintf(err_msg_buf, sizeof(err_msg_buf), "Unexpected interface event at %s:%d\n %d expected, %d delivered\n",
                             file, line, expected_event->interface_evt.event, event_queue.interface_evt.event);
                    TEST_FAIL_MESSAGE(err_msg_buf);
                }
                dev_hdl = event_queue.interface_evt.hid_device_handle;
            }
        } else {
            snprintf(err_msg_buf, sizeof(err_msg_buf), "Event not generated on time at %s:%d\n", file, line);
            TEST_FAIL_MESSAGE(err_msg_buf);
        }
    }
    return (void *)dev_hdl;
}

#define hid_host_test_expect_event(expected_event, ticks, event_count) \
    hid_host_test_expect_event_impl((expected_event), (ticks), (event_count), __FILE__, __LINE__)

#define hid_host_test_expect_no_event(ticks) \
    hid_host_test_expect_no_event_impl((ticks), __FILE__, __LINE__)

/**
 * @brief HID Host interface callback with power management (suspend/resume) events
 * @note  The callback is pushing events to an event queue
 *
 * @param[in] hid_device_handle Device handle
 * @param[in] event             Interface event
 * @param[in] arg               Callback argument
 */
static void hid_host_pm_interface_callback(hid_host_device_handle_t hid_device_handle,
                                           const hid_host_interface_event_t event,
                                           void *arg)
{
    uint8_t data[64] = { 0 };
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(hid_device_handle, &dev_params));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&user_arg_value, arg, "User argument has lost");

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        printf("USB port %d, Interface num %d: ",
               dev_params.addr,
               dev_params.iface_num);

        hid_host_device_get_raw_input_report_data(hid_device_handle,
                                                  data,
                                                  64,
                                                  &data_length);

        for (int i = 0; i < data_length; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        printf("USB port %d, interface %d, '%s', '%s' DISCONNECTED\n",
               dev_params.addr,
               dev_params.iface_num,
               test_hid_sub_class_names[dev_params.sub_class],
               test_hid_proto_names[dev_params.proto]);
        TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_close(hid_device_handle) );
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        printf("USB Host transfer error\n");
        break;
    case HID_HOST_INTERFACE_EVENT_SUSPENDED:
        printf("USB port %d, interface %d, '%s', '%s' SUSPENDED\n",
               dev_params.addr,
               dev_params.iface_num,
               test_hid_sub_class_names[dev_params.sub_class],
               test_hid_proto_names[dev_params.proto]);
        break;
    case HID_HOST_INTERFACE_EVENT_RESUMED:
        printf("USB port %d, interface %d, '%s', '%s' RESUMED\n",
               dev_params.addr,
               dev_params.iface_num,
               test_hid_sub_class_names[dev_params.sub_class],
               test_hid_proto_names[dev_params.proto]);
        break;
    default:
        TEST_FAIL_MESSAGE("HID Interface unhandled event");
        break;
    }

    const hid_host_event_queue_t event_queue = {
        .event_group = HID_INTERFACE_EVENT,
        .interface_evt.hid_device_handle = hid_device_handle,
        .interface_evt.event = event,
        .interface_evt.arg = arg,
    };

    if (hid_host_test_event_queue) {
        xQueueSend(hid_host_test_event_queue, &event_queue, 0);
    }
}

/**
 * @brief HID Host driver callback
 * @note  The callback is pushing events to an event queue
 *
 * @param[in] hid_device_handle Device handle
 * @param[in] event             Driver event
 * @param[in] arg               Callback argument
 */
static void hid_host_test_pm_driver_callback(hid_host_device_handle_t hid_device_handle,
                                             const hid_host_driver_event_t event,
                                             void *arg)
{
    hid_host_dev_params_t dev_params;
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(hid_device_handle, &dev_params));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&user_arg_value, arg, "User argument has lost");

    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED:
        printf("USB port %d, interface %d, '%s', '%s' CONNECTED\n",
               dev_params.addr,
               dev_params.iface_num,
               test_hid_sub_class_names[dev_params.sub_class],
               test_hid_proto_names[dev_params.proto]);
        break;
    default:
        TEST_FAIL_MESSAGE("HID Driver unhandled event");
        return;
    }

    const hid_host_event_queue_t event_queue = {
        .event_group = HID_DRIVER_EVENT,
        .driver_evt.hid_device_handle = hid_device_handle,
        .driver_evt.event = event,
        .driver_evt.arg = arg,
    };

    if (hid_host_test_event_queue) {
        xQueueSend(hid_host_test_event_queue, &event_queue, 0);
    }
}

/**
 * @brief Open and start device with 2 interfaces
 *
 * Function checks new device events
 */
static void open_start_device(void)
{
    // Wait, until the device is connected, expect 2 CONNECTED events
    hid_host_device_handle_t dev1_hdl = (hid_host_device_handle_t)hid_host_test_expect_event(&new_dev_event, pdMS_TO_TICKS(TEST_DEV_CONN_WAIT_MS), 1);
    hid_host_device_handle_t dev2_hdl = (hid_host_device_handle_t)hid_host_test_expect_event(&new_dev_event, pdMS_TO_TICKS(TEST_DEV_CONN_WAIT_MS), 1);

    TEST_ASSERT_NOT_NULL(dev1_hdl);
    TEST_ASSERT_NOT_NULL(dev2_hdl);

    const hid_host_device_config_t dev_config = {
        .callback = hid_host_pm_interface_callback,
        .callback_arg = &user_arg_value,
    };

    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_open(dev1_hdl, &dev_config));
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_start(dev1_hdl));

    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_open(dev2_hdl, &dev_config));
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_start(dev2_hdl));

    s_global_hdl = dev2_hdl;
}
#endif // HID_HOST_SUSPEND_RESUME_API_SUPPORTED

void hid_host_test_device_callback_to_queue(hid_host_device_handle_t hid_device_handle,
                                            const hid_host_driver_event_t event,
                                            void *arg)
{

    const hid_host_event_queue_t evt_queue = {
        .event_group = HID_DRIVER_EVENT,
        .driver_evt.hid_device_handle = hid_device_handle,
        .driver_evt.event = event,
        .driver_evt.arg = arg,
    };

    if (hid_host_test_event_queue) {
        xQueueSend(hid_host_test_event_queue, &evt_queue, 0);
    }
}

void hid_host_test_requests_callback(hid_host_device_handle_t hid_device_handle,
                                     const hid_host_driver_event_t event,
                                     void *arg)
{
    hid_host_dev_params_t dev_params;
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(hid_device_handle, &dev_params));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&user_arg_value, arg, "User argument has lost");

    uint8_t *test_buffer = NULL; // for report descriptor
    unsigned int test_length = 0;
    uint8_t tmp[10] = { 0 };     // for input report
    size_t rep_len = 0;

    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED:
        printf("USB port %d, interface %d, '%s', '%s'\n",
               dev_params.addr,
               dev_params.iface_num,
               test_hid_sub_class_names[dev_params.sub_class],
               test_hid_proto_names[dev_params.proto]);

        const hid_host_device_config_t dev_config = {
            .callback = hid_host_test_interface_callback,
            .callback_arg = &user_arg_value
        };

        TEST_ASSERT_EQUAL(ESP_OK,  hid_host_device_open(hid_device_handle, &dev_config) );

        // Class device requests
        // hid_host_get_report_descriptor
        test_buffer = hid_host_get_report_descriptor(hid_device_handle, &test_length);

        TEST_ASSERT_NOT_NULL(test_buffer);
        printf("HID Report descriptor length: %d\n", test_length);

        // // HID Device info
        hid_host_dev_info_t hid_dev_info;
        TEST_ASSERT_EQUAL(ESP_OK, hid_host_get_device_info(hid_device_handle,
                                                           &hid_dev_info) );

        printf("\t VID: 0x%04X\n", hid_dev_info.VID);
        printf("\t PID: 0x%04X\n", hid_dev_info.PID);
        wprintf(L"\t iProduct: %S \n", hid_dev_info.iProduct);
        wprintf(L"\t iManufacturer: %S \n", hid_dev_info.iManufacturer);
        wprintf(L"\t iSerialNumber: %S \n", hid_dev_info.iSerialNumber);

        if (dev_params.proto == HID_PROTOCOL_NONE) {
            // If Protocol NONE, based on hid1_11.pdf, p.78, all other devices should support
            rep_len = sizeof(tmp);
            // For testing with ESP32 we used ReportID = 0x01 (Keyboard ReportID)
            if (ESP_OK == hid_class_request_get_report(hid_device_handle,
                                                       HID_REPORT_TYPE_INPUT, 0x01, tmp, &rep_len)) {
                printf("HID Get Report, type %d, id %d, length: %d:\n",
                       HID_REPORT_TYPE_INPUT, 0, rep_len);
                for (int i = 0; i < rep_len; i++) {
                    printf("%02X ", tmp[i]);
                }
                printf("\n");
            }

            rep_len = sizeof(tmp);
            // For testing with ESP32 we used ReportID = 0x02 (Mouse ReportID)
            if (ESP_OK == hid_class_request_get_report(hid_device_handle,
                                                       HID_REPORT_TYPE_INPUT, 0x02, tmp, &rep_len)) {
                printf("HID Get Report, type %d, id %d, length: %d:\n",
                       HID_REPORT_TYPE_INPUT, 0, rep_len);
                for (int i = 0; i < rep_len; i++) {
                    printf("%02X ", tmp[i]);
                }
                printf("\n");
            }
        } else {
            // hid_class_request_get_protocol
            hid_report_protocol_t proto;
            if (ESP_OK == hid_class_request_get_protocol(hid_device_handle, &proto)) {
                printf("HID protocol: %d\n", proto);
            }

            if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
                uint8_t idle_rate;
                // hid_class_request_get_idle
                if (ESP_OK == hid_class_request_get_idle(hid_device_handle,
                                                         0, &idle_rate)) {
                    printf("HID idle rate: %d\n", idle_rate);
                }
                // hid_class_request_set_idle
                if (ESP_OK == hid_class_request_set_idle(hid_device_handle,
                                                         0, 0)) {
                    printf("HID idle rate set to 0\n");
                }

                // hid_class_request_get_report
                rep_len = sizeof(tmp);
                if (ESP_OK == hid_class_request_get_report(hid_device_handle,
                                                           HID_REPORT_TYPE_INPUT, 0x01, tmp, &rep_len)) {
                    printf("HID get report type %d, id %d, length: %d\n",
                           HID_REPORT_TYPE_INPUT, 0x00, rep_len);
                }

                // hid_class_request_set_report
                uint8_t rep[1] = { 0x00 };
                if (ESP_OK == hid_class_request_set_report(hid_device_handle,
                                                           HID_REPORT_TYPE_OUTPUT, 0x01, rep, 1)) {
                    printf("HID set report type %d, id %d\n", HID_REPORT_TYPE_OUTPUT, 0x00);
                }
            }

            if (dev_params.proto == HID_PROTOCOL_MOUSE) {
                // hid_class_request_get_report
                rep_len = sizeof(tmp);
                if (ESP_OK == hid_class_request_get_report(hid_device_handle,
                                                           HID_REPORT_TYPE_INPUT, 0x02, tmp, &rep_len)) {
                    printf("HID get report type %d, id %d, length: %d\n",
                           HID_REPORT_TYPE_INPUT, 0x00, rep_len);
                }
            }

            // hid_class_request_set_protocol
            if (ESP_OK == hid_class_request_set_protocol(hid_device_handle,
                                                         HID_REPORT_PROTOCOL_BOOT)) {
                printf("HID protocol change to BOOT: %d\n", proto);
            }
        }

        TEST_ASSERT_EQUAL(ESP_OK,  hid_host_device_start(hid_device_handle) );
        break;
    default:
        TEST_FAIL_MESSAGE("HID Driver unhandled event");
        break;
    }
}

void hid_host_test_task(void *pvParameters)
{
    hid_host_event_queue_t evt_queue;
    // Create queue
    hid_host_test_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));

    // Wait queue
    while (!time_to_shutdown) {
        if (xQueueReceive(hid_host_test_event_queue, &evt_queue, pdMS_TO_TICKS(50))) {
            hid_host_test_requests_callback(evt_queue.driver_evt.hid_device_handle,
                                            evt_queue.driver_evt.event,
                                            evt_queue.driver_evt.arg);
        }
    }

    xQueueReset(hid_host_test_event_queue);
    vQueueDelete(hid_host_test_event_queue);
    hid_host_test_event_queue = NULL;
    vTaskDelete(NULL);
}

void hid_host_test_polling_task(void *pvParameters)
{
    // Wait queue
    while (!time_to_stop_polling) {
        hid_host_handle_events(portMAX_DELAY);
    }

    vTaskDelete(NULL);
}

// Stress the HID device by getting input report according to its protocol
// Note: the s_global_hdl must be valid before calling this function
static void test_hid_host_device_stress(hid_host_dev_params_t *dev_params)
{
    uint8_t tmp[10] = { 0 };     // for input report
    size_t rep_len = 0;
    hid_report_protocol_t proto;

    if (dev_params->proto == HID_PROTOCOL_NONE) {
        rep_len = sizeof(tmp);
        // For testing with ESP32 we used ReportID = 0x01 (Keyboard ReportID)
        TEST_ASSERT_EQUAL(ESP_OK, hid_class_request_get_report(s_global_hdl, HID_REPORT_TYPE_INPUT, 0x01, tmp, &rep_len));
    } else {
        // Get Protocol
        TEST_ASSERT_EQUAL(ESP_OK, hid_class_request_get_protocol(s_global_hdl, &proto));
        // Get Report for Keyboard protocol, ReportID = 0x00 (Boot Keyboard ReportID)
        if (dev_params->proto == HID_PROTOCOL_KEYBOARD) {
            rep_len = sizeof(tmp);
            TEST_ASSERT_EQUAL(ESP_OK, hid_class_request_get_report(s_global_hdl, HID_REPORT_TYPE_INPUT, 0x01, tmp, &rep_len));
        }
        if (dev_params->proto == HID_PROTOCOL_MOUSE) {
            rep_len = sizeof(tmp);
            TEST_ASSERT_EQUAL(ESP_OK, hid_class_request_get_report(s_global_hdl, HID_REPORT_TYPE_INPUT, 0x02, tmp, &rep_len));
        }
    }
}

#define MULTIPLE_TASKS_TASKS_NUM 10

// Note: the s_global_hdl must be valid before this task is started
void concurrent_task(void *arg)
{
    SemaphoreHandle_t worker_done_sem = *(SemaphoreHandle_t *) arg;
    uint8_t *test_buffer = NULL;
    unsigned int test_length = 0;
    hid_host_dev_params_t dev_params;

    // Here we don't need to serialize the access to the device, because
    // we expecting serialization in the HID Host driver itself.
    // We just using the valid s_global_hdl to access the device concurrently from multiple tasks.

    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(s_global_hdl, &dev_params));

    // Get Report descriptor
    test_buffer = hid_host_get_report_descriptor(s_global_hdl, &test_length);
    TEST_ASSERT_NOT_NULL(test_buffer);
    // Stress the device with getting input report
    test_hid_host_device_stress(&dev_params);
    // Notify the main test that this task is done
    xSemaphoreGive(worker_done_sem);
    // Delete this task
    vTaskDelete(NULL);
}

// Note: the s_global_hdl must be valid before this task is started
void access_task(void *arg)
{
    uint8_t *test_buffer = NULL;
    unsigned int test_length = 0;
    hid_host_dev_params_t dev_params;

    // Get device params
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(s_global_hdl, &dev_params));

    // Get Report descriptor
    test_buffer = hid_host_get_report_descriptor(s_global_hdl, &test_length);
    TEST_ASSERT_NOT_NULL(test_buffer);

    // Stress the device until time_to_shutdown is set
    while (!time_to_shutdown) {
        test_hid_host_device_stress(&dev_params);
    }

    xSemaphoreGive(s_global_hdl_sem);
    vTaskDelete(NULL);
}

/**
 * @brief Creates MULTIPLE_TASKS_TASKS_NUM to get report descriptor and get protocol from HID device.
 */
void test_start_multiple_tasks_access(void)
{
    // Create a counting semaphore to track worker task completions
    SemaphoreHandle_t worker_done_sem = NULL;
    worker_done_sem = xSemaphoreCreateCounting(MULTIPLE_TASKS_TASKS_NUM, 0);
    TEST_ASSERT_NOT_NULL_MESSAGE(worker_done_sem, "Failed to create counting semaphore");

    // Create tasks that will try to access HID dev with global hdl
    for (int i = 0; i < MULTIPLE_TASKS_TASKS_NUM; i++) {
        TEST_ASSERT_EQUAL(pdTRUE, xTaskCreate(concurrent_task,
                                              "HID multiple task stress",
                                              4096,
                                              (void *) &worker_done_sem,
                                              i + 3,
                                              NULL));
    }
    // Wait all tasks to complete
    for (int i = 0; i < MULTIPLE_TASKS_TASKS_NUM; i++) {
        TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, xSemaphoreTake(worker_done_sem, pdMS_TO_TICKS(5000)), "Not all tasks completed in time");
    }
    // Clean up
    vSemaphoreDelete(worker_done_sem);
}

/**
 * @brief Start USB Host and handle common USB host library events while devices/clients are present
 *
 * @param[in] arg  Main task handle
 */
static void usb_lib_task(void *arg)
{
    const bool skip_phy_setup = install_phy();
    const usb_host_config_t host_config = {
        .skip_phy_setup = skip_phy_setup,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_install(&host_config) );
    const TaskHandle_t main_task_hdl = (TaskHandle_t)arg;
    xTaskNotifyGive(main_task_hdl);

    bool all_clients_gone = false;
    bool all_dev_free = false;
    while (!all_clients_gone || !all_dev_free) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

        // Release devices once all clients has deregistered
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
            printf("USB Event flags: NO_CLIENTS\n");
            all_clients_gone = true;
        }
        // All devices were removed
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            printf("USB Event flags: ALL_FREE\n");
            all_dev_free = true;
            time_to_stop_polling = true;
            // Notify that device was being disconnected
            xTaskNotifyGive(main_task_hdl);
        }
#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED
        // Automatic ssuspend timer
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_AUTO_SUSPEND) {
            printf("USB Event flags: AUTO_SUSPEND\n");
            TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
        }
#endif // HID_HOST_SUSPEND_RESUME_API_SUPPORTED
    }

    // Change global flag for all tasks still running
    time_to_shutdown = true;

    // Clean up USB Host
    vTaskDelay(10); // Short delay to allow clients clean-up
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_uninstall());
    delete_phy();
    vTaskDelete(NULL);
}

// ----------------------- Public -------------------------

/**
 * @brief Setups HID testing
 *
 * - Create USB lib task
 * - Install HID Host driver
 */
void test_hid_setup(hid_host_driver_event_cb_t device_callback,
                    hid_test_event_handle_t hid_test_event_handle)
{
    TEST_ASSERT_EQUAL(pdTRUE, xTaskCreatePinnedToCore(usb_lib_task,
                                                      "usb_events",
                                                      4096,
                                                      xTaskGetCurrentTaskHandle(),
                                                      2, NULL, 0));
    // Wait for notification from usb_lib_task
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, ulTaskNotifyTake(true, pdMS_TO_TICKS(2000)), "host_lib_task did not start on time");

    // HID host driver config
    const hid_host_driver_config_t hid_host_driver_config = {
        .create_background_task = (hid_test_event_handle == HID_TEST_EVENT_HANDLE_IN_DRIVER)
        ? true
        : false,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = device_callback,
        .callback_arg = (void *) &user_arg_value
    };

    TEST_ASSERT_EQUAL(ESP_OK, hid_host_install(&hid_host_driver_config) );
}

/**
 * @brief Teardowns HID testing
 * - Disconnect connected USB device manually by setting root port power (PHY triggering)
 * - Wait for USB lib task was closed
 * - Uninstall HID Host driver
 * - Clear the notification value to 0
 * - Short delay to allow task to be cleaned up
 */
void test_hid_teardown(void)
{
    force_conn_state(false, pdMS_TO_TICKS(1000));
    vTaskDelay(50);
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_uninstall());
    ulTaskNotifyValueClear(NULL, UINT32_MAX);
    vTaskDelay(20);
}

// ------------------------- HID Test ------------------------------------------
static void test_setup_hid_task(void)
{
    // Task is working until the devices are gone
    time_to_shutdown = false;
    // Create process
    TEST_ASSERT_EQUAL(pdTRUE, xTaskCreate(&hid_host_test_task,
                                          "hid_task",
                                          4 * 1024,
                                          NULL,
                                          3,
                                          &hid_test_task_handle));
}

static void test_setup_hid_polling_task(void)
{
    time_to_stop_polling = false;

    TEST_ASSERT_EQUAL(pdTRUE, xTaskCreate(&hid_host_test_polling_task,
                                          "hid_task_polling",
                                          4 * 1024,
                                          NULL, 2, NULL));
}

TEST_CASE("memory_leakage", "[hid_host]")
{
    // Install USB and HID driver with the regular 'hid_host_test_callback'
    test_hid_setup(hid_host_test_callback, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    // Tear down test
    test_hid_teardown();
    // Verify the memory leakage during test environment tearDown()
}

TEST_CASE("multiple_task_access", "[hid_host]")
{
    // Create semaphore for s_global_hdl, because it will be used in multiple tasks access
    s_global_hdl_sem = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_EQUAL_MESSAGE(NULL, s_global_hdl_sem, "Semaphore creation failed");
    // Install USB and HID driver with 'hid_host_test_concurrent'
    test_hid_setup(hid_host_test_concurrent, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    // Wait the s_global_hdl to be valid 5sec timeout
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, xSemaphoreTake(s_global_hdl_sem, pdMS_TO_TICKS(5000)), "HID device handle not ready in time");
    // Start multiple task access to USB device with control requests
    test_start_multiple_tasks_access();
    // Tear down test
    test_hid_teardown();
    // Delete the semaphore
    vSemaphoreDelete(s_global_hdl_sem);
    s_global_hdl_sem = NULL;
    // Verify the memory leakage during test environment tearDown()
}

TEST_CASE("class_specific_requests", "[hid_host]")
{
    // Create external HID events task
    test_setup_hid_task();
    // Install USB and HID driver with 'hid_host_test_device_callback_to_queue'
    test_hid_setup(hid_host_test_device_callback_to_queue, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    // All specific control requests will be verified during device connection callback 'hid_host_test_requests_callback'
    // Wait for test completed for 250 ms
    vTaskDelay(250);
    // Tear down test
    test_hid_teardown();
    // Verify the memory leakage during test environment tearDown()
}

TEST_CASE("class_specific_requests_with_external_polling", "[hid_host]")
{
    // Create external HID events task
    test_setup_hid_task();
    // Install USB and HID driver with 'hid_host_test_device_callback_to_queue'
    test_hid_setup(hid_host_test_device_callback_to_queue, HID_TEST_EVENT_HANDLE_EXTERNAL);
    // Create HID Driver events polling task
    test_setup_hid_polling_task();
    // All specific control requests will be verified during device connection callback 'hid_host_test_requests_callback'
    // Wait for test completed for 250 ms
    vTaskDelay(250);
    // Tear down test
    test_hid_teardown();
    // Verify the memory leakage during test environment tearDown()
}

TEST_CASE("class_specific_requests_with_external_polling_without_polling", "[hid_host]")
{
    // Create external HID events task
    test_setup_hid_task();
    // Install USB and HID driver with 'hid_host_test_device_callback_to_queue'
    test_hid_setup(hid_host_test_device_callback_to_queue, HID_TEST_EVENT_HANDLE_EXTERNAL);
    // Do not create HID Driver events polling task to eliminate events polling
    // ...
    // Wait for 250 ms
    vTaskDelay(250);
    // Tear down test
    test_hid_teardown();
    // Verify the memory leakage during test environment tearDown()
}

TEST_CASE("sudden_disconnect", "[hid_host]")
{
    // Create semaphore for s_global_hdl, because it will be used in access_task
    s_global_hdl_sem = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_EQUAL_MESSAGE(NULL, s_global_hdl_sem, "Semaphore creation failed");
    // Install USB and HID driver with 'hid_host_test_concurrent'
    test_hid_setup(hid_host_test_concurrent, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    // Wait for the HID device handle to be ready
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, xSemaphoreTake(s_global_hdl_sem, pdMS_TO_TICKS(5000)), "HID device handle not ready in time");
    // Start task to that polls the device with control transfers
    TEST_ASSERT_EQUAL(pdTRUE, xTaskCreate(access_task, "HID ctrl_xfer polling", 4096, NULL, 3, NULL));
    // Tear down test while access_task stresses the HID device
    test_hid_teardown();
    // Delete the semaphore
    vSemaphoreDelete(s_global_hdl_sem);
}

TEST_CASE("request Report Descriptor 32K", "[hid_host_extra_large_report]")
{
    // Create semaphore for s_global_hdl, because it will be used in multiple tasks access
    s_global_hdl_sem = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_EQUAL_MESSAGE(NULL, s_global_hdl_sem, "Semaphore creation failed");
    // Install USB and HID driver with 'hid_host_test_concurrent'
    test_hid_setup(hid_host_test_concurrent, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    // Wait the s_global_hdl to be valid 5sec timeout
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, xSemaphoreTake(s_global_hdl_sem, pdMS_TO_TICKS(5000)), "HID device handle not ready in time");
    // Get Report descriptor
    uint8_t *test_buffer = NULL;
    unsigned int test_length = 0;
    test_buffer = hid_host_get_report_descriptor(s_global_hdl, &test_length);
    // Expected to fail due to extra large Report Descriptor
    TEST_ASSERT_NULL_MESSAGE(test_buffer, "Report descriptor request should have failed for extra large size");
    // Tear down test
    test_hid_teardown();
    // Delete the semaphore
    vSemaphoreDelete(s_global_hdl_sem);
}

#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED
/**
 * @brief Basic Suspend/Resume sequence
 *
 * Purpose:
 *     - Test HID Host reaction to global suspend/resume events
 *
 * Procedure:
 *     - Install USB Host lib, Install HID driver, open device and start device
 *     - Suspend and resume the root port, check that correct interface events are delivered
 *     - Teardown
 */
TEST_CASE("suspend_resume_basic", "[hid_host_pm]")
{
    hid_host_test_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
    TEST_ASSERT_NOT_NULL(hid_host_test_event_queue);

    // Install USB and HID driver with 'hid_host_test_pm_driver_callback'
    test_hid_setup(hid_host_test_pm_driver_callback, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    open_start_device();

    printf("Issue suspend\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    hid_host_test_expect_event(&suspend_event, pdMS_TO_TICKS(5000), 2);

    printf("Issue resume\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());
    hid_host_test_expect_event(&resume_event, pdMS_TO_TICKS(5000), 2);

    // Tear down test
    test_hid_teardown();
    vQueueDelete(hid_host_test_event_queue);
    hid_host_test_event_queue = NULL;
}

#define TEST_HID_SUSPEND_TIMER_INTERVAL_MS   500
#define TEST_HID_SUSPEND_TIMER_MARGIN_MS     50

/**
 * @brief Automatic Suspend timer
 *
 * Purpose:
 *     - Test automatic suspend timer functionality (One-Shot and Periodic timer settings)
 *
 * Procedure:
 *     - Install USB Host lib, Install HID driver, open device and start device
 *     - Set automatic suspend timer, expect the root port to be suspended by expecting interface events
 *     - Issue a CTRL transfer to the device, expect the root port to be resumed
 *     - Teardown
 */
TEST_CASE("auto_suspend_timer", "[hid_host_pm]")
{
    hid_host_test_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
    TEST_ASSERT_NOT_NULL(hid_host_test_event_queue);

    // Install USB and HID driver with 'hid_host_test_pm_driver_callback'
    test_hid_setup(hid_host_test_pm_driver_callback, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    open_start_device();

    // Set one-shot auto suspend timer, and expect suspended event
    printf("Set One-Shot auto suspend timer\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_set_auto_suspend(USB_HOST_LIB_AUTO_SUSPEND_ONE_SHOT, TEST_HID_SUSPEND_TIMER_INTERVAL_MS));
    hid_host_test_expect_event(&suspend_event, pdMS_TO_TICKS(TEST_HID_SUSPEND_TIMER_INTERVAL_MS + TEST_HID_SUSPEND_TIMER_MARGIN_MS), 2);

    // Manually resume the root port and expect the resumed event
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());
    hid_host_test_expect_event(&resume_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

    // Make sure no other event is delivered, as the auto suspend timer is a one-shot timer
    hid_host_test_expect_no_event(pdMS_TO_TICKS(TEST_HID_SUSPEND_TIMER_INTERVAL_MS * 2));

    // Set periodic auto suspend timer
    printf("Set periodic auto suspend timer\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_set_auto_suspend(USB_HOST_LIB_AUTO_SUSPEND_PERIODIC, TEST_HID_SUSPEND_TIMER_INTERVAL_MS));

    for (int i = 0; i < 3; i++) {
        // Expect suspend event from the periodic auto suspend timer
        hid_host_test_expect_event(&suspend_event, pdMS_TO_TICKS(TEST_HID_SUSPEND_TIMER_INTERVAL_MS + TEST_HID_SUSPEND_TIMER_MARGIN_MS), 2);

        // Even though the periodic timer is running, don't expect any event because of suspended root port
        hid_host_test_expect_no_event(pdMS_TO_TICKS(TEST_HID_SUSPEND_TIMER_INTERVAL_MS * 2));

        // Manually resume the root port and expect the resumed event
        TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());
        hid_host_test_expect_event(&resume_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

        // Verify transfer on resumed device
        hid_host_dev_params_t dev_params;
        TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(s_global_hdl, &dev_params));
        test_hid_host_device_stress(&dev_params);
    }

    // Disable the periodic auto suspend timer
    printf("Disable periodic auto suspend timer\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_set_auto_suspend(USB_HOST_LIB_AUTO_SUSPEND_PERIODIC, 0));
    // Make sure no event is delivered
    hid_host_test_expect_no_event(pdMS_TO_TICKS(TEST_HID_SUSPEND_TIMER_INTERVAL_MS * 2));

    // Tear down test
    test_hid_teardown();
    vQueueDelete(hid_host_test_event_queue);
    hid_host_test_event_queue = NULL;
}
/**
 * @brief Resume by transfer submit
 *
 * Purpose:
 *     - Test, that a device can be resumed submitting a transfer
 *
 * Procedure:
 *     - Install USB Host lib, Install HID driver, open device and start device
 *     - Manually suspend the root port, expect suspend event
 *     - Issue a CTRL transfer to the device, expect the root port to be resumed, expect resume event
 *     - Manually suspend the root port, expect suspend event
 *     - Start the device, expect the root port to be resumed, expect resume event
 *     - Teardown
 */
TEST_CASE("resume_by_transfer_submit", "[hid_host_pm]")
{
    hid_host_test_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
    TEST_ASSERT_NOT_NULL(hid_host_test_event_queue);

    // Install USB and HID driver with 'hid_host_test_pm_driver_callback'
    test_hid_setup(hid_host_test_pm_driver_callback, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    open_start_device();

    // Suspend the root port manually
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    hid_host_test_expect_event(&suspend_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

    hid_host_dev_params_t dev_params;
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(s_global_hdl, &dev_params));

    // Auto resume the device by sending a ctrl transfer
    test_hid_host_device_stress(&dev_params);
    hid_host_test_expect_event(&resume_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

    // Suspend the root port manually
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    hid_host_test_expect_event(&suspend_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

    // Auto resume the device by calling device start
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_start(s_global_hdl));
    hid_host_test_expect_event(&resume_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

    // Tear down test
    test_hid_teardown();
    vQueueDelete(hid_host_test_event_queue);
    hid_host_test_event_queue = NULL;
}

/**
 * @brief Sudden disconnect with suspended device
 *
 * Purpose:
 *     - Test HID Host reaction to sudden disconnection with suspended device
 *
 * Procedure:
 *     - Install USB Host lib, Install HID driver, open device and start device
 *     - Suspend the root port, check that correct interface events are delivered
 *     - Disconnect the device, expect disconnection event to be delivered
 *     - Teardown
 */
TEST_CASE("sudden_disconnect_suspended_device", "[hid_host_pm]")
{
    hid_host_test_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
    TEST_ASSERT_NOT_NULL(hid_host_test_event_queue);

    // Install USB and HID driver with 'hid_host_test_pm_driver_callback'
    test_hid_setup(hid_host_test_pm_driver_callback, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    open_start_device();

    printf("Issue suspend\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    hid_host_test_expect_event(&suspend_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

    // Disconnect the device, while the root port is suspended
    force_conn_state(false, pdMS_TO_TICKS(1000));
    hid_host_test_expect_event(&dev_gone_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

    // Tear down test
    vTaskDelay(20);
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_uninstall() );
    ulTaskNotifyValueClear(NULL, UINT32_MAX);
    vTaskDelay(20);
    vQueueDelete(hid_host_test_event_queue);
    hid_host_test_event_queue = NULL;
}

/**
 * @brief Sudden disconnect during suspend sequence
 *
 * Purpose:
 *     - Test HID Host reaction to unfinished suspend sequence
 *
 * Procedure:
 *     - Install USB Host lib, Install HID driver, open device and start device
 *     - Host suspends the root port
 *     - Device disconnects itself as soon as it registers suspend event
 *     - Host expects disconnection event to be delivered
 *     - Device connects itself back
 *     - Host expects connection event and reopens the device
 *     - Teardown
 */
TEST_CASE("sudden_disconnect_during_suspend", "[hid_host_suspend_dconn]")
{
    hid_host_test_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
    TEST_ASSERT_NOT_NULL(hid_host_test_event_queue);

    // Install USB and HID driver with 'hid_host_test_pm_driver_callback'
    test_hid_setup(hid_host_test_pm_driver_callback, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    open_start_device();

    printf("Issue suspend\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());

    // The device has registered suspend event and suddenly disconnected the port

    // Expect device disconnect event and new device event
    hid_host_test_expect_event(&dev_gone_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);
    open_start_device();

    // Verify transfer on resumed device
    hid_host_dev_params_t dev_params;
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(s_global_hdl, &dev_params));
    test_hid_host_device_stress(&dev_params);

    // Tear down test
    vTaskDelay(20);
    test_hid_teardown();
    vQueueDelete(hid_host_test_event_queue);
    hid_host_test_event_queue = NULL;
}

/**
 * @brief Sudden disconnect during resume sequence
 *
 * Purpose:
 *     - Test HID Host reaction to unfinished resume sequence
 *
 * Procedure:
 *     - Install USB Host lib, Install HID driver, open and start the device, suspend and resumes the root port
 *     - Device disconnects itself as soon as it registers resume events
 *     - Host expects disconnection event to be delivered
 *     - Device connects itself back
 *     - Host expects connection event and reopens the device
 *     - Teardown
 */
TEST_CASE("sudden_disconnect_during_resume", "[hid_host_resume_dconn]")
{
    hid_host_test_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
    TEST_ASSERT_NOT_NULL(hid_host_test_event_queue);

    // Install USB and HID driver with 'hid_host_test_pm_driver_callback'
    test_hid_setup(hid_host_test_pm_driver_callback, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    open_start_device();

    printf("Issue suspend\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    hid_host_test_expect_event(&suspend_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

    // Keep suspended for a while
    vTaskDelay(100);

    printf("Issue resume\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());

    // The device has registered suspend event and suddenly disconnected the port

    // Expect device disconnect event and new device event
    hid_host_test_expect_event(&dev_gone_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);
    open_start_device();

    // Verify transfer on resumed device
    hid_host_dev_params_t dev_params;
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(s_global_hdl, &dev_params));
    test_hid_host_device_stress(&dev_params);

    // Tear down test
    vTaskDelay(20);
    test_hid_teardown();
    vQueueDelete(hid_host_test_event_queue);
    hid_host_test_event_queue = NULL;
}

#endif // HID_HOST_SUSPEND_RESUME_API_SUPPORTED
#ifdef HID_HOST_REMOTE_WAKE_SUPPORTED

/**
 * @brief Basic remote wakeup test
 *
 * Purpose:
 *     - Test remote wakeup generated from the device
 *
 * Procedure:
 *     - Install USB Host lib, Install HID driver, open and starts the device, suspend the root port
 *     - Device issues remote wakeup signalling after some time of being suspended
 *     - Host expects resume event
 *     - Teardown
 */
TEST_CASE("remote_wakeup", "[hid_host_remote_wake]")
{
    hid_host_test_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
    TEST_ASSERT_NOT_NULL(hid_host_test_event_queue);

    // Install USB and HID driver with 'hid_host_test_pm_driver_callback'
    test_hid_setup(hid_host_test_pm_driver_callback, HID_TEST_EVENT_HANDLE_IN_DRIVER);

    // Wait for both devices to connect
    hid_host_device_handle_t dev1_hdl = (hid_host_device_handle_t)hid_host_test_expect_event(&new_dev_event, pdMS_TO_TICKS(TEST_DEV_CONN_WAIT_MS), 1);
    hid_host_device_handle_t dev2_hdl = (hid_host_device_handle_t)hid_host_test_expect_event(&new_dev_event, pdMS_TO_TICKS(TEST_DEV_CONN_WAIT_MS), 1);
    TEST_ASSERT_NOT_NULL(dev1_hdl);
    TEST_ASSERT_NOT_NULL(dev2_hdl);
    s_global_hdl = dev1_hdl;

    // Device config
    const hid_host_device_config_t dev_config = {
        .callback = hid_host_pm_interface_callback,
        .callback_arg = &user_arg_value,
    };
    // Open and start one device
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_open(dev1_hdl, &dev_config));
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_start(dev1_hdl));

    // Enable remote wakeup
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_enable_remote_wakeup(dev1_hdl, true));

    printf("Issue suspend\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    hid_host_test_expect_event(&suspend_event, pdMS_TO_TICKS(5000), 1);

    // Keep suspended for a while
    vTaskDelay(100);

    // The device has registered suspend event and started issuing remote wakeup sequence after some time

    // Expect resume event (remote wakeup)
    hid_host_test_expect_event(&resume_event, pdMS_TO_TICKS(2000), 1);

    // Verify transfer on resumed device
    hid_host_dev_params_t dev_params;
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(dev1_hdl, &dev_params));
    test_hid_host_device_stress(&dev_params);

    // Tear down test
    vTaskDelay(20);
    test_hid_teardown();
    vQueueDelete(hid_host_test_event_queue);
    hid_host_test_event_queue = NULL;
}

/**
 * @brief Remote wakeup setting with multiple interfaces
 *
 * Purpose:
 *     - Test correctly enabling/disabling the remote wakeup on device with multiple interfaces
 *     - Remote wakeup feature is pre-device, not per interface
 *
 * Procedure:
 *     - Install USB Host lib, Install HID driver, wait for both interfaces to be connected, open and start them
 *     - Enable remote wakeup with 1st iface, disable it with 2nd iface
 *     - Don't expect remote wakeup from device, as it was enabled by 1st iface, then disabled by the 2nd iface
 *     - Reconnect the device and wait for bot interfaces to be connected, open and start them
 *     - Disable (already disabled remote wakeup) with 2nd iface, enable it with 1st iface
 *     - Expect remote wakeup for from device, as it was disabled (not set) by the 2nd iface, then enabled by 1st iface
 *     - Expect resume event, teardown
 */
TEST_CASE("remote_wakeup_multiple_ifaces", "[hid_host_remote_wake]")
{
    hid_host_test_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
    TEST_ASSERT_NOT_NULL(hid_host_test_event_queue);

    // Install USB and HID driver with 'hid_host_test_pm_driver_callback'
    test_hid_setup(hid_host_test_pm_driver_callback, HID_TEST_EVENT_HANDLE_IN_DRIVER);

    // Wait for both devices to connect
    hid_host_device_handle_t dev1_hdl = (hid_host_device_handle_t)hid_host_test_expect_event(&new_dev_event, pdMS_TO_TICKS(TEST_DEV_CONN_WAIT_MS), 1);
    hid_host_device_handle_t dev2_hdl = (hid_host_device_handle_t)hid_host_test_expect_event(&new_dev_event, pdMS_TO_TICKS(TEST_DEV_CONN_WAIT_MS), 1);
    TEST_ASSERT_NOT_NULL(dev1_hdl);
    TEST_ASSERT_NOT_NULL(dev2_hdl);

    // Device config
    const hid_host_device_config_t dev_config = {
        .callback = hid_host_pm_interface_callback,
        .callback_arg = &user_arg_value,
    };

    // Open and start both devices
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_open(dev1_hdl, &dev_config));
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_open(dev2_hdl, &dev_config));
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_start(dev1_hdl));
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_start(dev2_hdl));

    // Try to disable already disabled remote wakeup
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_enable_remote_wakeup(dev2_hdl, false));

    // Enable and disable remote wakeup
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_enable_remote_wakeup(dev1_hdl, true));
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_enable_remote_wakeup(dev2_hdl, false));

    printf("Issue suspend\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    hid_host_test_expect_event(&suspend_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

    // The device has registered suspend event, but remote wakeup is currently disabled

    // No resume event shall be delivered
    hid_host_test_expect_no_event(pdMS_TO_TICKS(5000));

    // Reconnect
    force_conn_state(false, 0);
    hid_host_test_expect_event(&dev_gone_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);
    force_conn_state(true, pdMS_TO_TICKS(1000));

    // Wait for both devices to connect
    dev1_hdl = (hid_host_device_handle_t)hid_host_test_expect_event(&new_dev_event, pdMS_TO_TICKS(TEST_DEV_CONN_WAIT_MS), 1);
    dev2_hdl = (hid_host_device_handle_t)hid_host_test_expect_event(&new_dev_event, pdMS_TO_TICKS(TEST_DEV_CONN_WAIT_MS), 1);

    // Open and start both devices
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_open(dev1_hdl, &dev_config));
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_open(dev2_hdl, &dev_config));
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_start(dev1_hdl));
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_start(dev2_hdl));

    // Try to disable already disabled remote wakeup
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_enable_remote_wakeup(dev2_hdl, false));
    // Enable remote wakeup
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_enable_remote_wakeup(dev1_hdl, true));
    // Try to enable already enabled remote wakeup
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_enable_remote_wakeup(dev2_hdl, true));

    printf("Issue suspend\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    hid_host_test_expect_event(&suspend_event, pdMS_TO_TICKS(TEST_EVENT_WAIT_MS), 2);

    // The device has registered suspend event, remote wakeup is currently enabled, expect resume event

    hid_host_test_expect_event(&resume_event, pdMS_TO_TICKS(5000), 2);

    // Verify transfer on resumed device
    hid_host_dev_params_t dev_params;
    TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_get_params(dev1_hdl, &dev_params));
    test_hid_host_device_stress(&dev_params);

    // Tear down test
    vTaskDelay(20);
    test_hid_teardown();
    vQueueDelete(hid_host_test_event_queue);
    hid_host_test_event_queue = NULL;
}

#endif // HID_HOST_REMOTE_WAKE_SUPPORTED
