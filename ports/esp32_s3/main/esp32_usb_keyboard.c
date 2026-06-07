/*
 * esp32_usb_keyboard.c - USB HID boot-keyboard input for keyboard USB role.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/hid.h"
#include "usb/usb_host.h"
#include "usb/hid_usage_keyboard.h"
#include "hal/usb_serial_jtag_ll.h"

static const char * TAG = "esp32_usb_kbd";

static QueueHandle_t s_key_queue;
static usb_host_client_handle_t s_probe_client;
static usb_device_handle_t s_raw_kbd_device;
static usb_transfer_t * s_raw_kbd_transfer;
static bool s_host_start_attempted;
static volatile int s_keyboard_attached;
static portMUX_TYPE s_repeat_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_prev_keys[HID_KEYBOARD_KEY_MAX];
static uint8_t s_repeat_hid_key;
static uint8_t s_repeat_modifier;
static int s_repeat_key;
static TickType_t s_repeat_next_tick;
static bool s_repeat_active;

static const uint8_t keycode2ascii[57][2] = {
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {'a', 'A'},
    {'b', 'B'},
    {'c', 'C'},
    {'d', 'D'},
    {'e', 'E'},
    {'f', 'F'},
    {'g', 'G'},
    {'h', 'H'},
    {'i', 'I'},
    {'j', 'J'},
    {'k', 'K'},
    {'l', 'L'},
    {'m', 'M'},
    {'n', 'N'},
    {'o', 'O'},
    {'p', 'P'},
    {'q', 'Q'},
    {'r', 'R'},
    {'s', 'S'},
    {'t', 'T'},
    {'u', 'U'},
    {'v', 'V'},
    {'w', 'W'},
    {'x', 'X'},
    {'y', 'Y'},
    {'z', 'Z'},
    {'1', '!'},
    {'2', '@'},
    {'3', '#'},
    {'4', '$'},
    {'5', '%'},
    {'6', '^'},
    {'7', '&'},
    {'8', '*'},
    {'9', '('},
    {'0', ')'},
    {ENTER, ENTER},
    {ESC, ESC},
    {BKSP, BKSP},
    {'\t', '\t'},
    {' ', ' '},
    {'-', '_'},
    {'=', '+'},
    {'[', '{'},
    {']', '}'},
    {'\\', '|'},
    {'\\', '|'},
    {';', ':'},
    {'\'', '"'},
    {'`', '~'},
    {',', '<'},
    {'.', '>'},
    {'/', '?'},
};

static void queue_key(int key) {
    if (!s_key_queue || key == 0) return;
    xQueueSend(s_key_queue, &key, 0);
}

static bool modifier_shift(uint8_t modifier) {
    return (modifier & (HID_LEFT_SHIFT | HID_RIGHT_SHIFT)) != 0;
}

static bool modifier_ctrl(uint8_t modifier) {
    return (modifier & (HID_LEFT_CONTROL | HID_RIGHT_CONTROL)) != 0;
}

static bool key_found(const uint8_t * src, uint8_t key, unsigned len);

static int translate_key(uint8_t modifier, uint8_t key_code) {
    if (key_code >= HID_KEY_A && key_code <= HID_KEY_Z && modifier_ctrl(modifier)) {
        return (key_code - HID_KEY_A) + 1;
    }
    if (key_code < (sizeof keycode2ascii / sizeof keycode2ascii[0])) {
        int shifted = modifier_shift(modifier) ? 1 : 0;
        return keycode2ascii[key_code][shifted];
    }

    switch (key_code) {
    case HID_KEY_F1:
        return F1;
    case HID_KEY_F2:
        return F2;
    case HID_KEY_F3:
        return F3;
    case HID_KEY_F4:
        return F4;
    case HID_KEY_F5:
        return F5;
    case HID_KEY_F6:
        return F6;
    case HID_KEY_F7:
        return F7;
    case HID_KEY_F8:
        return F8;
    case HID_KEY_F9:
        return F9;
    case HID_KEY_F10:
        return F10;
    case HID_KEY_F11:
        return F11;
    case HID_KEY_F12:
        return F12;
    case HID_KEY_INSERT:
        return INSERT;
    case HID_KEY_HOME:
        return HOME;
    case HID_KEY_PAGEUP:
        return PUP;
    case HID_KEY_DELETE:
        return DEL;
    case HID_KEY_END:
        return END;
    case HID_KEY_PAGEDOWN:
        return PDOWN;
    case HID_KEY_RIGHT:
        return RIGHT;
    case HID_KEY_LEFT:
        return LEFT;
    case HID_KEY_DOWN:
        return DOWN;
    case HID_KEY_UP:
        return UP;
    default:
        return 0;
    }
}

static TickType_t repeat_delay_ticks(int ms, int fallback_ms) {
    if (ms <= 0) ms = fallback_ms;
    TickType_t ticks = pdMS_TO_TICKS((uint32_t)ms);
    return ticks ? ticks : 1;
}

static bool tick_due(TickType_t now, TickType_t due) {
    return (int32_t)(now - due) >= 0;
}

static void repeat_clear_locked(void) {
    s_repeat_hid_key = 0;
    s_repeat_modifier = 0;
    s_repeat_key = 0;
    s_repeat_next_tick = 0;
    s_repeat_active = false;
}

static void repeat_begin_locked(uint8_t modifier, uint8_t hid_key) {
    int key = translate_key(modifier, hid_key);
    if (!key) {
        repeat_clear_locked();
        return;
    }
    s_repeat_hid_key = hid_key;
    s_repeat_modifier = modifier;
    s_repeat_key = key;
    s_repeat_next_tick = xTaskGetTickCount() +
                         repeat_delay_ticks(Option.RepeatStart, 600);
    s_repeat_active = true;
}

static void repeat_update_from_report(uint8_t modifier, const uint8_t * keys) {
    portENTER_CRITICAL(&s_repeat_mux);
    if (s_repeat_active && key_found(keys, s_repeat_hid_key, HID_KEYBOARD_KEY_MAX)) {
        s_repeat_modifier = modifier;
        s_repeat_key = translate_key(modifier, s_repeat_hid_key);
        if (!s_repeat_key) repeat_clear_locked();
    } else {
        repeat_clear_locked();
        for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
            uint8_t key = keys[i];
            if (key > HID_KEY_ERROR_UNDEFINED && translate_key(modifier, key)) {
                repeat_begin_locked(modifier, key);
                break;
            }
        }
    }
    portEXIT_CRITICAL(&s_repeat_mux);
}

static void repeat_start_for_new_key(uint8_t modifier, uint8_t hid_key) {
    portENTER_CRITICAL(&s_repeat_mux);
    repeat_begin_locked(modifier, hid_key);
    portEXIT_CRITICAL(&s_repeat_mux);
}

static void synth_repeat_due(void) {
    int key = 0;
    TickType_t now = xTaskGetTickCount();

    portENTER_CRITICAL(&s_repeat_mux);
    if (s_repeat_active && s_repeat_key && tick_due(now, s_repeat_next_tick)) {
        key = s_repeat_key;
        TickType_t interval = repeat_delay_ticks(Option.RepeatRate, 150);
        s_repeat_next_tick += interval;
        if (tick_due(now, s_repeat_next_tick)) s_repeat_next_tick = now + interval;
    }
    portEXIT_CRITICAL(&s_repeat_mux);

    if (key) queue_key(key);
}

static bool key_found(const uint8_t * src, uint8_t key, unsigned len) {
    for (unsigned i = 0; i < len; i++) {
        if (src[i] == key) return true;
    }
    return false;
}

static void keyboard_report(const uint8_t * data, int length) {
    if (length < (int)sizeof(hid_keyboard_input_report_boot_t)) return;
    hid_keyboard_input_report_boot_t * report = (hid_keyboard_input_report_boot_t *)data;

    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        uint8_t key = report->key[i];
        if (key > HID_KEY_ERROR_UNDEFINED &&
            !key_found(s_prev_keys, key, HID_KEYBOARD_KEY_MAX)) {
            queue_key(translate_key(report->modifier.val, key));
            repeat_start_for_new_key(report->modifier.val, key);
        }
    }

    repeat_update_from_report(report->modifier.val, report->key);
    memcpy(s_prev_keys, report->key, HID_KEYBOARD_KEY_MAX);
}

static void usb_probe_client_callback(const usb_host_client_event_msg_t * event_msg,
                                      void * arg) {
    (void)arg;
    if (!event_msg) return;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (event_msg->dev_gone.dev_hdl == s_raw_kbd_device) {
            s_keyboard_attached = 0;
            s_raw_kbd_device = NULL;
            s_raw_kbd_transfer = NULL;
        }
        break;
    default:
        break;
    }
}

static void raw_keyboard_transfer_callback(usb_transfer_t * transfer) {
    if (!transfer) return;
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        keyboard_report(transfer->data_buffer, (int)transfer->actual_num_bytes);
    }

    if (s_raw_kbd_device && transfer == s_raw_kbd_transfer) {
        usb_host_transfer_submit(transfer);
    }
}

static bool raw_keyboard_attach(usb_device_handle_t handle,
                                const usb_config_desc_t * config_desc,
                                uint8_t iface_num,
                                uint8_t alt_setting) {
    if (s_raw_kbd_device || !handle || !config_desc) return false;

    uint8_t endpoint = 0;
    uint16_t packet_size = 0;
    const uint8_t * p = (const uint8_t *)config_desc;
    bool in_target_iface = false;
    for (int offset = 0; offset < config_desc->wTotalLength;) {
        uint8_t length = p[offset];
        uint8_t type = p[offset + 1];
        if (length < 2 || offset + length > config_desc->wTotalLength) break;
        if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && length >= sizeof(usb_intf_desc_t)) {
            const usb_intf_desc_t * intf = (const usb_intf_desc_t *)(p + offset);
            in_target_iface = intf->bInterfaceNumber == iface_num &&
                              intf->bAlternateSetting == alt_setting;
        } else if (in_target_iface &&
                   type == USB_B_DESCRIPTOR_TYPE_ENDPOINT &&
                   length >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t * ep = (const usb_ep_desc_t *)(p + offset);
            bool is_in = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
            bool is_int = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) ==
                          USB_BM_ATTRIBUTES_XFER_INT;
            if (is_in && is_int) {
                endpoint = ep->bEndpointAddress;
                packet_size = ep->wMaxPacketSize;
                break;
            }
        }
        offset += length;
    }

    if (!endpoint || packet_size == 0) {
        return false;
    }

    esp_err_t err = usb_host_interface_claim(s_probe_client, handle, iface_num, alt_setting);
    if (err != ESP_OK) return false;

    usb_transfer_t * transfer = NULL;
    err = usb_host_transfer_alloc(packet_size, 0, &transfer);
    if (err != ESP_OK || !transfer) {
        usb_host_interface_release(s_probe_client, handle, iface_num);
        return false;
    }

    transfer->device_handle = handle;
    transfer->bEndpointAddress = endpoint;
    transfer->callback = raw_keyboard_transfer_callback;
    transfer->context = NULL;
    transfer->num_bytes = packet_size;

    s_raw_kbd_device = handle;
    s_raw_kbd_transfer = transfer;
    s_keyboard_attached = 1;

    err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
        s_keyboard_attached = 0;
        s_raw_kbd_device = NULL;
        s_raw_kbd_transfer = NULL;
        usb_host_transfer_free(transfer);
        usb_host_interface_release(s_probe_client, handle, iface_num);
        return false;
    }
    return true;
}

static bool find_keyboard_interface(const usb_config_desc_t * config_desc,
                                    uint8_t * iface_num,
                                    uint8_t * alt_setting) {
    if (!config_desc || !iface_num || !alt_setting) return false;
    const uint8_t * p = (const uint8_t *)config_desc;
    for (int offset = 0; offset < config_desc->wTotalLength;) {
        uint8_t length = p[offset];
        uint8_t type = p[offset + 1];
        if (length < 2 || offset + length > config_desc->wTotalLength) break;
        if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && length >= sizeof(usb_intf_desc_t)) {
            const usb_intf_desc_t * intf = (const usb_intf_desc_t *)(p + offset);
            if (intf->bInterfaceClass == USB_CLASS_HID &&
                intf->bInterfaceProtocol == HID_PROTOCOL_KEYBOARD) {
                *iface_num = intf->bInterfaceNumber;
                *alt_setting = intf->bAlternateSetting;
                return true;
            }
        }
        offset += length;
    }
    return false;
}

static void probe_scan_devices(void) {
    if (!s_probe_client) return;

    uint8_t addresses[8] = {0};
    int address_count = 0;
    esp_err_t err = usb_host_device_addr_list_fill((int)sizeof(addresses),
                                                   addresses, &address_count);
    if (err != ESP_OK) return;

    for (int i = 0; i < address_count; i++) {
        uint8_t address = addresses[i];
        if (address == 0) continue;

        usb_device_handle_t handle = NULL;
        err = usb_host_device_open(s_probe_client, address, &handle);
        if (err != ESP_OK || !handle) continue;

        const usb_config_desc_t * config_desc = NULL;
        err = usb_host_get_active_config_descriptor(handle, &config_desc);
        bool keep_open = false;
        if (err == ESP_OK && config_desc) {
            uint8_t iface_num = 0;
            uint8_t alt_setting = 0;
            if (find_keyboard_interface(config_desc, &iface_num, &alt_setting) && !s_raw_kbd_device)
                keep_open = raw_keyboard_attach(handle, config_desc, iface_num, alt_setting);
        }

        if (!keep_open) usb_host_device_close(s_probe_client, handle);
        if (keep_open) break;
    }
}

static void usb_probe_client_task(void * arg) {
    (void)arg;
    TickType_t last_scan = 0;
    while (true) {
        if (!s_probe_client) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        esp_err_t err = usb_host_client_handle_events(s_probe_client, pdMS_TO_TICKS(50));
        (void)err;
        TickType_t now = xTaskGetTickCount();
        if (now - last_scan >= pdMS_TO_TICKS(500)) {
            last_scan = now;
            probe_scan_devices();
        }
    }
}

static void start_probe_client(void) {
    if (s_probe_client) return;

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 10,
        .async = {
            .client_event_callback = usb_probe_client_callback,
            .callback_arg = NULL,
        },
    };
    esp_err_t err = usb_host_client_register(&client_config, &s_probe_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb probe client register failed: %s", esp_err_to_name(err));
        return;
    }

    if (xTaskCreatePinnedToCore(usb_probe_client_task, "usb_probe", 3072, NULL, 5, NULL, 0) != pdTRUE) {
        ESP_LOGE(TAG, "usb probe task create failed");
    }
}

static void usb_lib_task(void * arg) {
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
        xTaskNotifyGive((TaskHandle_t)arg);
        vTaskDelete(NULL);
        return;
    }
    usb_serial_jtag_ll_phy_enable_external(true);
    usb_serial_jtag_ll_phy_enable_pad(false);
    start_probe_client();
    xTaskNotifyGive((TaskHandle_t)arg);

    while (true) {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    }
}

static int ensure_queues(void) {
    s_key_queue = xQueueCreate(64, sizeof(int));
    if (!s_key_queue) {
        ESP_LOGE(TAG, "queue allocation failed");
        return 0;
    }
    return 1;
}

void esp32_usb_keyboard_start_host(void) {
    if (s_host_start_attempted) return;
    s_host_start_attempted = true;

    if (!ensure_queues()) return;

    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 4096, current, 5,
                                NULL, 0) != pdTRUE) {
        ESP_LOGE(TAG, "usb task create failed");
        return;
    }
    if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(1000)) == 0) {
        ESP_LOGE(TAG, "usb host task did not report startup");
    }
}

void esp32_usb_keyboard_init(void) {
    esp32_usb_keyboard_start_host();
}

int esp32_usb_keyboard_pop_key(void) {
    if (!s_key_queue) return -1;
    synth_repeat_due();
    int key = -1;
    return xQueueReceive(s_key_queue, &key, 0) == pdTRUE ? key : -1;
}

int esp32_usb_keyboard_input_available(void) {
    synth_repeat_due();
    return s_key_queue && uxQueueMessagesWaiting(s_key_queue) > 0;
}

void esp32_usb_keyboard_service(void) {
    synth_repeat_due();
}

void esp32_usb_keyboard_clear_repeat_state(void) {
    portENTER_CRITICAL(&s_repeat_mux);
    repeat_clear_locked();
    memset(s_prev_keys, 0, sizeof s_prev_keys);
    portEXIT_CRITICAL(&s_repeat_mux);
}

int esp32_usb_keyboard_has_keyboard(void) {
    return s_keyboard_attached != 0;
}
