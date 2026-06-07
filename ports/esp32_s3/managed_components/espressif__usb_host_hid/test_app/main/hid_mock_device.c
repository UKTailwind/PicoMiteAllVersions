/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "esp_log.h"
#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define EVT_QUEUE_LENGTH 5

const char *HID_DEV_TAG = "HID device";
static QueueHandle_t dev_evt_queue = NULL;
static StaticQueue_t s_queue_buf;
uint8_t ucQueueStorage[EVT_QUEUE_LENGTH * sizeof(tinyusb_event_t)];

/************* Device types ****************/

// Define test HID mock device types
enum test_hid_mock_device {
    TEST_HID_MOCK_DEVICE_WITH_ONE_IFACE = 0,
    TEST_HID_MOCK_DEVICE_WITH_TWO_IFACES,
    TEST_HID_MOCK_DEVICE_WITH_REPORT_DESC_1905B,
    TEST_HID_MOCK_DEVICE_WITH_REPORT_DESC_32KB,
    TEST_HID_MOCK_DEVICE_SUSPEND_DCONN,
    TEST_HID_MOCK_DEVICE_RESUME_DCONN,
    TEST_HID_MOCK_DEVICE_REMOTE_WAKE,
    TEST_HID_MOCK_DEVICE_MAX,
};

static enum test_hid_mock_device _test_hid_mock_device_mode = TEST_HID_MOCK_DEVICE_MAX;

/************* TinyUSB descriptors ****************/
#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD) ),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE) )
};

const uint8_t hid_keyboard_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD) )
};

const uint8_t hid_mouse_report_descriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE) )
};

/**
 * Valid HID report descriptor of 1905 bytes.
 *
 * This large descriptor is intentionally sized to exceed the default 512-byte buffer
 * used for HID report descriptors. The size is larger than the default buffer limit,
 * forcing the HID Host driver to realloc the memory for CTRL transfer.
*/
const uint8_t hid_report_descriptor_1905B[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD) ),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE) ),
    TUD_HID_REPORT_DESC_LIGHTING(3),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(4)),
    TUD_HID_REPORT_DESC_GAMEPAD(HID_REPORT_ID(5)),
    TUD_HID_REPORT_DESC_SYSTEM_CONTROL(HID_REPORT_ID(6)),
    TUD_HID_REPORT_DESC_LIGHTING(7),
    TUD_HID_REPORT_DESC_LIGHTING(8),
    TUD_HID_REPORT_DESC_LIGHTING(9),
    TUD_HID_REPORT_DESC_LIGHTING(10),
};

/**
 * Empty HID report descriptor of 32 KB.
*/
const uint8_t hid_report_descriptor_32KB[32 * 1024] = {0};

/**
 * @brief String descriptor
 */
const char *hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "TinyUSB",             // 1: Manufacturer
    "TinyUSB Device",      // 2: Product
    "123456",              // 3: Serials, should use chip ID
    "Example HID interface",  // 4: HID
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor_one_iface[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, CFG_TUD_HID, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
    TUD_HID_DESCRIPTOR(1, 4, false, sizeof(hid_report_descriptor), 0x82, 16, 10),
};

static const uint8_t hid_configuration_descriptor_two_ifaces[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, CFG_TUD_HID, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_KEYBOARD, sizeof(hid_keyboard_report_descriptor), 0x81, 16, 10),
    TUD_HID_DESCRIPTOR(2, 4, HID_ITF_PROTOCOL_MOUSE, sizeof(hid_mouse_report_descriptor), 0x82, 16, 10),
};

static const uint8_t hid_configuration_descriptor_report_1905B[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, CFG_TUD_HID, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor_1905B), 0x81, 16, 10),
    TUD_HID_DESCRIPTOR(1, 4, false, sizeof(hid_report_descriptor_1905B), 0x82, 16, 10),
};

static const uint8_t hid_configuration_descriptor_report_32K[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, CFG_TUD_HID, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, 32 * 1024, 0x81, 16, 10),
    TUD_HID_DESCRIPTOR(1, 4, false, 32 * 1024, 0x82, 16, 10),
};


static const uint8_t *hid_configuration_descriptor_list[TEST_HID_MOCK_DEVICE_MAX] = {
    hid_configuration_descriptor_one_iface,
    hid_configuration_descriptor_two_ifaces,
    hid_configuration_descriptor_report_1905B,
    hid_configuration_descriptor_report_32K,
    hid_configuration_descriptor_two_ifaces,
    hid_configuration_descriptor_two_ifaces,
    hid_configuration_descriptor_two_ifaces,
};

/**
 * @brief Device qualifier
 *
 * This is a simple device qualifier for HS devices
 */

#if (TUD_OPT_HIGH_SPEED)
static const tusb_desc_device_qualifier_t device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0
};
#endif // TUD_OPT_HIGH_SPEED
/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    switch (_test_hid_mock_device_mode) {
    case TEST_HID_MOCK_DEVICE_WITH_ONE_IFACE:
        return hid_report_descriptor;
    case TEST_HID_MOCK_DEVICE_WITH_TWO_IFACES:
    case TEST_HID_MOCK_DEVICE_SUSPEND_DCONN:
    case TEST_HID_MOCK_DEVICE_RESUME_DCONN:
    case TEST_HID_MOCK_DEVICE_REMOTE_WAKE:
        return (!!instance) ? hid_mouse_report_descriptor : hid_keyboard_report_descriptor;
    case TEST_HID_MOCK_DEVICE_WITH_REPORT_DESC_1905B:
        return hid_report_descriptor_1905B;
    case TEST_HID_MOCK_DEVICE_WITH_REPORT_DESC_32KB:
        return hid_report_descriptor_32KB;
    default:
        TEST_FAIL_MESSAGE("HID mock device, unhandled test mode");
    }
    return NULL;
}

/**
 * @brief Get Keyboard report
 *
 * Fill buffer with test Keyboard report data
 *
 * @param[in] buffer   Pointer to a buffer for filling
 * @return uint16_t    Length of copied data to buffer
 */
static inline uint16_t get_keyboard_report(uint8_t *buffer)
{
    hid_keyboard_report_t kb_report = {
        0,                              // Keyboard modifier
        0,                              // Reserved
        { HID_KEY_M, HID_KEY_N, HID_KEY_O, HID_KEY_P, HID_KEY_Q, HID_KEY_R }
    };
    memcpy(buffer, &kb_report, sizeof(kb_report));
    return sizeof(kb_report);
}

/**
 * @brief Get Mouse report
 *
 * Fill buffer with test Mouse report data
 *
 * @param[in] buffer   Pointer to a buffer for filling
 * @return uint16_t    Length of copied data to buffer
 */
static inline uint16_t get_mouse_report(uint8_t *buffer)
{
    hid_mouse_report_t mouse_report = {
        MOUSE_BUTTON_LEFT | MOUSE_BUTTON_RIGHT, // buttons
        -1,                                     // x
        127,                                    // y
        0,                                      // wheel
        0                                       // pan
    };
    memcpy(buffer, &mouse_report, sizeof(mouse_report));
    return sizeof(mouse_report);
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    switch (report_id) {
    case HID_ITF_PROTOCOL_KEYBOARD:
        return get_keyboard_report(buffer);

    case HID_ITF_PROTOCOL_MOUSE:
        return get_mouse_report(buffer);

    default:
        ESP_LOGW(HID_DEV_TAG, "Unhandled ReportID %d", report_id);
        break;
    }
    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{

}

// HID Mock Device

/**
 * @brief Callback for device events.
 *
 * @note
 * For Linux-based Hosts: Reflects the SetConfiguration() request from the Host Driver.
 * For Win-based Hosts: SetConfiguration() request is present only with available Class in device descriptor.
 */
static void hid_mock_device_event_handler(tinyusb_event_t *event, void *arg)
{
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        ESP_LOGI(HID_DEV_TAG, "\t --> Attached");
        break;
    case TINYUSB_EVENT_DETACHED:
        ESP_LOGI(HID_DEV_TAG, "\t <-- Detached");
        break;
    case TINYUSB_EVENT_SUSPENDED:
        ESP_LOGI(HID_DEV_TAG, "\t --> Suspended with remote wakeup %s\n", ((event->suspended.remote_wakeup) ? ("enabled") : ("disabled")));
        break;
    case TINYUSB_EVENT_RESUMED:
        ESP_LOGI(HID_DEV_TAG, "\t --> Resumed");
        break;
    default:
        break;
    }

    xQueueSend(dev_evt_queue, event, 0);
}

/**
 * @brief Run HID mock device in selected mode
 *
 * @param[in] mode   Device mode
 */
static void hid_mock_device_run(const enum test_hid_mock_device mode)
{
    // Create static app message queue
    dev_evt_queue = xQueueCreateStatic(EVT_QUEUE_LENGTH, sizeof(tinyusb_event_t), &(ucQueueStorage[0]), &s_queue_buf);
    configASSERT(dev_evt_queue);

    // Validate test mode parameter, should be less than max value
    TEST_ASSERT_LESS_THAN_MESSAGE(TEST_HID_MOCK_DEVICE_MAX, mode, "HID mock device set mode, wrong test mode");
    _test_hid_mock_device_mode = mode;

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(hid_mock_device_event_handler);

    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor_list[_test_hid_mock_device_mode];
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = hid_configuration_descriptor_list[_test_hid_mock_device_mode];
    tusb_cfg.descriptor.qualifier = &device_qualifier;
#endif // TUD_OPT_HIGH_SPEED
    tusb_cfg.descriptor.string = hid_string_descriptor;
    tusb_cfg.descriptor.string_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    // Print the message about the mode
    // Note: these messages are used in the pytest, so change them carefully
    switch (_test_hid_mock_device_mode) {
    case TEST_HID_MOCK_DEVICE_WITH_ONE_IFACE:
        printf("HID mock device with 1xInterface (Protocol=None) has been started\n");
        break;
    case TEST_HID_MOCK_DEVICE_WITH_TWO_IFACES:
    case TEST_HID_MOCK_DEVICE_SUSPEND_DCONN:
    case TEST_HID_MOCK_DEVICE_RESUME_DCONN:
    case TEST_HID_MOCK_DEVICE_REMOTE_WAKE:
        printf("HID mock device with 2xInterfaces (Protocol=BootKeyboard, Protocol=BootMouse) has been started\n");
        break;
    case TEST_HID_MOCK_DEVICE_WITH_REPORT_DESC_1905B:
        printf("HID mock device with large report descriptor has been started\n");
        break;
    case TEST_HID_MOCK_DEVICE_WITH_REPORT_DESC_32KB:
        printf("HID mock device with extra large report descriptor has been started\n");
        break;
    default:
        TEST_FAIL_MESSAGE("HID mock device, unhandled test mode");
        break;
    }
}

/**
 * @brief Run HID Device with 1 interface
 */
TEST_CASE("mock_hid_device_with_one_iface", "[hid_device][ignore]")
{
    hid_mock_device_run(TEST_HID_MOCK_DEVICE_WITH_ONE_IFACE);

    while (1) {
        vTaskDelay(10);
    }
}

/**
 * @brief Run HID Device with 2 interfaces
 */
TEST_CASE("mock_hid_device_with_two_ifaces", "[hid_device_two_ifaces][ignore]")
{
    hid_mock_device_run(TEST_HID_MOCK_DEVICE_WITH_TWO_IFACES);

    while (1) {
        vTaskDelay(10);
    }
}

/**
 * @brief Run HID Device with 2 interfaces and large report descriptor
 */
TEST_CASE("mock_hid_device_with_large_report", "[hid_device_large_report][ignore]")
{
    hid_mock_device_run(TEST_HID_MOCK_DEVICE_WITH_REPORT_DESC_1905B);

    while (1) {
        vTaskDelay(10);
    }
}

/**
 * @brief Run HID Device with 2 interfaces and 32kb report descriptor
 */
TEST_CASE("mock_hid_device_with_32K_report", "[hid_device_extra_large_report][ignore]")
{
    hid_mock_device_run(TEST_HID_MOCK_DEVICE_WITH_REPORT_DESC_32KB);

    while (1) {
        vTaskDelay(10);
    }
}

/**
 * @brief Run HID Device with 2 interfaces
 *
 * Device performs sudden disconnect followed by a reconnect upon receiving device suspend event
 */
TEST_CASE("mock_hid_device_disconnect_during_suspend", "[hid_device_suspend_dconn][ignore]")
{
    hid_mock_device_run(TEST_HID_MOCK_DEVICE_SUSPEND_DCONN);

    while (1) {
        tinyusb_event_t dev_evt;
        if (xQueueReceive(dev_evt_queue, &dev_evt, portMAX_DELAY)) {
            switch (dev_evt.id) {
            case TINYUSB_EVENT_SUSPENDED:
                TEST_ASSERT(tud_disconnect());
                ESP_LOGI(HID_DEV_TAG, "Triggering disconnect");

                // Stay disconnected for a while and trigger connect
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP_LOGI(HID_DEV_TAG, "Triggering reconnect");
                TEST_ASSERT(tud_connect());
                break;
            default:
                break;
            }
        }
    }
}

/**
 * @brief Run HID Device with 2 interfaces
 *
 * Device performs sudden disconnect followed by a reconnect upon receiving device resume event
 */
TEST_CASE("mock_hid_device_disconnect_during_resume", "[hid_device_resume_dconn][ignore]")
{
    hid_mock_device_run(TEST_HID_MOCK_DEVICE_RESUME_DCONN);

    while (1) {
        tinyusb_event_t dev_evt;
        if (xQueueReceive(dev_evt_queue, &dev_evt, portMAX_DELAY)) {
            switch (dev_evt.id) {
            case TINYUSB_EVENT_RESUMED:
                TEST_ASSERT(tud_disconnect());
                ESP_LOGI(HID_DEV_TAG, "Triggering disconnect");

                // Stay disconnected for a while and trigger connect
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP_LOGI(HID_DEV_TAG, "Triggering reconnect");
                TEST_ASSERT(tud_connect());
                break;
            default:
                break;
            }
        }
    }
}

/**
 * @brief Run HID Device with 2 interfaces
 *
 * Device performs remote wakeup (if enabled by the host) upon receiving device resume event
 */
TEST_CASE("mock_hid_device_remote_wake", "[hid_device_remote_wake][ignore]")
{
    hid_mock_device_run(TEST_HID_MOCK_DEVICE_REMOTE_WAKE);

    while (1) {
        tinyusb_event_t dev_evt;
        if (xQueueReceive(dev_evt_queue, &dev_evt, portMAX_DELAY)) {
            switch (dev_evt.id) {
            case TINYUSB_EVENT_SUSPENDED:
                // Stay suspended for a while
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (dev_evt.suspended.remote_wakeup) {
                    ESP_LOGI(HID_DEV_TAG, "Triggering remote wakeup");
                    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_remote_wakeup());
                } else {
                    ESP_LOGI(HID_DEV_TAG, "Remote wakeup not enabled by host");
                    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, tinyusb_remote_wakeup());
                }
                break;
            default:
                break;
            }
        }
    }
}
