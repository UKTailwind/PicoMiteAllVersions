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
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/rtc_cntl_struct.h"
#include "soc/usb_serial_jtag_struct.h"
#include "soc/usb_wrap_struct.h"

static const char *TAG = "esp32_usb_kbd";

typedef enum {
    USB_KBD_EVENT_HID,
} usb_kbd_event_group_t;

typedef struct {
    usb_kbd_event_group_t event_group;
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
    void *arg;
} usb_kbd_event_t;

static QueueHandle_t s_event_queue;
static QueueHandle_t s_key_queue;
static usb_host_client_handle_t s_probe_client;
static usb_device_handle_t s_raw_kbd_device;
static usb_transfer_t *s_raw_kbd_transfer;
static uint8_t s_raw_kbd_iface = 0xff;
static uint8_t s_raw_kbd_ep = 0;

typedef struct {
    volatile int queues_ready;
    volatile int host_start_attempted;
    volatile int host_task_created;
    volatile int host_started;
    volatile int host_err;
    volatile int host_notify_timeout;
    volatile int probe_client_registered;
    volatile int probe_client_task_created;
    volatile int probe_client_err;
    volatile unsigned probe_new_dev;
    volatile unsigned probe_dev_gone;
    volatile unsigned probe_other_events;
    volatile int probe_last_addr;
    volatile int probe_scan_err;
    volatile int probe_scan_count;
    volatile int probe_open_err;
    volatile int probe_desc_err;
    volatile int probe_config_err;
    volatile int probe_seen_addr;
    volatile int probe_seen_vid;
    volatile int probe_seen_pid;
    volatile int probe_seen_class;
    volatile int probe_seen_ifaces;
    volatile int probe_keyboard_iface;
    volatile int raw_claim_err;
    volatile int raw_transfer_alloc_err;
    volatile int raw_transfer_submit_err;
    volatile int raw_endpoint;
    volatile int raw_packet_size;
    volatile unsigned raw_reports;
    volatile unsigned raw_submit_errors;
    volatile unsigned usj_conf0;
    volatile unsigned wrap_conf;
    volatile int usj_phy_sel;
    volatile int usj_pad_enable;
    volatile int wrap_phy_sel;
    volatile int wrap_pad_enable;
    volatile int wrap_dp_pd;
    volatile int wrap_dm_pd;
    volatile int rtc_sw_hw_phy_sel;
    volatile int rtc_sw_phy_sel;
    volatile int hid_start_attempted;
    volatile int hid_started;
    volatile int hid_err;
    volatile int hid_event_task_created;
    volatile int hid_event_task_err;
    volatile unsigned host_heap_internal_free;
    volatile unsigned host_heap_internal_largest;
    volatile unsigned host_heap_dma_free;
    volatile unsigned host_heap_dma_largest;
    volatile unsigned hid_heap_internal_free;
    volatile unsigned hid_heap_internal_largest;
    volatile unsigned hid_heap_dma_free;
    volatile unsigned hid_heap_dma_largest;
    volatile int keyboard_attached;
    volatile unsigned driver_connected_events;
    volatile unsigned keyboard_connected;
    volatile unsigned non_keyboard_connected;
    volatile unsigned disconnected;
    volatile unsigned transfer_errors;
    volatile unsigned reports;
    volatile unsigned short_reports;
    volatile unsigned raw_report_errors;
    volatile unsigned queued_keys;
    volatile unsigned queue_drops;
    volatile unsigned param_errors;
    volatile unsigned open_errors;
    volatile unsigned protocol_errors;
    volatile unsigned idle_errors;
    volatile unsigned start_errors;
    volatile int last_addr;
    volatile int last_iface;
    volatile int last_subclass;
    volatile int last_proto;
    volatile int last_report_len;
    volatile int last_key;
} usb_keyboard_diag_t;

static usb_keyboard_diag_t s_diag = {
    .host_err = ESP_ERR_INVALID_STATE,
    .probe_client_err = ESP_ERR_INVALID_STATE,
    .probe_last_addr = -1,
    .probe_scan_err = ESP_ERR_INVALID_STATE,
    .probe_open_err = ESP_ERR_INVALID_STATE,
    .probe_desc_err = ESP_ERR_INVALID_STATE,
    .probe_config_err = ESP_ERR_INVALID_STATE,
    .probe_seen_addr = -1,
    .probe_seen_vid = -1,
    .probe_seen_pid = -1,
    .probe_seen_class = -1,
    .probe_seen_ifaces = -1,
    .probe_keyboard_iface = -1,
    .raw_claim_err = ESP_ERR_INVALID_STATE,
    .raw_transfer_alloc_err = ESP_ERR_INVALID_STATE,
    .raw_transfer_submit_err = ESP_ERR_INVALID_STATE,
    .raw_endpoint = -1,
    .raw_packet_size = -1,
    .hid_err = ESP_ERR_INVALID_STATE,
    .hid_event_task_err = ESP_ERR_INVALID_STATE,
    .last_addr = -1,
    .last_iface = -1,
    .last_subclass = -1,
    .last_proto = -1,
    .last_report_len = -1,
    .last_key = -1,
};

static unsigned heap_free(uint32_t caps) {
    return (unsigned)heap_caps_get_free_size(caps);
}

static unsigned heap_largest(uint32_t caps) {
    return (unsigned)heap_caps_get_largest_free_block(caps);
}

static void capture_host_heap(void) {
    s_diag.host_heap_internal_free = heap_free(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_diag.host_heap_internal_largest = heap_largest(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_diag.host_heap_dma_free = heap_free(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_diag.host_heap_dma_largest = heap_largest(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

static void capture_hid_heap(void) {
    s_diag.hid_heap_internal_free = heap_free(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_diag.hid_heap_internal_largest = heap_largest(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_diag.hid_heap_dma_free = heap_free(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_diag.hid_heap_dma_largest = heap_largest(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

static void capture_usb_pad_state(void) {
    s_diag.usj_conf0 = USB_SERIAL_JTAG.conf0.val;
    s_diag.wrap_conf = USB_WRAP.otg_conf.val;
    s_diag.usj_phy_sel = USB_SERIAL_JTAG.conf0.phy_sel;
    s_diag.usj_pad_enable = USB_SERIAL_JTAG.conf0.usb_pad_enable;
    s_diag.wrap_phy_sel = USB_WRAP.otg_conf.phy_sel;
    s_diag.wrap_pad_enable = USB_WRAP.otg_conf.pad_enable;
    s_diag.wrap_dp_pd = USB_WRAP.otg_conf.dp_pulldown;
    s_diag.wrap_dm_pd = USB_WRAP.otg_conf.dm_pulldown;
    s_diag.rtc_sw_hw_phy_sel = RTCCNTL.usb_conf.sw_hw_usb_phy_sel;
    s_diag.rtc_sw_phy_sel = RTCCNTL.usb_conf.sw_usb_phy_sel;
}

static const uint8_t keycode2ascii[57][2] = {
    {0, 0},     {0, 0},     {0, 0},     {0, 0},
    {'a', 'A'}, {'b', 'B'}, {'c', 'C'}, {'d', 'D'},
    {'e', 'E'}, {'f', 'F'}, {'g', 'G'}, {'h', 'H'},
    {'i', 'I'}, {'j', 'J'}, {'k', 'K'}, {'l', 'L'},
    {'m', 'M'}, {'n', 'N'}, {'o', 'O'}, {'p', 'P'},
    {'q', 'Q'}, {'r', 'R'}, {'s', 'S'}, {'t', 'T'},
    {'u', 'U'}, {'v', 'V'}, {'w', 'W'}, {'x', 'X'},
    {'y', 'Y'}, {'z', 'Z'}, {'1', '!'}, {'2', '@'},
    {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'},
    {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'},
    {ENTER, ENTER}, {ESC, ESC}, {BKSP, BKSP}, {'\t', '\t'},
    {' ', ' '}, {'-', '_'}, {'=', '+'}, {'[', '{'},
    {']', '}'}, {'\\', '|'}, {'\\', '|'}, {';', ':'},
    {'\'', '"'}, {'`', '~'}, {',', '<'}, {'.', '>'},
    {'/', '?'},
};

static void queue_key(int key) {
    if (!s_key_queue || key == 0) return;
    if (xQueueSend(s_key_queue, &key, 0) == pdTRUE) {
        s_diag.queued_keys++;
        s_diag.last_key = key;
    } else {
        s_diag.queue_drops++;
    }
}

static bool modifier_shift(uint8_t modifier) {
    return (modifier & (HID_LEFT_SHIFT | HID_RIGHT_SHIFT)) != 0;
}

static bool modifier_ctrl(uint8_t modifier) {
    return (modifier & (HID_LEFT_CONTROL | HID_RIGHT_CONTROL)) != 0;
}

static int translate_key(uint8_t modifier, uint8_t key_code) {
    if (key_code >= HID_KEY_A && key_code <= HID_KEY_Z && modifier_ctrl(modifier)) {
        return (key_code - HID_KEY_A) + 1;
    }
    if (key_code < (sizeof keycode2ascii / sizeof keycode2ascii[0])) {
        int shifted = modifier_shift(modifier) ? 1 : 0;
        return keycode2ascii[key_code][shifted];
    }

    switch (key_code) {
    case HID_KEY_F1: return F1;
    case HID_KEY_F2: return F2;
    case HID_KEY_F3: return F3;
    case HID_KEY_F4: return F4;
    case HID_KEY_F5: return F5;
    case HID_KEY_F6: return F6;
    case HID_KEY_F7: return F7;
    case HID_KEY_F8: return F8;
    case HID_KEY_F9: return F9;
    case HID_KEY_F10: return F10;
    case HID_KEY_F11: return F11;
    case HID_KEY_F12: return F12;
    case HID_KEY_INSERT: return INSERT;
    case HID_KEY_HOME: return HOME;
    case HID_KEY_PAGEUP: return PUP;
    case HID_KEY_DELETE: return DEL;
    case HID_KEY_END: return END;
    case HID_KEY_PAGEDOWN: return PDOWN;
    case HID_KEY_RIGHT: return RIGHT;
    case HID_KEY_LEFT: return LEFT;
    case HID_KEY_DOWN: return DOWN;
    case HID_KEY_UP: return UP;
    default: return 0;
    }
}

static bool key_found(const uint8_t *src, uint8_t key, unsigned len) {
    for (unsigned i = 0; i < len; i++) {
        if (src[i] == key) return true;
    }
    return false;
}

static void keyboard_report(const uint8_t *data, int length) {
    static uint8_t prev_keys[HID_KEYBOARD_KEY_MAX];

    s_diag.last_report_len = length;
    if (length < (int)sizeof(hid_keyboard_input_report_boot_t)) {
        s_diag.short_reports++;
        return;
    }
    s_diag.reports++;
    hid_keyboard_input_report_boot_t *report = (hid_keyboard_input_report_boot_t *)data;

    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        uint8_t key = report->key[i];
        if (key > HID_KEY_ERROR_UNDEFINED &&
            !key_found(prev_keys, key, HID_KEYBOARD_KEY_MAX)) {
            queue_key(translate_key(report->modifier.val, key));
        }
    }

    memcpy(prev_keys, report->key, HID_KEYBOARD_KEY_MAX);
}

static void hid_interface_callback(hid_host_device_handle_t handle,
                                   hid_host_interface_event_t event,
                                   void *arg) {
    (void)arg;
    uint8_t data[64];
    size_t data_length = 0;
    hid_host_dev_params_t params;

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        if (hid_host_device_get_params(handle, &params) != ESP_OK) {
            s_diag.param_errors++;
            return;
        }
        if (hid_host_device_get_raw_input_report_data(handle, data, sizeof data,
                                                      &data_length) != ESP_OK) {
            s_diag.raw_report_errors++;
            return;
        }
        if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE &&
            params.proto == HID_PROTOCOL_KEYBOARD) {
            keyboard_report(data, (int)data_length);
        }
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        s_diag.transfer_errors++;
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        s_diag.disconnected++;
        s_diag.keyboard_attached = 0;
        hid_host_device_close(handle);
        break;
    default:
        break;
    }
}

static void handle_hid_event(hid_host_device_handle_t handle,
                             hid_host_driver_event_t event,
                             void *arg) {
    (void)arg;
    hid_host_dev_params_t params;
    s_diag.driver_connected_events++;
    if (hid_host_device_get_params(handle, &params) != ESP_OK) {
        s_diag.param_errors++;
        return;
    }
    s_diag.last_addr = params.addr;
    s_diag.last_iface = params.iface_num;
    s_diag.last_subclass = params.sub_class;
    s_diag.last_proto = params.proto;

    if (event != HID_HOST_DRIVER_EVENT_CONNECTED) return;
    if (params.proto != HID_PROTOCOL_KEYBOARD) {
        s_diag.non_keyboard_connected++;
        return;
    }

    const hid_host_device_config_t dev_config = {
        .callback = hid_interface_callback,
        .callback_arg = NULL,
    };
    if (hid_host_device_open(handle, &dev_config) != ESP_OK) {
        s_diag.open_errors++;
        return;
    }
    if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
        if (hid_class_request_set_protocol(handle, HID_REPORT_PROTOCOL_BOOT) != ESP_OK) {
            s_diag.protocol_errors++;
        }
        if (hid_class_request_set_idle(handle, 0, 0) != ESP_OK) {
            s_diag.idle_errors++;
        }
    }
    if (hid_host_device_start(handle) != ESP_OK) {
        s_diag.start_errors++;
        return;
    }
    s_diag.keyboard_connected++;
    s_diag.keyboard_attached = 1;
}

static void hid_device_callback(hid_host_device_handle_t handle,
                                hid_host_driver_event_t event,
                                void *arg) {
    if (!s_event_queue) return;
    usb_kbd_event_t evt = {
        .event_group = USB_KBD_EVENT_HID,
        .handle = handle,
        .event = event,
        .arg = arg,
    };
    xQueueSend(s_event_queue, &evt, 0);
}

static void usb_probe_client_callback(const usb_host_client_event_msg_t *event_msg,
                                      void *arg) {
    (void)arg;
    if (!event_msg) return;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        s_diag.probe_new_dev++;
        s_diag.probe_last_addr = event_msg->new_dev.address;
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        s_diag.probe_dev_gone++;
        if (event_msg->dev_gone.dev_hdl == s_raw_kbd_device) {
            s_diag.keyboard_attached = 0;
            s_raw_kbd_device = NULL;
            s_raw_kbd_transfer = NULL;
            s_raw_kbd_iface = 0xff;
            s_raw_kbd_ep = 0;
        }
        break;
    default:
        s_diag.probe_other_events++;
        break;
    }
}

static void raw_keyboard_transfer_callback(usb_transfer_t *transfer) {
    if (!transfer) return;
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        s_diag.raw_reports++;
        keyboard_report(transfer->data_buffer, (int)transfer->actual_num_bytes);
    } else {
        s_diag.transfer_errors++;
    }

    if (s_raw_kbd_device && transfer == s_raw_kbd_transfer) {
        esp_err_t err = usb_host_transfer_submit(transfer);
        s_diag.raw_transfer_submit_err = err;
        if (err != ESP_OK) {
            s_diag.raw_submit_errors++;
        }
    }
}

static bool raw_keyboard_attach(usb_device_handle_t handle,
                                const usb_config_desc_t *config_desc,
                                uint8_t iface_num,
                                uint8_t alt_setting) {
    if (s_raw_kbd_device || !handle || !config_desc) return false;

    uint8_t endpoint = 0;
    uint16_t packet_size = 0;
    const uint8_t *p = (const uint8_t *)config_desc;
    bool in_target_iface = false;
    for (int offset = 0; offset < config_desc->wTotalLength;) {
        uint8_t length = p[offset];
        uint8_t type = p[offset + 1];
        if (length < 2 || offset + length > config_desc->wTotalLength) break;
        if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && length >= sizeof(usb_intf_desc_t)) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)(p + offset);
            in_target_iface = intf->bInterfaceNumber == iface_num &&
                              intf->bAlternateSetting == alt_setting;
        } else if (in_target_iface &&
                   type == USB_B_DESCRIPTOR_TYPE_ENDPOINT &&
                   length >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)(p + offset);
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
        s_diag.raw_transfer_alloc_err = ESP_ERR_NOT_FOUND;
        return false;
    }

    esp_err_t err = usb_host_interface_claim(s_probe_client, handle, iface_num, alt_setting);
    s_diag.raw_claim_err = err;
    if (err != ESP_OK) return false;

    usb_transfer_t *transfer = NULL;
    err = usb_host_transfer_alloc(packet_size, 0, &transfer);
    s_diag.raw_transfer_alloc_err = err;
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
    s_raw_kbd_iface = iface_num;
    s_raw_kbd_ep = endpoint;
    s_diag.raw_endpoint = endpoint;
    s_diag.raw_packet_size = packet_size;
    s_diag.keyboard_attached = 1;
    s_diag.keyboard_connected++;

    err = usb_host_transfer_submit(transfer);
    s_diag.raw_transfer_submit_err = err;
    if (err != ESP_OK) {
        s_diag.raw_submit_errors++;
        s_diag.keyboard_attached = 0;
        s_raw_kbd_device = NULL;
        s_raw_kbd_transfer = NULL;
        s_raw_kbd_iface = 0xff;
        s_raw_kbd_ep = 0;
        usb_host_transfer_free(transfer);
        usb_host_interface_release(s_probe_client, handle, iface_num);
        return false;
    }
    return true;
}

static void probe_config_descriptor(const usb_config_desc_t *config_desc) {
    if (!config_desc) return;
    s_diag.probe_seen_ifaces = config_desc->bNumInterfaces;
    s_diag.probe_keyboard_iface = -1;

    const uint8_t *p = (const uint8_t *)config_desc;
    uint8_t kbd_alt = 0;
    for (int offset = 0; offset < config_desc->wTotalLength;) {
        uint8_t length = p[offset];
        uint8_t type = p[offset + 1];
        if (length < 2 || offset + length > config_desc->wTotalLength) break;
        if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && length >= sizeof(usb_intf_desc_t)) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)(p + offset);
            if (intf->bInterfaceClass == USB_CLASS_HID &&
                intf->bInterfaceProtocol == HID_PROTOCOL_KEYBOARD) {
                s_diag.probe_keyboard_iface = intf->bInterfaceNumber;
                kbd_alt = intf->bAlternateSetting;
                break;
            }
        }
        offset += length;
    }
    (void)kbd_alt;
}

static void probe_scan_devices(void) {
    if (!s_probe_client) return;

    uint8_t addresses[8] = {0};
    int address_count = 0;
    esp_err_t err = usb_host_device_addr_list_fill((int)sizeof(addresses),
                                                   addresses, &address_count);
    s_diag.probe_scan_err = err;
    if (err != ESP_OK) return;
    s_diag.probe_scan_count = address_count;

    for (int i = 0; i < address_count; i++) {
        uint8_t address = addresses[i];
        if (address == 0) continue;

        usb_device_handle_t handle = NULL;
        err = usb_host_device_open(s_probe_client, address, &handle);
        s_diag.probe_open_err = err;
        if (err != ESP_OK || !handle) continue;

        s_diag.probe_seen_addr = address;
        const usb_device_desc_t *dev_desc = NULL;
        err = usb_host_get_device_descriptor(handle, &dev_desc);
        s_diag.probe_desc_err = err;
        if (err == ESP_OK && dev_desc) {
            s_diag.probe_seen_vid = dev_desc->idVendor;
            s_diag.probe_seen_pid = dev_desc->idProduct;
            s_diag.probe_seen_class = dev_desc->bDeviceClass;
        }

        const usb_config_desc_t *config_desc = NULL;
        err = usb_host_get_active_config_descriptor(handle, &config_desc);
        s_diag.probe_config_err = err;
        bool keep_open = false;
        if (err == ESP_OK && config_desc) {
            probe_config_descriptor(config_desc);
            if (s_diag.probe_keyboard_iface >= 0 && !s_raw_kbd_device) {
                keep_open = raw_keyboard_attach(handle, config_desc,
                                                (uint8_t)s_diag.probe_keyboard_iface, 0);
            }
        }

        if (!keep_open) usb_host_device_close(s_probe_client, handle);
        break;
    }
}

static void usb_probe_client_task(void *arg) {
    (void)arg;
    TickType_t last_scan = 0;
    while (true) {
        if (!s_probe_client) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        esp_err_t err = usb_host_client_handle_events(s_probe_client, pdMS_TO_TICKS(50));
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            s_diag.probe_client_err = err;
        }
        TickType_t now = xTaskGetTickCount();
        if (now - last_scan >= pdMS_TO_TICKS(500)) {
            last_scan = now;
            probe_scan_devices();
        }
    }
}

static void start_probe_client(void) {
    if (s_diag.probe_client_registered || s_probe_client) return;

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
        s_diag.probe_client_err = err;
        ESP_LOGE(TAG, "usb probe client register failed: %s", esp_err_to_name(err));
        return;
    }
    s_diag.probe_client_err = ESP_OK;
    s_diag.probe_client_registered = 1;

    if (xTaskCreatePinnedToCore(usb_probe_client_task, "usb_probe", 3072, NULL, 5,
                                NULL, 0) == pdTRUE) {
        s_diag.probe_client_task_created = 1;
    } else {
        s_diag.probe_client_err = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "usb probe task create failed");
    }
}

static void usb_lib_task(void *arg) {
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    capture_host_heap();
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        s_diag.host_err = err;
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
        xTaskNotifyGive((TaskHandle_t)arg);
        vTaskDelete(NULL);
        return;
    }
    usb_serial_jtag_ll_phy_enable_external(true);
    usb_serial_jtag_ll_phy_enable_pad(false);
    capture_usb_pad_state();
    s_diag.host_err = ESP_OK;
    start_probe_client();
    s_diag.host_started = 1;
    xTaskNotifyGive((TaskHandle_t)arg);

    while (true) {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    }
}

static void hid_event_task(void *arg) {
    (void)arg;
    usb_kbd_event_t evt;
    while (true) {
        if (xQueueReceive(s_event_queue, &evt, portMAX_DELAY)) {
            handle_hid_event(evt.handle, evt.event, evt.arg);
        }
    }
}

static int ensure_queues(void) {
    s_key_queue = xQueueCreate(64, sizeof(int));
    s_event_queue = xQueueCreate(10, sizeof(usb_kbd_event_t));
    if (!s_key_queue || !s_event_queue) {
        s_diag.host_err = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "queue allocation failed");
        return 0;
    }
    s_diag.queues_ready = 1;
    return 1;
}

void esp32_usb_keyboard_start_host(void) {
    if (s_diag.host_start_attempted) return;
    s_diag.host_start_attempted = 1;

    if (!ensure_queues()) return;

    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 4096, current, 5,
                                NULL, 0) != pdTRUE) {
        s_diag.host_err = ESP_FAIL;
        ESP_LOGE(TAG, "usb task create failed");
        return;
    }
    s_diag.host_task_created = 1;
    if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(1000)) == 0) {
        s_diag.host_notify_timeout = 1;
        ESP_LOGE(TAG, "usb host task did not report startup");
    }
}

void esp32_usb_keyboard_start_hid(void) {
    if (s_diag.hid_start_attempted) return;
    s_diag.hid_start_attempted = 1;

    if (!s_diag.host_started) {
        s_diag.hid_err = ESP_ERR_INVALID_STATE;
        return;
    }

    s_diag.hid_err = ESP_ERR_NOT_SUPPORTED;
    s_diag.hid_event_task_err = ESP_ERR_NOT_SUPPORTED;
    return;

    const hid_host_driver_config_t hid_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_device_callback,
        .callback_arg = NULL,
    };
    capture_hid_heap();
    esp_err_t err = hid_host_install(&hid_config);
    if (err != ESP_OK) {
        s_diag.hid_err = err;
        ESP_LOGE(TAG, "hid_host_install failed: %s", esp_err_to_name(err));
        return;
    }
    s_diag.hid_err = ESP_OK;
    s_diag.hid_started = 1;

    if (xTaskCreatePinnedToCore(hid_event_task, "hid_events", 4096, NULL, 4,
                                NULL, 0) == pdTRUE) {
        s_diag.hid_event_task_err = ESP_OK;
        s_diag.hid_event_task_created = 1;
    } else {
        s_diag.hid_event_task_err = ESP_FAIL;
        ESP_LOGE(TAG, "hid event task create failed");
    }
}

void esp32_usb_keyboard_init(void) {
    esp32_usb_keyboard_start_host();
    esp32_usb_keyboard_start_hid();
}

int esp32_usb_keyboard_pop_key(void) {
    if (!s_key_queue) return -1;
    int key = -1;
    return xQueueReceive(s_key_queue, &key, 0) == pdTRUE ? key : -1;
}

int esp32_usb_keyboard_input_available(void) {
    return s_key_queue && uxQueueMessagesWaiting(s_key_queue) > 0;
}

int esp32_usb_keyboard_has_keyboard(void) {
    return s_diag.keyboard_attached != 0;
}

void esp32_usb_keyboard_print_status(void) {
    char line[128];
    capture_usb_pad_state();
    MMPrintString("USB KEYBOARD MODE\r\n");
    snprintf(line, sizeof line, "USB host: %s err=%d timeout=%d\r\n",
             s_diag.host_started ? "started" : "not started",
             s_diag.host_err, s_diag.host_notify_timeout);
    MMPrintString(line);
    snprintf(line, sizeof line,
             "USB probe: reg=%d task=%d err=%d new=%u gone=%u addr=%d\r\n",
             s_diag.probe_client_registered, s_diag.probe_client_task_created,
             s_diag.probe_client_err, s_diag.probe_new_dev,
             s_diag.probe_dev_gone, s_diag.probe_last_addr);
    MMPrintString(line);
    snprintf(line, sizeof line,
             "USB scan: err=%d count=%d open/desc/cfg=%d/%d/%d\r\n",
             s_diag.probe_scan_err, s_diag.probe_scan_count,
             s_diag.probe_open_err, s_diag.probe_desc_err,
             s_diag.probe_config_err);
    MMPrintString(line);
    snprintf(line, sizeof line,
             "USB seen: addr=%d vidpid=%04x:%04x class=%d if=%d kbdif=%d\r\n",
             s_diag.probe_seen_addr, s_diag.probe_seen_vid & 0xffff,
             s_diag.probe_seen_pid & 0xffff, s_diag.probe_seen_class,
             s_diag.probe_seen_ifaces, s_diag.probe_keyboard_iface);
    MMPrintString(line);
    snprintf(line, sizeof line,
             "USB rawkbd: claim=%d alloc=%d sub=%d ep=%02x pkt=%d reps=%u\r\n",
             s_diag.raw_claim_err, s_diag.raw_transfer_alloc_err,
             s_diag.raw_transfer_submit_err, s_diag.raw_endpoint & 0xff,
             s_diag.raw_packet_size, s_diag.raw_reports);
    MMPrintString(line);
    snprintf(line, sizeof line,
             "USB mux: usj phy/pad=%d/%d wrap phy/pad=%d/%d rtc=%d/%d\r\n",
             s_diag.usj_phy_sel, s_diag.usj_pad_enable,
             s_diag.wrap_phy_sel, s_diag.wrap_pad_enable,
             s_diag.rtc_sw_hw_phy_sel, s_diag.rtc_sw_phy_sel);
    MMPrintString(line);
    snprintf(line, sizeof line,
             "USB pulls: wrap dpdm-pd=%d/%d usj=0x%08x wrap=0x%08x\r\n",
             s_diag.wrap_dp_pd, s_diag.wrap_dm_pd,
             s_diag.usj_conf0, s_diag.wrap_conf);
    MMPrintString(line);
    snprintf(line, sizeof line, "Host heap: int=%u/%u dma=%u/%u\r\n",
             s_diag.host_heap_internal_free, s_diag.host_heap_internal_largest,
             s_diag.host_heap_dma_free, s_diag.host_heap_dma_largest);
    MMPrintString(line);
    if (s_diag.hid_err == ESP_ERR_NOT_SUPPORTED) {
        snprintf(line, sizeof line, "HID host: disabled; using raw USB HID\r\n");
    } else {
        snprintf(line, sizeof line, "HID host: %s err=%d task=%d\r\n",
                 s_diag.hid_started ? "started" : "not started",
                 s_diag.hid_err, s_diag.hid_event_task_err);
    }
    MMPrintString(line);
    snprintf(line, sizeof line, "HID heap: int=%u/%u dma=%u/%u\r\n",
             s_diag.hid_heap_internal_free, s_diag.hid_heap_internal_largest,
             s_diag.hid_heap_dma_free, s_diag.hid_heap_dma_largest);
    MMPrintString(line);
    snprintf(line, sizeof line,
             "HID dev: attached=%d kbd=%u other=%u disc=%u\r\n",
             s_diag.keyboard_attached, s_diag.keyboard_connected,
             s_diag.non_keyboard_connected, s_diag.disconnected);
    MMPrintString(line);
    snprintf(line, sizeof line,
             "Last HID: addr=%d iface=%d subclass=%d proto=%d\r\n",
             s_diag.last_addr, s_diag.last_iface, s_diag.last_subclass,
             s_diag.last_proto);
    MMPrintString(line);
    snprintf(line, sizeof line,
             "Reports=%u keys=%u drops=%u xfererr=%u rawerr=%u\r\n",
             s_diag.reports, s_diag.queued_keys, s_diag.queue_drops,
             s_diag.transfer_errors, s_diag.raw_report_errors);
    MMPrintString(line);
}
