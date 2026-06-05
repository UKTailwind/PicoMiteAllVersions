/*
 * BTConsole.c — BLE Nordic UART Service (NUS) console for PicoMiteBT.
 *
 * Advertises a NUS GATT service on the CYW43439's BLE radio. Any BLE
 * serial-terminal app (e.g. "Serial Bluetooth Terminal" on Android,
 * "Bluefruit Connect" on iOS, web-bluetooth in Chrome) connects and
 * presents the link as a virtual serial port.
 */

#ifdef PICOMITEBT

#include "BTConsole.h"

#include <string.h>
#include <stdio.h>

/* MMBasic preamble first — pulls in configuration.h which defines
   UNUSED as a pin-mode flag (1<<0). btstack's headers define UNUSED
   as a function-like macro UNUSED(x); the two clash. We don't use
   btstack's UNUSED macro in this file, so undef it before pulling
   in btstack to silence the warning. */
#include "configuration.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#undef UNUSED   /* drop the pin-flag macro before btstack redefines it */
#include "btstack.h"
#include "btstack_tlv_flash_bank.h"
#include "hal_flash_bank.h"
#include "ble/le_device_db_tlv.h"

/* The btstack TLV needs a flash-bank backing store. Rather than carve
   out a separate flash region (which would collide with MMBasic's
   claim on all flash above the firmware), we tunnel the TLV through
   the Option struct: Option.bt_tlv is 2 KB, split into two 1 KB
   "banks" for btstack's wear-leveling. The Option struct gets saved
   to flash via SaveOptions(), so the LTK rides along automatically. */
#define BT_TLV_BANK_SIZE 1024

static uint32_t bt_tlv_get_size(void *ctx) {
    (void)ctx;
    return BT_TLV_BANK_SIZE;
}
static uint32_t bt_tlv_get_alignment(void *ctx) {
    (void)ctx;
    return 4;
}
static void bt_tlv_erase(void *ctx, int bank) {
    (void)ctx;
    memset(&Option.bt_tlv[bank * BT_TLV_BANK_SIZE], 0xFF, BT_TLV_BANK_SIZE);
    SaveOptions();
}
static void bt_tlv_read(void *ctx, int bank, uint32_t offset,
                        uint8_t *buffer, uint32_t size) {
    (void)ctx;
    memcpy(buffer, &Option.bt_tlv[bank * BT_TLV_BANK_SIZE + offset], size);
}
static void bt_tlv_write(void *ctx, int bank, uint32_t offset,
                         const uint8_t *data, uint32_t size) {
    (void)ctx;
    /* btstack assumes flash, which can only flip bits 1->0 without
       erase. AND with the existing bytes to mirror that semantic. */
    uint8_t *dest = &Option.bt_tlv[bank * BT_TLV_BANK_SIZE + offset];
    for (uint32_t i = 0; i < size; i++) {
        dest[i] &= data[i];
    }
    SaveOptions();
}

static const hal_flash_bank_t bt_tlv_hal = {
    .get_size = bt_tlv_get_size,
    .get_alignment = bt_tlv_get_alignment,
    .erase = bt_tlv_erase,
    .read = bt_tlv_read,
    .write = bt_tlv_write,
};

#include "nus_gatt.h"

#define BT_TX_BUF_SIZE 1024
/* RX ring sized to absorb a worst-case BLE notification burst even
   when MMBasic's editor is slow (low CPU speeds). Was 256, which
   overflowed silently during AutoSave paste-ins below ~300 MHz —
   att_write_callback drops bytes when rx_full() and the host has no
   way to know (the ATT-write-response is sent automatically). */
#define BT_RX_BUF_SIZE 2048

#define NUS_RX_VALUE_HANDLE ATT_CHARACTERISTIC_6E400002_B5A3_F393_E0A9_E50E24DCCA9E_01_VALUE_HANDLE
#define NUS_TX_VALUE_HANDLE ATT_CHARACTERISTIC_6E400003_B5A3_F393_E0A9_E50E24DCCA9E_01_VALUE_HANDLE
#define NUS_TX_CLIENT_CONFIG_HANDLE \
    ATT_CHARACTERISTIC_6E400003_B5A3_F393_E0A9_E50E24DCCA9E_01_CLIENT_CONFIGURATION_HANDLE
static volatile uint16_t tx_head;
static volatile uint16_t tx_tail;
static uint8_t tx_buf[BT_TX_BUF_SIZE];

static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static uint8_t rx_buf[BT_RX_BUF_SIZE];

static hci_con_handle_t conn_handle = HCI_CON_HANDLE_INVALID;

/* notifications_enabled, can_send_now_requested and att_mtu are
   touched both from the main thread (bt_console_putc) and from the
   cyw43 alarm-IRQ context (packet_handler / drain_tx_notify under
   threadsafe_background). Without volatile the compiler can cache
   stale values, leaving bytes stranded in tx_buf with no pending
   CAN_SEND_NOW event — manifests as "stalls after first packet" in
   XMODEM where there's no further bt_console_putc activity to
   recover. */
static volatile bool notifications_enabled;
static volatile bool can_send_now_requested;
static volatile uint16_t att_mtu = 23;
static bool bt_startup_complete;

static btstack_packet_callback_registration_t hci_event_callback_registration;

static uint8_t adv_data[31];
static uint8_t adv_data_len;
static char local_name[20];

static inline uint16_t tx_count(void)
{
    uint16_t h = tx_head, t = tx_tail;
    return (h >= t) ? (h - t) : (uint16_t)(BT_TX_BUF_SIZE - t + h);
}

static inline bool tx_full(void)
{
    return ((uint16_t)(tx_head + 1) % BT_TX_BUF_SIZE) == tx_tail;
}

static inline bool rx_full(void)
{
    return ((uint16_t)(rx_head + 1) % BT_RX_BUF_SIZE) == rx_tail;
}

static inline uint16_t rx_count(void)
{
    uint16_t h = rx_head, t = rx_tail;
    return (h >= t) ? (h - t) : (uint16_t)(BT_RX_BUF_SIZE - t + h);
}

/* Drain TX ring through BLE notifications.

   Critical: stage the bytes by PEEKING into tx_buf without advancing
   tx_tail, attempt the notify, and only commit (advance tx_tail) on
   success. Otherwise a failed att_server_notify (controller busy,
   buffer full, etc.) silently loses the bytes — which is why XMODEM
   stalled after the first packet: the ACK byte made it into the ring,
   was peek-staged, the notify failed transiently, and the byte was
   discarded with no retry. */
static void drain_tx_notify(void)
{
    if (conn_handle == HCI_CON_HANDLE_INVALID)
        return;

    if (!notifications_enabled)
        return;

    uint16_t max_chunk = (att_mtu > 3) ? (att_mtu - 3) : 20;

    if (max_chunk > 244)
        max_chunk = 244;

    uint16_t avail = tx_count();

    if (avail == 0)
        return;

    if (avail > max_chunk)
        avail = max_chunk;

    static uint8_t stage[247];

    if (avail > sizeof(stage))
        avail = sizeof(stage);

    /* Peek into the ring without advancing tx_tail. */
    uint16_t tail_snapshot = tx_tail;
    for (uint16_t i = 0; i < avail; i++)
    {
        stage[i] = tx_buf[(tail_snapshot + i) % BT_TX_BUF_SIZE];
    }

    uint8_t rc = att_server_notify(conn_handle,
                                   NUS_TX_VALUE_HANDLE,
                                   stage,
                                   avail);

    if (rc != 0)
    {
        /* Notify failed (typically BTSTACK_ACL_BUFFERS_FULL). Leave
           bytes in the ring; re-request CAN_SEND_NOW so the next
           event tries again. */
        att_server_request_can_send_now_event(conn_handle);
        can_send_now_requested = true;
        return;
    }

    /* Commit: advance tail past the bytes we successfully handed off. */
    tx_tail = (uint16_t)(tail_snapshot + avail) % BT_TX_BUF_SIZE;

    if (tx_count() > 0)
    {
        can_send_now_requested = true;
        att_server_request_can_send_now_event(conn_handle);
    }
    else
    {
        can_send_now_requested = false;
    }
}

/* Main BTstack event handler */
static void packet_handler(uint8_t packet_type,
                           uint16_t channel,
                           uint8_t *packet,
                           uint16_t size)
{
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(packet))
    {

    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            gap_advertisements_enable(1);
        }
        break;

    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
            if (hci_subevent_le_connection_complete_get_status(packet) == 0) {
                conn_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                notifications_enabled = false;
                can_send_now_requested = false;
                att_mtu = 23;
                /* Ask the peer for a longer supervision timeout to
                   survive heavy bidirectional traffic (e.g. AutoSave
                   paste-ins). 0x0400 = 10 s vs. the typical default
                   of 720 ms. Interval kept short (15-30 ms) for low
                   typing latency. */
                gap_request_connection_parameter_update(
                    conn_handle, 0x000C, 0x0018, 0, 0x0400);
            }
            break;
#ifdef HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE
        case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE:
            if (hci_subevent_le_enhanced_connection_complete_get_status(packet) == 0) {
                conn_handle = hci_subevent_le_enhanced_connection_complete_get_connection_handle(packet);
                notifications_enabled = false;
                can_send_now_requested = false;
                att_mtu = 23;
                gap_request_connection_parameter_update(
                    conn_handle, 0x000C, 0x0018, 0, 0x0400);
            }
            break;
#endif
        default:
            break;
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE:
        conn_handle = HCI_CON_HANDLE_INVALID;
        notifications_enabled = false;
        can_send_now_requested = false;
        gap_advertisements_enable(1);
        break;

    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        att_mtu = att_event_mtu_exchange_complete_get_MTU(packet);
        break;

    case ATT_EVENT_CAN_SEND_NOW:
        drain_tx_notify();
        break;

    case SM_EVENT_JUST_WORKS_REQUEST:
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        break;

    default:
        break;
    }
}

/* No dynamic ATT reads */
static uint16_t att_read_callback(hci_con_handle_t connection_handle,
                                  uint16_t att_handle,
                                  uint16_t offset,
                                  uint8_t *buffer,
                                  uint16_t buffer_size)
{
    (void)connection_handle;
    (void)att_handle;
    (void)offset;
    (void)buffer;
    (void)buffer_size;

    return 0;
}

/* Handle CCCD writes and RX characteristic writes */
static int att_write_callback(hci_con_handle_t connection_handle,
                              uint16_t att_handle,
                              uint16_t transaction_mode,
                              uint16_t offset,
                              uint8_t *buffer,
                              uint16_t buffer_size)
{
    /* Only handle regular writes; prepare/execute/cancel come in with
       transaction_mode != NONE and att_handle == 0. */
    if (transaction_mode != ATT_TRANSACTION_MODE_NONE) {
        return 0;
    }
    (void)offset;

    if (att_handle == NUS_TX_CLIENT_CONFIG_HANDLE)
    {
        notifications_enabled =
            (little_endian_read_16(buffer, 0) ==
             GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);

        if (notifications_enabled && tx_count() > 0)
        {
            can_send_now_requested = true;
            att_server_request_can_send_now_event(connection_handle);
        }

        return 0;
    }

    if (att_handle == NUS_RX_VALUE_HANDLE)
    {
        /* Real flow control: if the whole incoming chunk won't fit
           in the RX ring, reject the write so the host retries.
           Previously we silently dropped overflow bytes — which broke
           AutoSave at low CPU speeds because MMBasic couldn't drain
           the ring fast enough and Tera Term never knew bytes were
           lost. INSUFFICIENT_RESOURCES is the standard ATT error for
           "ran out of buffer; try again later". */
        uint16_t free = (uint16_t)(BT_RX_BUF_SIZE - 1) -
                        ((uint16_t)(rx_head - rx_tail) %
                         (uint16_t)BT_RX_BUF_SIZE);
        if (buffer_size > free) {
            return ATT_ERROR_INSUFFICIENT_RESOURCES;
        }
        for (uint16_t i = 0; i < buffer_size; i++)
        {
            rx_buf[rx_head] = buffer[i];
            rx_head =
                (uint16_t)(rx_head + 1) % BT_RX_BUF_SIZE;
        }

        return 0;
    }

    return 0;
}

void bt_console_init(void)
{
    tx_head = tx_tail = 0;
    rx_head = rx_tail = 0;

    conn_handle = HCI_CON_HANDLE_INVALID;

    notifications_enabled = false;
    can_send_now_requested = false;

    att_mtu = 23;

    l2cap_init();

    /* Persistent bonding via the Option struct (see bt_tlv_hal above).
       The btstack TLV writes get redirected into Option.bt_tlv and
       persisted via SaveOptions() — keeps the LTK alive across reboots
       without colliding with MMBasic's flash claims. */
    {
        static btstack_tlv_flash_bank_t tlv_context;
        const btstack_tlv_t *tlv = btstack_tlv_flash_bank_init_instance(
            &tlv_context, &bt_tlv_hal, NULL);
        btstack_tlv_set_instance(tlv, &tlv_context);
        le_device_db_tlv_configure(tlv, &tlv_context);
    }

    sm_init();

    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);

    /* Legacy bonding (no Secure Connections). With SC enabled some
       Windows BLE stacks complete the pairing exchange at the protocol
       level (pairing_complete status=0x00) but then refuse to persist
       the bond — the device disappears from "Paired devices" right
       after. Legacy bonding is the universally-compatible path; we
       still get an encrypted link and a stored LTK via TLV. */
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING);

    att_server_init(profile_data,
                    att_read_callback,
                    att_write_callback);

    {
        pico_unique_board_id_t uid;

        pico_get_unique_board_id(&uid);

        snprintf(local_name,
                 sizeof(local_name),
                 "PicoMite-%02X%02X",
                 uid.id[6],
                 uid.id[7]);
    }

{
    uint8_t *p = adv_data;

    /* Flags */
    *p++ = 0x02;
    *p++ = 0x01;
    *p++ = 0x06;

    /* Shortened local name */
    uint8_t name_len =
        (uint8_t)strlen(local_name);

    if (name_len > 20)
        name_len = 20;

    *p++ = (uint8_t)(name_len + 1);
    *p++ = 0x08; /* Shortened Local Name */

    memcpy(p, local_name, name_len);
    p += name_len;

    adv_data_len = (uint8_t)(p - adv_data);
}
{
    uint8_t scan_data[31];
    uint8_t *p = scan_data;

    /* Nordic UART Service UUID */
    *p++ = 17;
    *p++ = 0x07;

    const uint8_t nus_uuid[] = {
        0x9E, 0xCA, 0xDC, 0x24,
        0x0E, 0xE5,
        0xA9, 0xE0,
        0x93, 0xF3,
        0xA3, 0xB5,
        0x01, 0x00, 0x40, 0x6E
    };

    memcpy(p, nus_uuid, sizeof(nus_uuid));
    p += sizeof(nus_uuid);

    gap_scan_response_set_data(
        (uint8_t)(p - scan_data),
        scan_data);
}
    {
        uint16_t adv_int_min = 0x00A0;
        uint16_t adv_int_max = 0x00A0;

        uint8_t adv_type = 0;

        bd_addr_t null_addr = {0};

        gap_advertisements_set_params(adv_int_min,
                                      adv_int_max,
                                      adv_type,
                                      0,
                                      null_addr,
                                      0x07,
                                      0);

        gap_advertisements_set_data(adv_data_len,
                                    adv_data);
    }

    hci_event_callback_registration.callback =
        &packet_handler;

    hci_add_event_handler(
        &hci_event_callback_registration);

    att_server_register_packet_handler(
        &packet_handler);

    /* Listen for SM events too so we can see pairing progress in the
       UART log (Just Works request, pairing complete/failed, etc.). */
    {
        static btstack_packet_callback_registration_t sm_cb;
        sm_cb.callback = &packet_handler;
        sm_add_event_handler(&sm_cb);
    }

    hci_power_control(HCI_POWER_ON);

    bt_startup_complete = true;
}

void bt_console_poll(void)
{
    if (!bt_startup_complete)
        return;

    cyw43_arch_poll();

    {
        static uint64_t last_heart_us = 0;
        static int led_state = 0;

        uint64_t now = time_us_64();

        /* OPTION HEARTBEAT OFF — same gate as BTKeyboard.c. Turn the
           LED off on transition then leave it alone. */
        if (Option.NoHeartbeat)
        {
            if (led_state != 0)
            {
                led_state = 0;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            }
            return;
        }

        /* LED cadence: 1000 ms before a console connects, 500 ms once
           connected — matches the bt_keyboard_poll() host cadence. */
        uint32_t interval =
            bt_console_connected()
                ? 500000
                : 1000000;

        if (now - last_heart_us >= interval)
        {
            last_heart_us = now;

            led_state ^= 1;

            cyw43_arch_gpio_put(
                CYW43_WL_GPIO_LED_PIN,
                led_state);
        }
    }
}

bool bt_console_connected(void)
{
    return conn_handle != HCI_CON_HANDLE_INVALID;
}
int bt_console_rx_available(void)
{
    return rx_count();
}

int bt_console_getc(void)
{
    if (rx_head == rx_tail)
        return -1;

    int c = rx_buf[rx_tail];

    rx_tail =
        (uint16_t)(rx_tail + 1) % BT_RX_BUF_SIZE;

    return c;
}

void bt_console_putc(uint8_t c)
{
    if (tx_full())
    {
        if (!bt_startup_complete ||
            !bt_console_connected())
        {
            tx_tail =
                (uint16_t)(tx_tail + 1) %
                BT_TX_BUF_SIZE;
        }
        else
        {
            while (tx_full())
            {
                cyw43_arch_poll();
            }
        }
    }

    tx_buf[tx_head] = c;

    tx_head =
        (uint16_t)(tx_head + 1) %
        BT_TX_BUF_SIZE;

    /* Always request — btstack dedups identical requests for the same
       connection internally. The previous "if (!can_send_now_requested)"
       guard was racy against drain_tx_notify resetting the flag in IRQ
       context: a stale "true" read by the main thread would skip the
       request, leaving bytes stranded in tx_buf with no pending event
       (XMODEM "stalls after first packet" failure mode). */
    if (bt_startup_complete && bt_console_connected())
    {
        att_server_request_can_send_now_event(conn_handle);
    }
}

void bt_console_flush(void)
{
    if (!bt_startup_complete) return;
    /* Belt-and-braces: if we have queued bytes but no CAN_SEND_NOW is
       pending, request one explicitly. Should already have been
       requested by bt_console_putc but the cost is negligible. */
    if (bt_console_connected() && tx_count() > 0) {
        att_server_request_can_send_now_event(conn_handle);
    }
    cyw43_arch_poll();
}

#endif /* PICOMITEBT */