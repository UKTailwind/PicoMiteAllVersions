/*
 * BTKeyboard.c — BLE HID host (HOG) keyboard input for PicoMiteBTH.
 *
 * Step 2: scan for BLE devices, log them to the USB CDC console, and
 * once we see an advertisement from a hardcoded target MAC, connect
 * to it, let SM pair (Just Works, no I/O), then use hids_client to
 * subscribe to keyboard input reports. Each received report is
 * hexdumped to the console and counted — no keymapping into the
 * BASIC input ring yet (that's step 3).
 *
 * No bonding persistence yet: link keys live in RAM only, so the
 * keyboard re-pairs from scratch on every boot. Step 4 will move the
 * btstack TLV onto Option.bt_tlv exactly like BTConsole.c does.
 *
 * Edit BTH_TARGET_ADDR_STRING / BTH_TARGET_ADDR_TYPE below to point
 * at a real keyboard. Until then, the build advertises forever — the
 * scan log will show observed devices so you can identify the right
 * MAC + address type.
 */

#if defined(PICOMITEBTH) || defined(PICOMITEHDMIBTH)

#include "BTKeyboard.h"

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "configuration.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "pico/cyw43_arch.h"
#undef UNUSED
#include "btstack.h"
#include "ble/gatt-service/hids_client.h"
#include "ble/att_db_util.h"
#include "btstack_tlv_flash_bank.h"
#include "hal_flash_bank.h"
#include "ble/le_device_db_tlv.h"
#include "hci_dump_embedded_stdout.h"

/* Shared HID keyboard keymap + 8-byte boot-keyboard report decoder
   (international layouts, Caps/Num/Scroll lock, KeyDown[] for the
   KEYDOWN() BASIC function). Same path the USB-HID-host build uses. */
#include "KeyboardMap.h"
/* nunstruct[] / nunfoundc[] -- the BASIC DEVICE(MOUSE|GAMEPAD) reader
   arrays. process_mouse_input (KeyboardMap.c) writes mouse data into
   slot 2; bth_gamepad_publish (below) writes gamepad data into slot 3. */
#include "I2C.h"

/* ============================================================================
 * Target keyboard — placeholder MAC + address type. Edit before flashing.
 * Use the [BT] scan log lines that BTKeyboard prints over USB CDC to find
 * your keyboard's real address.
 *   BD_ADDR_TYPE_LE_PUBLIC = 0  (typical for older / cheap kbds)
 *   BD_ADDR_TYPE_LE_RANDOM = 1  (Apple, most Logitech, most modern kbds)
 * ============================================================================
 */
#define BTH_TARGET_ADDR_STRING "30:25:94:33:46:35"
// #define BTH_TARGET_ADDR_STRING  "C0:1C:94:33:46:35"
#define BTH_TARGET_ADDR_TYPE BD_ADDR_TYPE_LE_PUBLIC

/* When BTH_MATCH_HID_UUID is defined, scan-matching is relaxed:
   any advertiser that includes the HID Service UUID (0x1812) in
   its advertising data is treated as a candidate. The hardcoded
   MAC is still checked first; UUID match acts as a fallback for
   testing when the target MAC is unknown or rotates as an RPA.
   Comment out to require strict MAC equality. */
#define BTH_MATCH_HID_UUID

/* Drop advertising reports below this RSSI (dBm). Keeping the
   cutoff near -70 means devices in the same room (typically
   -45..-65) get through but neighbours' devices (often -85..-100)
   are silently discarded — both from the scan log and from the
   match logic. Set lower (e.g. -90) if your target is further
   away or behind a wall. */
#define BTH_SCAN_RSSI_MIN (-70)

/* Master switch for the verbose [BT] diagnostic log — the HCI / scan /
   SM / connection trace produced by bth_log(), plus the raw-notification
   hex dump. Off by default: with it undefined the only Bluetooth console
   output is the "Bluetooth Keyboard Connected/Disconnected" messages.
   Define it to trace pairing / connection problems. */
// #define BTH_DEBUG_LOG

/* Verbose scan diagnostics. When defined, every distinct advertiser
   seen while scanning is logged once — address, address-type, RSSI,
   advertising event-type, whether it carries the HID UUID (0x1812),
   and the local name. Logged BEFORE the RSSI gate so even weak/distant
   devices show, and deduped per scan session so the console stays
   readable. Use it to confirm a new keyboard is actually broadcasting
   and to read the address / address-type you need for
   BTH_TARGET_ADDR_STRING / BTH_TARGET_ADDR_TYPE. Comment out for normal
   operation. (Its log lines go through bth_log, so BTH_DEBUG_LOG must
   also be defined for them to appear.) */
// #define BTH_SCAN_DEBUG

/* hid_descriptor_storage size — same as the SDK hid_host_demo. Large
   enough for typical keyboard HID descriptors (~150 bytes); 300 leaves
   slack for combination kbd+mouse devices. */
#define HID_DESCRIPTOR_STORAGE_SIZE 300

/* ============================================================================
 * Persistent bond storage — btstack TLV tunnelled through Option.bt_tlv.
 *
 * Same scheme PICOMITEBT uses (see BTConsole.c): two virtual flash banks of
 * 1 KB each backed by the Option struct. SaveOptions() rewrites the 4 KB
 * options sector every time btstack updates the TLV, so LTKs / IRKs survive
 * power cycles and you don't have to forget+repair the host on every boot.
 * ============================================================================
 */
#define BT_TLV_BANK_SIZE 1024
/*
    Bluetooth LE HID Report Descriptor Parser
    -----------------------------------------

    Parses a HID report descriptor stored in:

        uint8_t hid_report_descriptor[];

    Features:
    - Short item parsing
    - Main / Global / Local item decoding
    - Collection nesting tracking
    - Usage page decoding
    - Report ID tracking
    - Handles signed values
    - Safe bounds checking
    - No dynamic allocation
    - Re-entrant

    Suitable for embedded BLE HID stacks.

    Example:
        parse_hid_report_descriptor(
            hid_report_descriptor,
            sizeof(hid_report_descriptor)
        );
*/

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
static uint32_t extract_value(const uint8_t *data, uint8_t size)
{
    uint32_t v = 0;

    switch (size)
    {
    case 4:
        v |= ((uint32_t)data[3] << 24);

    case 3:
        v |= ((uint32_t)data[2] << 16);

    case 2:
        v |= ((uint32_t)data[1] << 8);

    case 1:
        v |= ((uint32_t)data[0]);
        break;

    default:
        break;
    }

    return v;
}
static int32_t sign_extend(uint32_t value, uint8_t size_bytes)
{
    switch (size_bytes)
    {
    case 1:
        return (int8_t)value;

    case 2:
        return (int16_t)value;

    case 4:
        return (int32_t)value;

    default:
        return (int32_t)value;
    }
}
/* ----------------------------------------------------------------------------
 * Verbose HID-report-descriptor debug walker. Not compiled in normal builds —
 * #define BTH_HID_DESCRIPTOR_DEBUG to turn it on. Prints the descriptor item
 * by item to stdout; useful when bringing up a new peer to understand the
 * report map structure. The runtime mouse-field extractor below is a
 * separate, focused parser.
 * ----------------------------------------------------------------------------
 */
#ifdef BTH_HID_DESCRIPTOR_DEBUG

// Example usage

typedef enum
{
    HID_ITEM_TYPE_MAIN = 0,
    HID_ITEM_TYPE_GLOBAL = 1,
    HID_ITEM_TYPE_LOCAL = 2,
    HID_ITEM_TYPE_LONG = 3
} hid_item_type_t;

typedef struct
{
    uint8_t size;
    uint8_t type;
    uint8_t tag;
    uint32_t value;
    int32_t svalue;
} hid_item_t;

static const char *collection_types[] = {
    "Physical",
    "Application",
    "Logical",
    "Report",
    "Named Array",
    "Usage Switch",
    "Usage Modifier"};

static const char *usage_pages[] = {
    [0x01] = "Generic Desktop",
    [0x02] = "Simulation",
    [0x03] = "VR",
    [0x04] = "Sport",
    [0x05] = "Game",
    [0x06] = "Generic Device",
    [0x07] = "Keyboard",
    [0x08] = "LED",
    [0x09] = "Button",
    [0x0C] = "Consumer"};

static void print_indent(unsigned level)
{
    while (level--)
    {
        printf("  ");
    }
}

static void decode_main_item(
    const hid_item_t *item,
    unsigned collection_depth)
{
    switch (item->tag)
    {

    case 8: // Input
        print_indent(collection_depth);
        printf("Input: 0x%02X\n", (unsigned)item->value);
        break;

    case 9: // Output
        print_indent(collection_depth);
        printf("Output: 0x%02X\n", (unsigned)item->value);
        break;

    case 10: // Collection
        print_indent(collection_depth);

        if (item->value < 7)
            printf("Collection (%s)\n",
                   collection_types[item->value]);
        else
            printf("Collection (%u)\n",
                   (unsigned)item->value);

        break;

    case 11: // Feature
        print_indent(collection_depth);
        printf("Feature: 0x%02X\n", (unsigned)item->value);
        break;

    case 12: // End Collection
        print_indent(collection_depth);
        printf("End Collection\n");
        break;

    default:
        print_indent(collection_depth);
        printf("Main Tag %u: 0x%08X\n",
               item->tag,
               (unsigned)item->value);
        break;
    }
}

static void decode_global_item(
    const hid_item_t *item,
    uint16_t *usage_page,
    uint8_t *report_id)
{
    switch (item->tag)
    {

    case 0: // Usage Page
        *usage_page = (uint16_t)item->value;

        if (*usage_page < 256 &&
            usage_pages[*usage_page] != NULL)
        {
            printf("Usage Page: %s (0x%X)\n",
                   usage_pages[*usage_page],
                   *usage_page);
        }
        else
        {
            printf("Usage Page: 0x%X\n",
                   *usage_page);
        }
        break;

    case 1: // Logical Minimum
        printf("Logical Min: %d\n",
               (int)item->svalue);
        break;

    case 2: // Logical Maximum
        printf("Logical Max: %d\n",
               (int)item->svalue);
        break;

    case 7: // Report Size
        printf("Report Size: %u\n",
               (unsigned)item->value);
        break;

    case 8: // Report ID
        *report_id = (uint8_t)item->value;

        printf("Report ID: %u\n",
               (unsigned)*report_id);
        break;

    case 9: // Report Count
        printf("Report Count: %u\n",
               (unsigned)item->value);
        break;

    default:
        printf("Global Tag %u: 0x%08X\n",
               item->tag,
               (unsigned)item->value);
        break;
    }
}

static void decode_local_item(
    const hid_item_t *item,
    uint16_t usage_page)
{
    switch (item->tag)
    {

    case 0: // Usage
        printf("Usage: 0x%X", (unsigned)item->value);

        if (usage_page == 0x01)
        {

            switch (item->value)
            {

            case 0x02:
                printf(" (Mouse)");
                break;

            case 0x04:
                printf(" (Joystick)");
                break;

            case 0x05:
                printf(" (Gamepad)");
                break;

            case 0x06:
                printf(" (Keyboard)");
                break;

            case 0x30:
                printf(" (X)");
                break;

            case 0x31:
                printf(" (Y)");
                break;

            case 0x38:
                printf(" (Wheel)");
                break;
            }
        }

        printf("\n");
        break;

    case 1:
        printf("Usage Minimum: 0x%X\n",
               (unsigned)item->value);
        break;

    case 2:
        printf("Usage Maximum: 0x%X\n",
               (unsigned)item->value);
        break;

    default:
        printf("Local Tag %u: 0x%08X\n",
               item->tag,
               (unsigned)item->value);
        break;
    }
}

void parse_hid_report_descriptor(
    const uint8_t *descriptor,
    size_t length)
{
    size_t i = 0;

    uint16_t usage_page = 0;
    uint8_t report_id = 0;

    unsigned collection_depth = 0;

    while (i < length && descriptor[i] != 0)
    {

        uint8_t prefix = descriptor[i++];

        //    Long item

        if (prefix == 0xFE)
        {

            if ((i + 2) > length)
            {
                printf("Malformed long item\n");
                return;
            }

            uint8_t data_size = descriptor[i++];
            uint8_t long_tag = descriptor[i++];

            printf("Long Item: tag=%u size=%u\n",
                   long_tag,
                   data_size);

            if ((i + data_size) > length)
            {
                printf("Descriptor truncated\n");
                return;
            }

            i += data_size;
            continue;
        }

        uint8_t size_code = prefix & 0x03;
        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag = (prefix >> 4) & 0x0F;

        uint8_t size;

        switch (size_code)
        {
        case 0:
            size = 0;
            break;
        case 1:
            size = 1;
            break;
        case 2:
            size = 2;
            break;
        case 3:
            size = 4;
            break;
        default:
            size = 0;
            break;
        }

        if ((i + size) > length)
        {
            printf("Descriptor truncated\n");
            return;
        }

        hid_item_t item;

        item.size = size;
        item.type = type;
        item.tag = tag;
        item.value = extract_value(&descriptor[i], size);
        item.svalue = sign_extend(item.value, size);

        i += size;

        switch (type)
        {

        case HID_ITEM_TYPE_MAIN:

            if (tag == 12)
            {
                if (collection_depth)
                    collection_depth--;
            }

            decode_main_item(&item, collection_depth);

            if (tag == 10)
            {
                collection_depth++;
            }

            break;

        case HID_ITEM_TYPE_GLOBAL:
            decode_global_item(
                &item,
                &usage_page,
                &report_id);
            break;

        case HID_ITEM_TYPE_LOCAL:
            decode_local_item(
                &item,
                usage_page);
            break;

        default:
            printf("Reserved item type\n");
            break;
        }
    }
}

uint8_t hid_report_descriptor[] = {

    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x02, // Usage (Mouse)
    0xA1, 0x01, // Collection (Application)

    0x09, 0x01, // Usage (Pointer)
    0xA1, 0x00, // Collection (Physical)

    0x05, 0x09, // Usage Page (Button)
    0x19, 0x01, // Usage Minimum (1)
    0x29, 0x03, // Usage Maximum (3)

    0x15, 0x00, // Logical Minimum (0)
    0x25, 0x01, // Logical Maximum (1)

    0x95, 0x03, // Report Count (3)
    0x75, 0x01, // Report Size (1)

    0x81, 0x02, // Input (Data,Var,Abs)

    0xC0, // End Collection
    0xC0  // End Collection
};

#endif /* BTH_HID_DESCRIPTOR_DEBUG */

static uint32_t bt_tlv_get_size(void *ctx)
{
    (void)ctx;
    return BT_TLV_BANK_SIZE;
}

static uint32_t bt_tlv_get_alignment(void *ctx)
{
    (void)ctx;
    return 4;
}

static void bt_tlv_erase(void *ctx, int bank)
{
    (void)ctx;
    memset(&Option.bt_tlv[bank * BT_TLV_BANK_SIZE], 0xFF, BT_TLV_BANK_SIZE);
    SaveOptions();
}

static void bt_tlv_read(void *ctx, int bank, uint32_t offset,
                        uint8_t *buffer, uint32_t size)
{
    (void)ctx;
    memcpy(buffer, &Option.bt_tlv[bank * BT_TLV_BANK_SIZE + offset], size);
}

static void bt_tlv_write(void *ctx, int bank, uint32_t offset,
                         const uint8_t *data, uint32_t size)
{
    (void)ctx;
    /* btstack assumes flash, which can only flip bits 1→0 without erase.
       Mirror that semantic so the TLV format stays consistent across the
       (RAM-shadow → flash) round-trip. */
    uint8_t *dest = &Option.bt_tlv[bank * BT_TLV_BANK_SIZE + offset];
    for (uint32_t i = 0; i < size; i++)
    {
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

typedef enum
{
    BTK_OFF,
    BTK_HCI_WAITING,
    BTK_SCANNING,
    BTK_CONNECTING,
    BTK_CONNECTED_UNENCRYPTED,
    BTK_ENCRYPTED,
    BTK_HIDS_CONNECTING,
    BTK_READY,
} btk_state_t;

static volatile btk_state_t state;
static bool bt_startup_complete;

static bd_addr_t target_addr;
static bd_addr_type_t target_addr_type = BTH_TARGET_ADDR_TYPE;

static hci_con_handle_t conn_handle = HCI_CON_HANDLE_INVALID;
static uint16_t hids_cid;
static uint32_t report_count;

static uint8_t hid_descriptor_storage[HID_DESCRIPTOR_STORAGE_SIZE];

static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_packet_callback_registration_t sm_event_callback_registration;

/* Parallel notification listener registered directly with gatt_client.
   Filters on con_handle only (attribute_handle = ANY) so it catches
   every notification on our link, regardless of whether hids_client's
   own listener fires. Useful as a fallback + diagnostic — proves the
   notification dispatch path works even if hids_client's report
   matching has issues. */
static gatt_client_notification_t bth_raw_notification_listener;
static bool bth_raw_listener_installed;

/* Fallback pairing kick-off: if 2 s after LE connect we haven't seen
   any SM activity, the peer probably isn't going to send a Security
   Request. Initiate pairing ourselves so we don't sit forever. */
static btstack_timer_source_t pair_kickoff_timer;

/* Forward decl — bth_log is defined further down. */
static void bth_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void pair_kickoff_timeout(btstack_timer_source_t *ts)
{
    (void)ts;
    if (state == BTK_CONNECTED_UNENCRYPTED &&
        conn_handle != HCI_CON_HANDLE_INVALID)
    {
        bth_log("no Security Request after 2s; initiating pairing");
        sm_request_pairing(conn_handle);
    }
}

/* ============================================================================
 * Diagnostic logging — goes to USB CDC via stdio. Plain printf works
 * because PICOMITEBTH keeps stdio_usb enabled (unlike PICOMITEBT which
 * disables it).
 * ============================================================================
 */
static void bth_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void bth_log(const char *fmt, ...)
{
#ifdef BTH_DEBUG_LOG
    va_list ap;
    fputs("[BT] ", stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputc('\r', stdout);
    fputc('\n', stdout);
    fflush(stdout);
#else
    (void)fmt; /* diagnostics off — see BTH_DEBUG_LOG */
#endif
}

/* ============================================================================
 * Scan helpers — extract the Complete / Shortened Local Name AD field
 * from an advertising payload so we can print it in scan logs.
 * ============================================================================
 */
static bool ad_extract_name(const uint8_t *data, uint16_t len,
                            char *out, size_t out_size)
{
    ad_context_t ctx;
    for (ad_iterator_init(&ctx, len, data);
         ad_iterator_has_more(&ctx);
         ad_iterator_next(&ctx))
    {
        uint8_t type = ad_iterator_get_data_type(&ctx);
        uint8_t ad_len = ad_iterator_get_data_len(&ctx);
        const uint8_t *ad_data = ad_iterator_get_data(&ctx);
        if (type == BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME ||
            type == BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME)
        {
            size_t copy = (ad_len < out_size - 1) ? ad_len : (out_size - 1);
            memcpy(out, ad_data, copy);
            out[copy] = '\0';
            return true;
        }
    }
    out[0] = '\0';
    return false;
}

/* Scan the advertising payload for a 16-bit Service UUID match.
   Walks both Complete and Incomplete UUID-16 list AD records. */
static bool ad_has_uuid16(const uint8_t *data, uint16_t len, uint16_t uuid)
{
    ad_context_t ctx;
    for (ad_iterator_init(&ctx, len, data);
         ad_iterator_has_more(&ctx);
         ad_iterator_next(&ctx))
    {
        uint8_t type = ad_iterator_get_data_type(&ctx);
        uint8_t ad_len = ad_iterator_get_data_len(&ctx);
        const uint8_t *ad_data = ad_iterator_get_data(&ctx);
        if (type == BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS ||
            type == BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS)
        {
            for (uint8_t i = 0; (i + 1) < ad_len; i += 2)
            {
                uint16_t u = little_endian_read_16(ad_data, i);
                if (u == uuid)
                    return true;
            }
        }
    }
    return false;
}

#ifdef BTH_SCAN_DEBUG
/* Addresses already logged this scan session, so the verbose scan log
   shows each advertiser once rather than flooding. Reset by start_scan().
   Note: peripherals using a rotating Resolvable Private Address will
   re-log when their address changes — that is expected. */
#define BTH_SCAN_SEEN_MAX 32
static bd_addr_t bth_scan_seen[BTH_SCAN_SEEN_MAX];
static uint8_t bth_scan_seen_count;

static bool bth_scan_log_once(const uint8_t *addr)
{
    for (uint8_t i = 0; i < bth_scan_seen_count; i++)
        if (memcmp(bth_scan_seen[i], addr, sizeof(bd_addr_t)) == 0)
            return false;
    if (bth_scan_seen_count < BTH_SCAN_SEEN_MAX)
        memcpy(bth_scan_seen[bth_scan_seen_count++], addr, sizeof(bd_addr_t));
    return true; /* first sighting (or table full — log anyway) */
}
#endif

/* ============================================================================
 * Connection lifecycle
 * ============================================================================
 */
static void start_scan(void)
{
    state = BTK_SCANNING;
#ifdef BTH_SCAN_DEBUG
    bth_scan_seen_count = 0;
#endif
    /* Active scan, 30 ms interval / 30 ms window — aggressive, but
       keyboards advertise at 30-100 ms during pairing so this catches
       them quickly. */
    gap_set_scan_parameters(1 /*active*/, 0x0030, 0x0030);
    gap_start_scan();
    bth_log("scanning for keyboard %s (edit BTH_TARGET_ADDR_STRING to match)",
            BTH_TARGET_ADDR_STRING);
}

static void connect_target(void)
{
    state = BTK_CONNECTING;
    gap_stop_scan();
    bth_log("connecting to %s (addr_type=%u)",
            bd_addr_to_str(target_addr), target_addr_type);
    gap_connect(target_addr, target_addr_type);
}

/* Keyboard report decode lives in KeyboardMap.c now -- shared with
   USBKeyboard.c. process_kbd_report() takes the 8-byte boot-keyboard
   report directly and dispatches into the console RX ring via the
   same APP_MapKeyToUsage / USR_KEYBRD_ProcessData path the USB-HID-
   host build uses. Lifts international keymap (US/UK/DE/FR/ES/BE,
   selected via Option.KeyboardConfig), Caps/Num/Scroll lock state,
   and KeyDown[] tracking for the KEYDOWN() BASIC function. */
typedef struct
{
    uint8_t report_id;

    uint16_t usage_page;
    uint16_t usage;

    uint16_t x_bit_offset;
    uint16_t y_bit_offset;

    uint8_t x_size;
    uint8_t y_size;

    bool x_relative;
    bool y_relative;

    uint16_t button_bit_offset;
    uint8_t button_count;

    uint16_t report_bits;

} hid_mouse_descriptor_t;
typedef struct
{
    uint16_t usage_page;

    int32_t logical_min;
    int32_t logical_max;

    uint8_t report_size;
    uint8_t report_count;

    uint8_t report_id;

    uint16_t usages[16];
    uint8_t usage_count;

    uint16_t usage_min;
    uint16_t usage_max;

    uint32_t bit_offset;

    /* Application-Usage tracking: identifies a Mouse Application
       (Generic Desktop / Mouse = 0x01 / 0x02) regardless of which
       Report ID the peer chose. Set when we see a `Usage (Mouse)`
       followed by a `Collection (Application)`, cleared on the
       matching `End Collection`. Replaces the earlier hardcoded
       assumption that the mouse always lives at Report ID 2. */
    uint16_t last_local_usage;
    int8_t app_collection_depth;
    int8_t mouse_app_depth; /* -1 = not in mouse application */

} hid_parse_state_t;

static hid_mouse_descriptor_t mouse_info;
static void hid_clear_local(hid_parse_state_t *st)
{
    st->usage_count = 0;
    st->usage_min = 0;
    st->usage_max = 0;
}
static bool hid_extract_mouse_descriptor(
    const uint8_t *descriptor,
    size_t length,
    hid_mouse_descriptor_t *mouse)
{
    hid_parse_state_t st;

    memset(&st, 0, sizeof(st));
    memset(mouse, 0, sizeof(*mouse));
    st.mouse_app_depth = -1; /* not inside any mouse Application yet */

    size_t i = 0;

    while (i < length)
    {
        uint8_t prefix = descriptor[i++];

        /*
         * End if descriptor terminator encountered
         */
        if (prefix == 0)
            break;

        /*
         * Long items unsupported
         */
        if (prefix == 0xFE)
            return false;

        uint8_t size_code = prefix & 0x03;
        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag = (prefix >> 4) & 0x0F;

        uint8_t size =
            (size_code == 3) ? 4 : size_code;

        uint32_t value =
            extract_value(&descriptor[i], size);

        int32_t svalue =
            sign_extend(value, size);

        i += size;

        /*
         * GLOBAL ITEMS
         */
        if (type == 1)
        {
            switch (tag)
            {
            case 0: // Usage Page
                st.usage_page = (uint16_t)value;
                break;

            case 1: // Logical Minimum
                st.logical_min = svalue;
                break;

            case 2: // Logical Maximum
                st.logical_max = svalue;
                break;

            case 7: // Report Size
                st.report_size = (uint8_t)value;
                break;

            case 8: // Report ID
                /*
                 * New report
                 */
                st.report_id = (uint8_t)value;
                st.bit_offset = 0;
                st.usage_count = 0;
                break;

            case 9: // Report Count
                st.report_count = (uint8_t)value;
                break;
            }
        }

        /*
         * LOCAL ITEMS
         */
        else if (type == 2)
        {
            switch (tag)
            {
            case 0: // Usage
            {
                if (st.usage_count < 16)
                {
                    st.usages[st.usage_count++] =
                        (uint16_t)value;
                }
                /* Remember the most recent Usage local item so the
                   next Collection (Application) main item can decide
                   whether we're entering a Mouse Application. */
                st.last_local_usage = (uint16_t)value;
                break;
            }

            case 1: // Usage Minimum
            {
                st.usage_min = (uint16_t)value;
                break;
            }

            case 2: // Usage Maximum
            {
                st.usage_max = (uint16_t)value;

                /*
                 * Expand usage range
                 */
                for (uint16_t u = st.usage_min;
                     u <= st.usage_max &&
                     st.usage_count < 16;
                     u++)
                {
                    st.usages[st.usage_count++] = u;
                }

                break;
            }
            }
        }

        /*
         * MAIN ITEMS
         */
        else if (type == 0)
        {
            switch (tag)
            {
            case 8: // INPUT
            {
                bool is_constant =
                    (value & 0x01) != 0;

                bool is_variable =
                    (value & 0x02) != 0;

                bool is_relative =
                    (value & 0x04) != 0;

                /* True if this Input main item lives inside a Mouse
                   Application Collection — i.e. inside the bracketed
                   region opened by `Usage (Mouse) ; Collection
                   (Application)`. Replaces the earlier hardcoded
                   `report_id == 2` so other peers with the mouse on
                   a different Report ID still work. */
                bool is_mouse_report =
                    (st.mouse_app_depth >= 0);

                /*
                 * Parse fields
                 */
                if (!is_constant &&
                    is_variable &&
                    is_mouse_report)
                {
                    for (uint32_t field = 0;
                         field < st.report_count;
                         field++)
                    {
                        uint16_t usage = 0;

                        if (field < st.usage_count)
                            usage = st.usages[field];

                        uint32_t field_offset =
                            st.bit_offset +
                            (field * st.report_size);

                        /*
                         * Generic Desktop
                         */
                        if (st.usage_page == 0x01)
                        {
                            switch (usage)
                            {
                            case 0x30: // X
                            {
                                mouse->report_id =
                                    st.report_id;

                                mouse->usage_page = 0x01;
                                mouse->usage = 0x02;

                                mouse->x_bit_offset =
                                    field_offset;

                                mouse->x_size =
                                    st.report_size;

                                mouse->x_relative =
                                    is_relative;

                                break;
                            }

                            case 0x31: // Y
                            {
                                mouse->y_bit_offset =
                                    field_offset;

                                mouse->y_size =
                                    st.report_size;

                                mouse->y_relative =
                                    is_relative;

                                break;
                            }
                            }
                        }

                        /*
                         * Buttons
                         */
                        else if (st.usage_page == 0x09)
                        {
                            /*
                             * First button field
                             */
                            if (mouse->button_count == 0)
                            {
                                mouse->button_bit_offset =
                                    field_offset;
                            }

                            mouse->button_count++;
                        }
                    }
                }

                /*
                 * Advance report offset
                 */
                st.bit_offset +=
                    (uint32_t)st.report_size *
                    (uint32_t)st.report_count;

                /*
                 * Track mouse report size
                 */
                if (is_mouse_report)
                {
                    mouse->report_bits =
                        st.bit_offset;
                }

                /*
                 * Clear LOCAL state
                 */
                hid_clear_local(&st);
                break;
            }

            case 10: // COLLECTION
            {
                /* If this is an Application Collection (value == 1)
                   preceded by `Usage (0x01:0x02) = Mouse`, remember
                   the depth so subsequent Input items in this
                   bracket are recognised as mouse fields. */
                st.app_collection_depth++;
                if (value == 0x01 /*Application*/ &&
                    st.usage_page == 0x01 &&
                    st.last_local_usage == 0x02 /*Mouse*/ &&
                    st.mouse_app_depth < 0)
                {
                    st.mouse_app_depth = st.app_collection_depth;
                }
                hid_clear_local(&st);
                break;
            }

            case 12: // END COLLECTION
            {
                if (st.mouse_app_depth >= 0 &&
                    st.app_collection_depth == st.mouse_app_depth)
                {
                    /* Closing the bracket that opened the mouse
                       Application — stop treating subsequent fields
                       as mouse fields. */
                    st.mouse_app_depth = -1;
                }
                if (st.app_collection_depth > 0)
                    st.app_collection_depth--;
                hid_clear_local(&st);
                break;
            }
            default:
                break;
            }
        }
    }

    return true;
}
typedef struct
{
    int32_t x;
    int32_t y;
    uint32_t buttons;
} hid_mouse_report_t;

static hid_mouse_report_t mouse_report;
static uint32_t hid_extract_bits(
    const uint8_t *data,
    uint32_t bit_offset,
    uint32_t bit_count)
{
    uint32_t value = 0;

    for (uint32_t i = 0; i < bit_count; i++)
    {
        uint32_t bit =
            (data[(bit_offset + i) / 8] >>
             ((bit_offset + i) % 8)) &
            1u;

        value |= (bit << i);
    }

    return value;
}

static int32_t hid_extract_signed_bits(
    const uint8_t *data,
    uint32_t bit_offset,
    uint32_t bit_count)
{
    uint32_t v =
        hid_extract_bits(
            data,
            bit_offset,
            bit_count);

    /*
     * Sign extend
     */
    if (bit_count < 32)
    {
        uint32_t sign_bit =
            1u << (bit_count - 1);

        if (v & sign_bit)
        {
            v |= ~((1u << bit_count) - 1);
        }
    }

    return (int32_t)v;
}

static bool hid_parse_mouse_report(
    const hid_mouse_descriptor_t *desc,
    const uint8_t *report,
    hid_mouse_report_t *out)
{
    const uint8_t *payload = report;

    out->x =
        hid_extract_signed_bits(
            payload,
            desc->x_bit_offset,
            desc->x_size);

    out->y =
        hid_extract_signed_bits(
            payload,
            desc->y_bit_offset,
            desc->y_size);

    out->buttons =
        hid_extract_bits(
            payload,
            desc->button_bit_offset,
            desc->button_count);

    return true;
}

/* Forward declarations for the gamepad wire-in helpers (defined at the
   bottom of the file, near the gamepad parser they wrap). They take
   only primitive arg types so we don't need the gamepad typedefs in
   scope here. */
void bth_setup_gamepad_descriptor(const uint8_t *desc, uint16_t desc_len);
bool bth_try_handle_gamepad_report(const uint8_t *val, uint16_t vlen);

/* Parallel raw-notification handler — receives EVERY GATT notification
   on this connection (because we register with characteristic=NULL).
   Used as both fallback and diagnostic: if hids_client's own listener
   fires, our hids_client_handler will print "[BT] report …"; if only
   this raw handler fires, we know notifications are reaching gatt_client
   but hids_client isn't dispatching them. */
static void bth_raw_notification_handler(uint8_t packet_type,
                                         uint16_t channel,
                                         uint8_t *packet,
                                         uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET)
        return;
    if (hci_event_packet_get_type(packet) != GATT_EVENT_NOTIFICATION)
        return;

    uint16_t vh = gatt_event_notification_get_value_handle(packet);
    uint16_t vlen = gatt_event_notification_get_value_length(packet);
    const uint8_t *val = gatt_event_notification_get_value(packet);

    /* 8-byte notifications are HID boot-keyboard reports (modifier +
       reserved + 6 keycodes). Mouse reports are identified by their
       parsed bit-length from the Report Map — not a hardcoded 5,
       which would misfire on any peer whose mouse uses a different
       field layout (wheel, extra buttons, etc.). Everything else
       is logged raw so the operator can investigate. */
    if (vlen == 8)
    {
        /* Standard 8-byte HID boot-keyboard report (modifier, reserved,
           6 keycodes). Hand straight to the shared decoder — same code
           path the USB-HID-host build uses. n = 0 means "no associated
           USB HID[] device" (the LED-set call sites in process_key are
           gated by #ifdef USBKEYBOARD anyway, so n is unused here). */
        process_kbd_report((const hid_keyboard_report_t *)val, 0);
        return;
    }

    if (mouse_info.report_bits != 0 &&
        vlen * 8u == mouse_info.report_bits)
    {
        hid_parse_mouse_report(&mouse_info, val, &mouse_report);
        /* Hand the decoded x/y/buttons to the shared mouse post-
           decode helper. Slot n=2 is the BT mouse, matching the
           DEVICE(MOUSE 2, "X"/"Y"/"L"/...) reader in External.c.
           ProxiOS-style mice in this descriptor don't include a
           wheel field, so wheel_delta = 0. Casts are safe: the
           parsed x/y are sign-extended int32 but typical mouse
           deltas fit easily in int16. */
        process_mouse_input((int16_t)mouse_report.x,
                            (int16_t)mouse_report.y,
                            0,
                            (uint8_t)mouse_report.buttons,
                            2);
        return;
    }

    /* Gamepad report? Dispatches on the parsed gamepad descriptor's
       report_bits. Currently only prints the decoded axes/hat/buttons
       to the console — DEVICE(GAMEPAD …) wiring is a follow-up. */
    if (bth_try_handle_gamepad_report(val, vlen))
        return;

    /* Consumer-control report? Media keys (volume, play/pause, track
       skip) arrive as a 2-byte little-endian consumer Usage ID on a
       separate characteristic. Recognised usages are queued into the
       console RX ring as media-key pseudo-ASCII codes; the trailing
       all-zero release is swallowed. Unknown 2-byte reports fall
       through to the raw log below. */
    if (process_consumer_report(val, vlen))
        return;

#ifdef BTH_DEBUG_LOG
    printf("[BT] raw notify handle=0x%04x len=%u:", (unsigned)vh, (unsigned)vlen);
    for (uint16_t i = 0; i < vlen; i++)
        printf(" %02X", val[i]);
    fputc('\r', stdout);
    fputc('\n', stdout);
    fflush(stdout);
#else
    (void)vh; /* unrecognised report — silently dropped (BTH_DEBUG_LOG off) */
#endif
}

/* ============================================================================
 * hids_client handler — fires for HID-service events only.
 * ============================================================================
 */
static void hids_client_handler(uint8_t packet_type,
                                uint16_t channel,
                                uint8_t *packet,
                                uint16_t size)
{
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET)
        return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META)
        return;

    switch (hci_event_gattservice_meta_get_subevent_code(packet))
    {
    case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED:
    {
        uint8_t status = packet[5];

        if (status != ERROR_CODE_SUCCESS)
        {
            bth_log("HID service connect failed status=0x%02x", status);
            state = BTK_ENCRYPTED;
            break;
        }

        //        MMPrintString("Bluetooth HID device connected\r\n> ");

        /*
         * Parse the downloaded HID report descriptor

        printf("\n=== HID REPORT DESCRIPTOR ===\n");  */

        /* Pull the actual descriptor length from btstack rather than
           the raw storage size — sizeof() includes the trailing
           zero-padded region, which our walker has to terminate on
           heuristically. Using the real length avoids walking past
           valid data and is robust to descriptors that happen to
           contain literal zero bytes inside item payloads. */
        {
            const uint8_t *desc =
                hids_client_descriptor_storage_get_descriptor_data(hids_cid, 0);
            uint16_t desc_len =
                hids_client_descriptor_storage_get_descriptor_len(hids_cid, 0);
            if (desc != NULL && desc_len > 0)
            {
#ifdef BTH_HID_DESCRIPTOR_DEBUG
                parse_hid_report_descriptor(desc,
                                            desc_len);
#endif
                hid_extract_mouse_descriptor(desc, desc_len, &mouse_info);
                /* Also extract gamepad layout (axes / hat / buttons)
                   so notifications matching its bit-length get decoded
                   in bth_try_handle_gamepad_report below. */
                bth_setup_gamepad_descriptor(desc, desc_len);
            }
            else
            {
                /* Fall back to the raw buffer if btstack didn't expose
                                            the accessor for this build. */
                hid_extract_mouse_descriptor(
                    hid_descriptor_storage,
                    sizeof(hid_descriptor_storage),
                    &mouse_info);
                bth_setup_gamepad_descriptor(
                    hid_descriptor_storage,
                    sizeof(hid_descriptor_storage));
            }
        }
        /*        PInt(mouse_info.report_id);
                PIntComma(mouse_info.usage_page);
                PIntComma(mouse_info.usage);
                PIntComma(mouse_info.x_bit_offset);
                PIntComma(mouse_info.x_size);
                PIntComma(mouse_info.y_bit_offset);
                PIntComma(mouse_info.y_size);
                PIntComma(mouse_info.button_bit_offset);
                PIntComma(mouse_info.button_count);
                PRet();
                parse_hid_report_descriptor(
                    hid_descriptor_storage,
                    sizeof(hid_descriptor_storage));

                printf("=== END HID REPORT DESCRIPTOR ===\n\n");*/
        if (!bth_raw_listener_installed)
        {
            gatt_client_listen_for_characteristic_value_updates(
                &bth_raw_notification_listener,
                &bth_raw_notification_handler,
                conn_handle,
                NULL);

            bth_raw_listener_installed = true;
        }

        if (!CurrentLinePtr)
            MMPrintString("Bluetooth Keyboard Connected\r\n> ");

        state = BTK_READY;
        break;
    }
    case GATTSERVICE_SUBEVENT_HID_REPORT:
    {
        report_count++;
        //        uint16_t report_len = gattservice_subevent_hid_report_get_report_len(packet);
        //        const uint8_t *report = gattservice_subevent_hid_report_get_report(packet);
        //        uint8_t report_id = gattservice_subevent_hid_report_get_report_id(packet);
        //        printf("[BT] report #%lu id=%u len=%u:",
        //              (unsigned long)report_count, report_id, report_len);
        //        for (uint16_t i = 0; i < report_len; i++)
        //            printf(" %02X", report[i]);
        //        fputc('\n', stdout);
        //        fflush(stdout);
        break;
    }
    default:
        break;
    }
}

/* ============================================================================
 * Master HCI/GAP/SM packet handler
 * ============================================================================
 */
static void packet_handler(uint8_t packet_type,
                           uint16_t channel,
                           uint8_t *packet,
                           uint16_t size)
{
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET)
        return;

    uint8_t event = hci_event_packet_get_type(packet);
    switch (event)
    {

    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING)
        {
            bd_addr_t local;
            gap_local_bd_addr(local);
            bth_log("HCI working, local addr %s", bd_addr_to_str(local));
            start_scan();
        }
        break;

    case GAP_EVENT_ADVERTISING_REPORT:
    {
        if (state != BTK_SCANNING)
            break;

        bd_addr_t addr;
        gap_event_advertising_report_get_address(packet, addr);
        bd_addr_type_t addr_type = gap_event_advertising_report_get_address_type(packet);
        int8_t rssi = (int8_t)gap_event_advertising_report_get_rssi(packet);

        uint8_t data_len = gap_event_advertising_report_get_data_length(packet);
        const uint8_t *data = gap_event_advertising_report_get_data(packet);

        char name[33];
        ad_extract_name(data, data_len, name, sizeof(name));

        /* HID Service UUID = 0x1812. Devices that advertise it are
           proper BLE-HID (HOG) peripherals — eligible for hosting.
           Note: some keyboards only put 0x1812 in their SCAN RESPONSE,
           which arrives as a separate report (event type 4) — watch the
           scan log for a second line from the same address. */
        bool has_hid = ad_has_uuid16(data, data_len, 0x1812);

#ifdef BTH_SCAN_DEBUG
        /* Log each distinct advertiser once, before the RSSI gate so even
           weak/distant devices show. evt: 0=ADV_IND 1=ADV_DIRECT_IND
           2=ADV_SCAN_IND 3=ADV_NONCONN_IND 4=SCAN_RSP. */
        if (bth_scan_log_once(addr))
            bth_log("adv %s type=%u evt=%u rssi=%d%s name=\"%s\"",
                    bd_addr_to_str(addr), addr_type,
                    gap_event_advertising_report_get_advertising_event_type(packet),
                    (int)rssi, has_hid ? " HID" : "", name);
#endif

        /* Filter out distant advertisers — see BTH_SCAN_RSSI_MIN. */
        if (rssi < BTH_SCAN_RSSI_MIN)
            break;

        bool mac_match = (memcmp(addr, target_addr, sizeof(bd_addr_t)) == 0);
#ifdef BTH_MATCH_HID_UUID
        if (mac_match || has_hid)
        {
            /* If UUID match drove us (not the hardcoded MAC), adopt
               the advertiser's address — gap_connect needs the
               actual addr_type that was seen here. */
            if (!mac_match)
            {
                memcpy(target_addr, addr, sizeof(bd_addr_t));
                target_addr_type = addr_type;
                bth_log("HID advertiser found: %s type=%u name=\"%s\"",
                        bd_addr_to_str(addr), addr_type, name);
            }
            connect_target();
        }
#else
        if (mac_match)
        {
            connect_target();
        }
#endif
        break;
    }

    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet))
        {

        /* HCI_SUBEVENT_LE_CONNECTION_COMPLETE is delivered for all
           initiator-mode LE connections we care about. (The newer
           ENHANCED_CONNECTION_COMPLETE_V1/V2 subevents carry extra
           fields needed only for LE Privacy / Extended Advertising
           addressing — we don't enable those.) */
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
        {
            uint8_t status = hci_subevent_le_connection_complete_get_status(packet);
            if (status != 0)
            {
                bth_log("LE connect failed status=0x%02x; rescanning", status);
                start_scan();
                break;
            }
            conn_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
            state = BTK_CONNECTED_UNENCRYPTED;

            /* On a *bonded* reconnect (the common case after the keyboard
               sleeps and re-advertises on a key press) we don't need to
               wait for the peer: the central is expected to re-establish
               encryption, and for a device with stored keys
               sm_request_pairing() starts *reencryption* with the saved
               LTK rather than a fresh pairing. Kicking it off immediately
               removes the up-to-2 s idle wait for a Security Request that
               many keyboards never send on reconnect — that wait was the
               bulk of the wake-from-sleep reconnect delay. The fallback
               timer is still armed as a safety net.

               This is gated on a bond already existing. For a first-time
               pairing we keep the passive wait, because initiating our own
               Pairing Request before an iOS-style peer's Security Request
               makes btstack drop the late request and the peer disconnects
               with auth failure — and such peers also lock down the HID
               ATT handles until encrypted, so early GATT must be avoided.

               Single-target assumption: this host pairs one keyboard, so a
               non-empty LE device DB means "this peer is bonded". If you
               bond a second, different keyboard its very first connect may
               initiate early; clear bonds when switching keyboards. */
            bool bonded = (le_device_db_count() > 0);
            bth_log("LE connected, handle=0x%04x; %s", (unsigned)conn_handle,
                    bonded ? "bonded — requesting reencryption"
                           : "waiting for SM");
            if (bonded)
                sm_request_pairing(conn_handle);

            btstack_run_loop_set_timer(&pair_kickoff_timer, 2000);
            btstack_run_loop_set_timer_handler(&pair_kickoff_timer,
                                               &pair_kickoff_timeout);
            btstack_run_loop_add_timer(&pair_kickoff_timer);
            break;
        }
        default:
            break;
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE:
    {
        uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
        bth_log("disconnected reason=0x%02x; restarting scan", reason);
        /* Only report to the console if the keyboard had actually
           reached the ready (HID-connected) state — avoids spurious
           "Disconnected" lines for failed pairings / dropped scans. */
        if (state == BTK_READY && !CurrentLinePtr)
            MMPrintString("Bluetooth Keyboard Disconnected\r\n> ");
        btstack_run_loop_remove_timer(&pair_kickoff_timer);
        if (bth_raw_listener_installed)
        {
            gatt_client_stop_listening_for_characteristic_value_updates(
                &bth_raw_notification_listener);
            bth_raw_listener_installed = false;
        }
        conn_handle = HCI_CON_HANDLE_INVALID;
        hids_cid = 0;
        start_scan();
        break;
    }

    /* ---- SM events ----------------------------------------------------- */
    case SM_EVENT_JUST_WORKS_REQUEST:
        bth_log("SM Just Works requested; confirming");
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        break;

    case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
    {
        /* Both sides display a 6-digit number; user normally compares
           visually and confirms. For a headless host we auto-confirm —
           security loss is small since the user is the one initiating
           the pairing via their keyboard's pairing mode. */
        uint32_t passkey = sm_event_numeric_comparison_request_get_passkey(packet);
        bth_log("SM Numeric Comparison %06lu; auto-confirming",
                (unsigned long)passkey);
        sm_numeric_comparison_confirm(
            sm_event_numeric_comparison_request_get_handle(packet));
        break;
    }

    case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
    {
        /* Peer wants us to display a 6-digit passkey so the user can
           type it on the peer (e.g. a keyboard). We log it; the user
           reads it from the console. */
        uint32_t passkey = sm_event_passkey_display_number_get_passkey(packet);
        bth_log("SM Passkey: type %06lu on the BT device", (unsigned long)passkey);
        break;
    }

    case SM_EVENT_PASSKEY_INPUT_NUMBER:
        /* Peer wants us to type a passkey it's displaying. Not wired
           up — we'd need to grab input from the BASIC console.
           Logging the request so it shows up if hit. */
        bth_log("SM Passkey input requested — not implemented");
        break;

    case SM_EVENT_PAIRING_STARTED:
        bth_log("SM pairing started");
        break;

    case SM_EVENT_PAIRING_COMPLETE:
    {
        uint8_t pstatus = sm_event_pairing_complete_get_status(packet);
        if (pstatus != ERROR_CODE_SUCCESS)
        {
            bth_log("SM pairing failed status=0x%02x reason=0x%02x",
                    pstatus, sm_event_pairing_complete_get_reason(packet));
            /* Tear the link down so we try again from clean state. */
            gap_disconnect(conn_handle);
            break;
        }
        bth_log("SM pairing complete; link encrypted");
        btstack_run_loop_remove_timer(&pair_kickoff_timer);
        state = BTK_HIDS_CONNECTING;
        uint8_t hstat = hids_client_connect(conn_handle,
                                            &hids_client_handler,
                                            HID_PROTOCOL_MODE_REPORT,
                                            &hids_cid);
        if (hstat != ERROR_CODE_SUCCESS)
        {
            bth_log("hids_client_connect failed status=0x%02x", hstat);
        }
        break;
    }

    case SM_EVENT_REENCRYPTION_COMPLETE:
    {
        uint8_t rstatus = sm_event_reencryption_complete_get_status(packet);
        if (rstatus != ERROR_CODE_SUCCESS)
        {
            bth_log("SM reencryption failed status=0x%02x", rstatus);
            gap_disconnect(conn_handle);
            break;
        }
        bth_log("SM reencryption complete");
        btstack_run_loop_remove_timer(&pair_kickoff_timer);
        state = BTK_HIDS_CONNECTING;
        uint8_t hstat = hids_client_connect(conn_handle,
                                            &hids_client_handler,
                                            HID_PROTOCOL_MODE_REPORT,
                                            &hids_cid);
        if (hstat != ERROR_CODE_SUCCESS)
        {
            bth_log("hids_client_connect (reenc) failed status=0x%02x", hstat);
        }
        break;
    }

    default:
        break;
    }
}

void bt_keyboard_init(void)
{
    state = BTK_HCI_WAITING;
    conn_handle = HCI_CON_HANDLE_INVALID;
    hids_cid = 0;
    report_count = 0;

    /* Parse the placeholder target MAC into bd_addr_t form. */
    sscanf_bd_addr(BTH_TARGET_ADDR_STRING, target_addr);

    /* Route btstack's printf-based protocol logging to USB CDC. Only
       fires when ENABLE_LOG_INFO / ENABLE_LOG_ERROR are defined in
       btstack_config.h. */
    //    hci_dump_init(hci_dump_embedded_stdout_get_instance());

    l2cap_init();

    /* Persistent bonding via the Option struct (see bt_tlv_hal above).
       Must be set up BEFORE sm_init() so the security manager picks it
       up as the LE Device DB backing store. */
    {
        static btstack_tlv_flash_bank_t tlv_context;
        const btstack_tlv_t *tlv = btstack_tlv_flash_bank_init_instance(
            &tlv_context, &bt_tlv_hal, NULL);
        btstack_tlv_set_instance(tlv, &tlv_context);
        le_device_db_tlv_configure(tlv, &tlv_context);
    }

    sm_init();
    /* KEYBOARD_DISPLAY is the most flexible IO capability — it lets
       the SM negotiate any pairing method (Just Works, Numeric
       Comparison, Passkey Display, Passkey Input). NoInputNoOutput
       forces Just Works which can't provide MITM protection, and
       BLE-HID peripherals routinely require MITM and terminate the
       connection if Just Works is the only option. */
    sm_set_io_capabilities(IO_CAPABILITY_KEYBOARD_DISPLAY);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING |
                                       SM_AUTHREQ_MITM_PROTECTION |
                                       SM_AUTHREQ_SECURE_CONNECTION);

    gatt_client_init();

    /* Tiny GATT server — exposes only the mandatory GAP service so
       the peer can read our Device Name (and Appearance) after
       connecting. Without this we present as a nameless central
       and BLE-keyboard apps just show our MAC. */
    {
        static const char local_name[] = "PicoMiteBTH";
        /* Appearance = Generic HID (0x03C0). Some peers narrow this
           to Keyboard (0x03C1) but Generic HID is appropriate for a
           host accepting any HID device. Little-endian on the wire. */
        static uint8_t appearance[2] = {0xC0, 0x03};

        att_db_util_init();
        att_db_util_add_service_uuid16(ORG_BLUETOOTH_SERVICE_GENERIC_ACCESS);
        att_db_util_add_characteristic_uuid16(
            ORG_BLUETOOTH_CHARACTERISTIC_GAP_DEVICE_NAME,
            ATT_PROPERTY_READ,
            ATT_SECURITY_NONE, ATT_SECURITY_NONE,
            (uint8_t *)local_name, sizeof(local_name) - 1);
        att_db_util_add_characteristic_uuid16(
            ORG_BLUETOOTH_CHARACTERISTIC_GAP_APPEARANCE,
            ATT_PROPERTY_READ,
            ATT_SECURITY_NONE, ATT_SECURITY_NONE,
            appearance, sizeof(appearance));
        att_server_init(att_db_util_get_address(), NULL, NULL);
    }

    hids_client_init(hid_descriptor_storage, sizeof(hid_descriptor_storage));

    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    sm_event_callback_registration.callback = &packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    hci_power_control(HCI_POWER_ON);

    bt_startup_complete = true;
    if (Option.USBKeyboard == CONFIG_UK)
        keylayout = UKkeyValue;
    else if (Option.USBKeyboard == CONFIG_US)
        keylayout = USkeyValue;
    else if (Option.USBKeyboard == CONFIG_GR)
        keylayout = DEkeyValue;
    else if (Option.USBKeyboard == CONFIG_FR)
        keylayout = FRkeyValue;
    else if (Option.USBKeyboard == CONFIG_ES)
        keylayout = ESkeyValue;
    else if (Option.USBKeyboard == CONFIG_BE)
        keylayout = BEkeyValue;
}

void bt_keyboard_poll(void)
{
    if (!bt_startup_complete)
        return;

    cyw43_arch_poll();

    {
        static uint64_t last_heart_us;
        static int led_state;
        uint64_t now = time_us_64();

        /* OPTION HEARTBEAT OFF — user wants the LED dark. Turn it off
           once on the transition, then skip the toggle entirely. The
           led_state mirror means we won't call gpio_put again until
           OPTION HEARTBEAT ON flips it back. */
        if (Option.NoHeartbeat)
        {
            if (led_state != 0)
            {
                led_state = 0;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            }
            return;
        }

        /* LED cadence telegraphs link state at a glance:
             1000 ms — not yet connected (HCI booting / scanning / pairing)
              500 ms — keyboard connected + HID notifications enabled */
        uint32_t interval = (state == BTK_READY) ? 500000u : 1000000u;

        if (now - last_heart_us >= interval)
        {
            last_heart_us = now;
            led_state ^= 1;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        }
    }
}

bool bt_keyboard_ready(void)
{
    return state == BTK_READY;
}
/*
 * Generic HID Gamepad descriptor extraction + report decoder
 *
 * Supports:
 *
 *  - Multiple report IDs
 *  - X/Y/Z/Rx/Ry/Rz axes
 *  - Signed or unsigned axes
 *  - Hat switch
 *  - Arbitrary button counts
 *  - Non-byte-aligned reports
 *  - Dynamic bit offsets
 *
 * Intended for descriptors similar to:
 *
 *   Gamepad
 *     X/Y/Rx/Ry
 *     Hat switch
 *     12 buttons
 *
 * like the descriptor shown in the example.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define HID_MAX_USAGES 32
#define HID_MAX_AXES 8

/*
 * Generic Desktop usages
 */
#define HID_USAGE_X 0x30
#define HID_USAGE_Y 0x31
#define HID_USAGE_Z 0x32
#define HID_USAGE_RX 0x33
#define HID_USAGE_RY 0x34
#define HID_USAGE_RZ 0x35
#define HID_USAGE_HAT 0x39

typedef struct
{
    uint16_t usage;

    uint16_t bit_offset;

    uint8_t size;

    bool relative;

    bool is_signed;

} hid_gamepad_axis_t;

typedef struct
{
    uint8_t report_id;

    uint16_t usage_page;
    uint16_t usage;

    hid_gamepad_axis_t axes[HID_MAX_AXES];
    uint8_t axis_count;

    bool has_hat;

    uint16_t hat_bit_offset;
    uint8_t hat_size;

    int32_t hat_logical_min;
    int32_t hat_logical_max;

    uint16_t button_bit_offset;
    uint16_t button_count;

    uint16_t report_bits;

} hid_gamepad_descriptor_t;

typedef struct
{
    uint16_t usage_page;

    int32_t logical_min;
    int32_t logical_max;

    uint8_t report_size;
    uint8_t report_count;

    uint8_t report_id;

    uint16_t usages[HID_MAX_USAGES];
    uint8_t usage_count;

    uint16_t usage_min;
    uint16_t usage_max;

    uint32_t bit_offset;

    uint16_t last_local_usage;

    int8_t app_collection_depth;
    int8_t gamepad_app_depth;

} hid_gamepad_parse_state_t;

typedef struct
{
    int32_t axes[HID_MAX_AXES];

    uint8_t hat;

    uint32_t buttons;

} hid_gamepad_report_t;

/*
 * Helpers
 */

/*static uint32_t extract_value(
    const uint8_t *data,
    uint32_t size)
{
    uint32_t v = 0;

    for (uint32_t i = 0; i < size; i++)
    {
        v |= ((uint32_t)data[i]) << (i * 8);
    }

    return v;
}

static int32_t sign_extend(
    uint32_t value,
    uint32_t size_bytes)
{
    uint32_t bits = size_bytes * 8;

    if (bits >= 32)
        return (int32_t)value;

    uint32_t sign_bit = 1u << (bits - 1);

    if (value & sign_bit)
    {
        value |= ~((1u << bits) - 1);
    }

    return (int32_t)value;
}


static uint32_t hid_extract_bits(
    const uint8_t *data,
    uint32_t bit_offset,
    uint32_t bit_count)
{
    uint32_t value = 0;

    for (uint32_t i = 0; i < bit_count; i++)
    {
        uint32_t bit =
            (data[(bit_offset + i) / 8] >>
             ((bit_offset + i) % 8)) & 1u;

        value |= (bit << i);
    }

    return value;
}

static int32_t hid_extract_signed_bits(
    const uint8_t *data,
    uint32_t bit_offset,
    uint32_t bit_count)
{
    uint32_t v =
        hid_extract_bits(
            data,
            bit_offset,
            bit_count);

    if (bit_count < 32)
    {
        uint32_t sign_bit =
            1u << (bit_count - 1);

        if (v & sign_bit)
        {
            v |= ~((1u << bit_count) - 1);
        }
    }

    return (int32_t)v;
}
*/
/*
 * Descriptor parser
 */
static void hid_gamepad_clear_local(
    hid_gamepad_parse_state_t *st)
{
    st->usage_count = 0;
    st->usage_min = 0;
    st->usage_max = 0;
}

static bool hid_extract_gamepad_descriptor(
    const uint8_t *descriptor,
    size_t length,
    hid_gamepad_descriptor_t *pad)
{
    hid_gamepad_parse_state_t st;

    memset(&st, 0, sizeof(st));
    memset(pad, 0, sizeof(*pad));

    st.gamepad_app_depth = -1;

    size_t i = 0;

    while (i < length)
    {
        uint8_t prefix = descriptor[i++];

        if (prefix == 0)
            break;

        if (prefix == 0xFE)
            return false;

        uint8_t size_code = prefix & 0x03;
        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag = (prefix >> 4) & 0x0F;

        uint8_t size =
            (size_code == 3) ? 4 : size_code;

        uint32_t value =
            extract_value(&descriptor[i], size);

        int32_t svalue =
            sign_extend(value, size);

        i += size;

        /*
         * GLOBAL
         */
        if (type == 1)
        {
            switch (tag)
            {
            case 0:
                st.usage_page = (uint16_t)value;
                break;

            case 1:
                st.logical_min = svalue;
                break;

            case 2:
                st.logical_max = svalue;
                break;

            case 7:
                st.report_size = (uint8_t)value;
                break;

            case 8:
                st.report_id = (uint8_t)value;
                st.bit_offset = 0;
                break;

            case 9:
                st.report_count = (uint8_t)value;
                break;
            }
        }

        /*
         * LOCAL
         */
        else if (type == 2)
        {
            switch (tag)
            {
            case 0:
            {
                if (st.usage_count < HID_MAX_USAGES)
                {
                    st.usages[st.usage_count++] =
                        (uint16_t)value;
                }

                st.last_local_usage =
                    (uint16_t)value;

                break;
            }

            case 1:
                st.usage_min = (uint16_t)value;
                break;

            case 2:
            {
                st.usage_max = (uint16_t)value;

                for (uint16_t u = st.usage_min;
                     u <= st.usage_max &&
                     st.usage_count < HID_MAX_USAGES;
                     u++)
                {
                    st.usages[st.usage_count++] = u;
                }

                break;
            }
            }
        }

        /*
         * MAIN
         */
        else if (type == 0)
        {
            switch (tag)
            {
            /*
             * INPUT
             */
            case 8:
            {
                bool is_constant =
                    (value & 0x01) != 0;

                bool is_variable =
                    (value & 0x02) != 0;

                bool is_relative =
                    (value & 0x04) != 0;

                bool is_gamepad_report =
                    (st.gamepad_app_depth >= 0);

                if (!is_constant &&
                    is_variable &&
                    is_gamepad_report)
                {
                    for (uint32_t field = 0;
                         field < st.report_count;
                         field++)
                    {
                        uint16_t usage = 0;

                        if (field < st.usage_count)
                        {
                            usage = st.usages[field];
                        }

                        uint32_t field_offset =
                            st.bit_offset +
                            (field * st.report_size);

                        /*
                         * Generic Desktop
                         */
                        if (st.usage_page == 0x01)
                        {
                            /*
                             * Axes
                             */
                            if (usage >= 0x30 &&
                                usage <= 0x35)
                            {
                                if (pad->axis_count <
                                    HID_MAX_AXES)
                                {
                                    hid_gamepad_axis_t *a =
                                        &pad->axes[pad->axis_count++];

                                    a->usage = usage;
                                    a->bit_offset =
                                        field_offset;

                                    a->size =
                                        st.report_size;

                                    a->relative =
                                        is_relative;

                                    a->is_signed =
                                        (st.logical_min < 0);

                                    pad->report_id =
                                        st.report_id;

                                    pad->usage_page =
                                        0x01;

                                    pad->usage =
                                        0x05;
                                }
                            }

                            /*
                             * Hat switch
                             */
                            else if (usage ==
                                     HID_USAGE_HAT)
                            {
                                pad->has_hat = true;

                                pad->hat_bit_offset =
                                    field_offset;

                                pad->hat_size =
                                    st.report_size;

                                pad->hat_logical_min =
                                    st.logical_min;

                                pad->hat_logical_max =
                                    st.logical_max;
                            }
                        }

                        /*
                         * Buttons
                         */
                        else if (st.usage_page == 0x09)
                        {
                            if (pad->button_count == 0)
                            {
                                pad->button_bit_offset =
                                    field_offset;
                            }

                            pad->button_count++;
                        }
                    }
                }

                st.bit_offset +=
                    (uint32_t)st.report_size *
                    (uint32_t)st.report_count;

                if (is_gamepad_report)
                {
                    pad->report_bits =
                        st.bit_offset;
                }

                hid_gamepad_clear_local(&st);
                break;
            }

            /*
             * COLLECTION
             */
            case 10:
            {
                st.app_collection_depth++;

                /*
                 * Generic Desktop / Gamepad
                 */
                if (value == 0x01 &&
                    st.usage_page == 0x01 &&
                    st.last_local_usage == 0x05 &&
                    st.gamepad_app_depth < 0)
                {
                    st.gamepad_app_depth =
                        st.app_collection_depth;
                }

                hid_gamepad_clear_local(&st);
                break;
            }

            /*
             * END COLLECTION
             */
            case 12:
            {
                if (st.gamepad_app_depth >= 0 &&
                    st.app_collection_depth ==
                        st.gamepad_app_depth)
                {
                    st.gamepad_app_depth = -1;
                }

                if (st.app_collection_depth > 0)
                {
                    st.app_collection_depth--;
                }

                hid_gamepad_clear_local(&st);
                break;
            }
            }
        }
    }

    return true;
}

/*
 * Report decoder
 */

static bool hid_parse_gamepad_report(
    const hid_gamepad_descriptor_t *desc,
    const uint8_t *report,
    hid_gamepad_report_t *out)
{
    memset(out, 0, sizeof(*out));

    /*
     * Axes
     */
    for (uint32_t i = 0;
         i < desc->axis_count;
         i++)
    {
        const hid_gamepad_axis_t *a =
            &desc->axes[i];

        if (a->is_signed)
        {
            out->axes[i] =
                hid_extract_signed_bits(
                    report,
                    a->bit_offset,
                    a->size);
        }
        else
        {
            out->axes[i] =
                (int32_t)hid_extract_bits(
                    report,
                    a->bit_offset,
                    a->size);
        }
    }

    /*
     * Hat
     */
    if (desc->has_hat)
    {
        out->hat =
            (uint8_t)hid_extract_bits(
                report,
                desc->hat_bit_offset,
                desc->hat_size);
    }

    /*
     * Buttons
     */
    out->buttons =
        hid_extract_bits(
            report,
            desc->button_bit_offset,
            desc->button_count);

    return true;
}

/* ============================================================================
 * Gamepad wire-in helpers — bridge between bth_raw_notification_handler /
 * GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED and the gamepad parser. Lives
 * here so the parser's typedefs are in scope; called from earlier in the
 * file via the forward declarations near bth_raw_notification_handler.
 * ============================================================================
 */
static hid_gamepad_descriptor_t gamepad_info;
static hid_gamepad_report_t gamepad_report;

/* Called from hids_client_handler when the HID service has finished
   discovery. Walks the report descriptor a second time looking for a
   Gamepad Application collection; populates gamepad_info if found,
   zeroes it out otherwise. Safe to call with a NULL or zero-length
   descriptor (becomes a no-op). */
void bth_setup_gamepad_descriptor(const uint8_t *desc, uint16_t desc_len)
{
    if (desc == NULL || desc_len == 0)
    {
        memset(&gamepad_info, 0, sizeof(gamepad_info));
        return;
    }
    hid_extract_gamepad_descriptor(desc, desc_len, &gamepad_info);
}

/* Publish a decoded gamepad report into nunstruct[n] so the BASIC
   DEVICE(GAMEPAD n, "...") function can read it. Field mapping mirrors
   the USB-HID-host gamepad path (process_xbox / process_sony_ds*):

     ax = axis 0 (left  X)        DEVICE(GAMEPAD n, "LX")
     ay = axis 1 (left  Y)        DEVICE(GAMEPAD n, "LY")
     Z  = axis 2 (right X / Rx)   DEVICE(GAMEPAD n, "RX")
     C  = axis 3 (right Y / Ry)   DEVICE(GAMEPAD n, "RY")
     L  = axis 4 (Z, e.g. trigger or analog L)   DEVICE(GAMEPAD n, "L")
     R  = axis 5 (Rz)             DEVICE(GAMEPAD n, "R")
     x0 = buttons bitmask, low 12 bits = buttons 1..12   DEVICE(... "B")
     y0 = hat-switch direction:
            0..7 = N, NE, E, SE, S, SW, W, NW
            0xFF = idle (no direction)
          Reachable via the new DEVICE(GAMEPAD n, "H") accessor.
     type carries a magic identifier so BASIC code can tell apart the
       different gamepad sources via DEVICE(GAMEPAD n, "T").
     nunfoundc[n] is set on any state change for the ON GAMEPAD interrupt.

   Hats live in their own field so a hat-up press doesn't read as
   "no button pressed" in DEVICE(... "B"), which was the case when
   the hat was folded into x0's upper nibble. */
static void bth_gamepad_publish(const hid_gamepad_descriptor_t *desc,
                                const hid_gamepad_report_t *report,
                                uint8_t n)
{
    /* Axis values land in ax/ay/Z/C/L/R, zero-filled if the device
       doesn't report that axis. */
    int32_t a[6] = {0, 0, 0, 0, 0, 0};
    uint8_t cnt = desc->axis_count;
    if (cnt > 6)
        cnt = 6;
    for (uint8_t i = 0; i < cnt; i++)
        a[i] = report->axes[i];

    unsigned short btn = (unsigned short)(report->buttons & 0x0FFFu);

    /* Hat encoding: HID 4-bit hats use values 0..7 for the 8 cardinal /
       intercardinal directions and the out-of-range value (typically
       0x08 with logical_max=7, or 0x0F for null) for "idle". We use
       0xFF as the canonical "no direction" so the in-range values 0..7
       stay as standard hat codes. */
    unsigned short hat = 0xFF;
    if (desc->has_hat)
    {
        int32_t h = report->hat;
        if (h >= desc->hat_logical_min && h <= desc->hat_logical_max)
        {
            hat = (unsigned short)(h & 0x07);
        }
    }

    /* Edge-detect for the interrupt path. */
    if (nunstruct[n].ax != a[0] ||
        nunstruct[n].ay != a[1] ||
        nunstruct[n].Z != a[2] ||
        nunstruct[n].C != a[3] ||
        nunstruct[n].L != a[4] ||
        nunstruct[n].R != a[5] ||
        nunstruct[n].x0 != btn ||
        nunstruct[n].y0 != hat)
    {
        nunfoundc[n] = 1;
    }

    nunstruct[n].ax = a[0];
    nunstruct[n].ay = a[1];
    nunstruct[n].Z = a[2];
    nunstruct[n].C = a[3];
    nunstruct[n].L = a[4];
    nunstruct[n].R = a[5];
    nunstruct[n].x0 = btn;
    nunstruct[n].y0 = hat;
    nunstruct[n].type = 0x4254u; /* 'BT' magic */
}

/* Called from bth_raw_notification_handler. If the notification length
   matches the gamepad's parsed report size, decode and publish to
   nunstruct[3] so DEVICE(GAMEPAD 3, "...") reads live values.
   Returns true so the caller doesn't fall through to the raw-bytes
   log; false if this notification isn't a gamepad report. */
bool bth_try_handle_gamepad_report(const uint8_t *val, uint16_t vlen)
{
    if (gamepad_info.report_bits == 0)
        return false;
    if (vlen * 8u != gamepad_info.report_bits)
        return false;

    hid_parse_gamepad_report(&gamepad_info, val, &gamepad_report);
    bth_gamepad_publish(&gamepad_info, &gamepad_report, 3);
    return true;
}
#endif /* PICOMITEBTH || PICOMITEHDMIBTH */
