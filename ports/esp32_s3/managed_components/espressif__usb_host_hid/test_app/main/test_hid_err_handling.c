/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_log.h"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usb/hid_host.h"

#include "test_hid_basic.h"

static const char *TAG = "hid-test";

// ----------------------- Private -------------------------
/**
 * @brief USB HID Host interface callback.
 *
 * Handle close event only.
 *
 * @param[in] event  HID Host interface event
 * @param[in] arg    Pointer to arguments, does not used
 *
 */
static void test_hid_host_interface_event_close(hid_host_device_handle_t hid_device_handle,
                                                const hid_host_interface_event_t event,
                                                void *arg)
{
    switch (event) {
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        TEST_ASSERT_EQUAL(ESP_OK, hid_host_device_close(hid_device_handle) );
        break;
    default:
        break;
    }
}

/**
 * @brief USB HID Host event callback stub.
 *
 * Does not handle anything.
 *
 * @param[in] event  HID Host device event
 * @param[in] arg    Pointer to arguments, does not used
 *
 */
static void test_hid_host_event_callback_stub(hid_host_device_handle_t hid_device_handle,
                                              const hid_host_driver_event_t event,
                                              void *arg)
{
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        // Device connected
    }
}

/**
 * @brief USB HID Host event callback.
 *
 * Handle connected event and open a device.
 *
 * @param[in] event  HID Host device event
 * @param[in] arg    Pointer to arguments, does not used
 *
 */
static void test_hid_host_event_callback_open(hid_host_device_handle_t hid_device_handle,
                                              const hid_host_driver_event_t event,
                                              void *arg)
{
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        const hid_host_device_config_t dev_config = {
            .callback = test_hid_host_interface_event_close,
            .callback_arg = NULL
        };

        TEST_ASSERT_EQUAL(ESP_OK,  hid_host_device_open(hid_device_handle, &dev_config) );
    }
}

// Install HID driver without USB Host and without configuration
static void test_install_hid_driver_without_config(void)
{
    ESP_LOGI(TAG, "Install driver without config");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, hid_host_install(NULL));
}

// Install HID driver without USB Host and with configuration
static void test_install_hid_driver_with_wrong_config(void)
{
    ESP_LOGI(TAG, "Install driver with incorrect config");
    const hid_host_driver_config_t hid_host_config_callback_null = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = NULL, /* error expected */
        .callback_arg = NULL
    };

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, hid_host_install(&hid_host_config_callback_null));

    const hid_host_driver_config_t hid_host_config_stack_size_null = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 0, /* error expected */
        .core_id = 0,
        .callback = test_hid_host_event_callback_stub,
        .callback_arg = NULL
    };

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, hid_host_install(&hid_host_config_stack_size_null));

    const hid_host_driver_config_t hid_host_config_task_priority_null = {
        .create_background_task = true,
        .task_priority = 0,/* error expected */
        .stack_size = 4096,
        .core_id = 0,
        .callback = test_hid_host_event_callback_stub,
        .callback_arg = NULL
    };

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, hid_host_install(&hid_host_config_task_priority_null));

    const hid_host_driver_config_t hid_host_config_correct = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = test_hid_host_event_callback_stub,
        .callback_arg = NULL
    };
    // Invalid state without USB Host installed
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, hid_host_install(&hid_host_config_correct));
}

void test_interface_callback_handler(hid_host_device_handle_t hid_device_handle,
                                     const hid_host_interface_event_t event,
                                     void *arg)
{
    // ...
}

// Open device without installed driver
static void test_claim_interface_without_driver(void)
{
    ESP_LOGI(TAG, "Claim interface without driver installed");
    hid_host_device_handle_t hid_dev_handle = NULL;

    const hid_host_device_config_t dev_config = {
        .callback = test_interface_callback_handler,
        .callback_arg = NULL
    };

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hid_host_device_open(hid_dev_handle, &dev_config) );
}

static void test_install_hid_driver_when_already_installed(void)
{
    ESP_LOGI(TAG, "Install driver while driver already installed");
    // Install USB and HID driver with the stub test_hid_host_event_callback_stub
    test_hid_setup(test_hid_host_event_callback_stub, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    // Try to install HID driver again
    const hid_host_driver_config_t hid_host_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = test_hid_host_event_callback_stub,
        .callback_arg = NULL
    };
    // Verify error code
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, hid_host_install(&hid_host_config));
    // Tear down test
    test_hid_teardown();
}

static void test_uninstall_hid_driver_while_device_was_not_opened(void)
{
    ESP_LOGI(TAG, "Uninstall driver with no device opened");
    // Install USB and HID driver with the stub test_hid_host_event_callback_stub
    test_hid_setup(test_hid_host_event_callback_stub, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    // Tear down test
    test_hid_teardown();
}

static void test_uninstall_hid_driver_while_device_is_present(void)
{
    ESP_LOGI(TAG, "Uninstall driver while device is present");
    // Install USB and HID driver with the stub test_hid_host_event_callback_stub
    test_hid_setup(test_hid_host_event_callback_open, HID_TEST_EVENT_HANDLE_IN_DRIVER);
    // Wait for USB device appearing for 250 msec
    vTaskDelay(250);
    // Uninstall HID Driver while device is still connected and verify a result
    printf("HID Driver uninstall attempt while HID Device is still present ...\n");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, hid_host_uninstall());
    // Tear down test
    test_hid_teardown();
}

#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED

static QueueHandle_t test_event_queue = NULL;
typedef struct {
    hid_host_device_handle_t hid_device_handle;
} test_event_queue_t;

/**
 * @brief USB HID Host event callback device open.
 *
 * Handles only device connect event delivery
 *
 * @param[in] event  HID Host device event
 * @param[in] arg    Pointer to arguments, not used
 *
 */
static void test_hid_host_event_callback_open_event(hid_host_device_handle_t hid_device_handle,
                                                    const hid_host_driver_event_t event,
                                                    void *arg)
{
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        const test_event_queue_t evt_queue = {
            .hid_device_handle = hid_device_handle,
        };

        if (test_event_queue) {
            TEST_ASSERT_EQUAL(pdTRUE, xQueueSend(test_event_queue, &evt_queue, 0));
        }
    }
}

/**
 * @brief Open suspended device
 *
 * Purpose:
 *     - Test HID Host reaction to opening a device, which is in suspended state
 *
 * Procedure:
 *     - Install USB Host lib, Install HID driver, wait for device connection
 *     - Suspend the root port and fail to open a device
 *     - Resume the root port, teardown
 */
static void test_open_suspended_device(void)
{
    ESP_LOGI(TAG, "Open suspended device");
    test_event_queue = xQueueCreate(4, sizeof(test_event_queue_t));
    TEST_ASSERT_NOT_NULL(test_event_queue);

    test_hid_setup(test_hid_host_event_callback_open_event, HID_TEST_EVENT_HANDLE_IN_DRIVER);

    // Make sure connect events are delivered from both devices
    test_event_queue_t queue_item_1, queue_item_2;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(test_event_queue, &queue_item_1, pdMS_TO_TICKS(500)));
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(test_event_queue, &queue_item_2, pdMS_TO_TICKS(500)));

    printf("Issue suspend\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    vTaskDelay(pdMS_TO_TICKS(100));

    const hid_host_device_config_t dev_config = {
        .callback = test_interface_callback_handler,
        .callback_arg = NULL
    };

    // Try to open devices with the root port suspended
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, hid_host_device_open(queue_item_1.hid_device_handle, &dev_config));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, hid_host_device_open(queue_item_2.hid_device_handle, &dev_config));

    printf("Issue resume\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());
    vTaskDelay(pdMS_TO_TICKS(100));

    // Teardown
    test_hid_teardown();
    vQueueDelete(test_event_queue);
    test_event_queue = NULL;
}

#endif // HID_HOST_SUSPEND_RESUME_API_SUPPORTED

// ----------------------- Public --------------------------

/**
 * @brief HID Error handling test
 *
 * There are multiple erroneous scenarios checked in this test.
 *
 */
TEST_CASE("error_handling", "[hid_host]")
{
    test_install_hid_driver_without_config();
    test_install_hid_driver_with_wrong_config();
    test_claim_interface_without_driver();
    test_install_hid_driver_when_already_installed();
    test_uninstall_hid_driver_while_device_was_not_opened();
    test_uninstall_hid_driver_while_device_is_present();
#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED
    test_open_suspended_device();
#endif // HID_HOST_SUSPEND_RESUME_API_SUPPORTED
}
