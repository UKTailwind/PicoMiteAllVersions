/*
 * btstack_config.h — BLE config shared by PicoMiteBT (peripheral / NUS
 * console) and PicoMiteBTH (central / HID host).
 *
 * Role and GATT direction differ between the two builds; common ground
 * is "single BLE link, cyw43439 controller, no Classic, no malloc-heavy
 * allocations".
 *
 * pico_btstack_ble defines ENABLE_BLE=1 on the command line; don't
 * redefine it here.
 */

#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

/* Port-specific flags. */
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_BTSTACK_STDIN
/* btstack/src/ble/att_db_util.c is compiled by pico_btstack_ble even
   though we use the static GATT DB from compile_gatt.py; it needs
   either HAVE_MALLOC or MAX_ATT_DB_SIZE to be defined. newlib's
   malloc is linked, so HAVE_MALLOC is appropriate. */
#define HAVE_MALLOC

#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_LE_SECURE_CONNECTIONS

/* LESC needs ECDH P-256. The cyw43 controller's offload support
   for it is unreliable across firmware versions, so compile in
   btstack's micro-ecc fallback. Likewise pull in software AES-128
   for the few code paths that bypass the controller. Together
   these add ~20 KB but they're a hard requirement for pairing
   with any peer that mandates LESC (iOS / iPadOS, most modern
   BLE keyboards). Mirror what the SDK's bundled example config
   does. */
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS
#define ENABLE_SOFTWARE_AES128

#ifdef PICOMITEBT
/* BLE role: we are a peripheral (advertiser + GATT server). */
#define ENABLE_LE_PERIPHERAL
#endif

#if defined(PICOMITEBTH) || defined(PICOMITEHDMIBTH)
/* BLE role: we are a central (scanner + GATT client) so we can
   discover a keyboard's HID Service, subscribe to input-report
   notifications, and write LED output reports.

   ENABLE_LE_PERIPHERAL is also defined here — btstack's hci.c has a
   privacy-resolution path that references peripheral-only fields
   (le_advertisements_state) without gating, so a CENTRAL-only build
   fails to compile. The SDK's shared example config takes the same
   approach. We never actually advertise, the dead code is small. */
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_PERIPHERAL
#define ENABLE_GATT_CLIENT_PAIRING
#endif

/* Static memory pools — keeps btstack's footprint deterministic. */
#define MAX_NR_HCI_CONNECTIONS 1
#define MAX_NR_SM_LOOKUP_ENTRIES 4
#define MAX_NR_WHITELIST_ENTRIES 4
/* Number of bonded peers held simultaneously. ~120 bytes of bt_tlv
   per entry plus btstack RAM state. Setting to 4 lets the user keep
   pairings with PC + phone + spare hosts (BT) or with multiple
   keyboards (BTH) without one evicting another. */
#define MAX_NR_LE_DEVICE_DB_ENTRIES 4
#if defined(PICOMITEBTH) || defined(PICOMITEHDMIBTH)
/* GATT client + HIDS client tables for the HID-host role. */
#define MAX_NR_GATT_CLIENTS 1
#define MAX_NR_HIDS_CLIENTS 1
#else
#define MAX_NR_GATT_CLIENTS 0
#endif
#define MAX_NR_SERVICE_RECORD_ITEMS 0

/* ACL buffer sized for a single high-throughput LE link. */
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (255 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

/* Persistent device DB — must match MAX_NR_LE_DEVICE_DB_ENTRIES.
   Entries are stored in bt_tlv[] inside the Option struct (see
   BTConsole.c). At ~120 bytes/entry plus TLV overhead, 4 entries fit
   easily within the 1 KB-per-bank budget. */
#define NVM_NUM_DEVICE_DB_ENTRIES 4
#define NVM_NUM_LINK_KEYS 0

/* HCI/btstack logging routes through printf, which the BTH build
   has via stdio_usb (USB CDC console). Verbose — every HCI command,
   ACL/SMP/L2CAP PDU dump appears interleaved with the BASIC prompt —
   but invaluable for diagnosing pairing failures. Keep enabled
   while bringing up new peers; turn off once HID flow is solid.
   PICOMITEBT (no stdio) still gates these off. */
#ifdef PICOMITEBTH
// #define ENABLE_LOG_INFO
// #define ENABLE_LOG_ERROR
#endif
/* hci_dump_embedded_stdout.c is compiled by pico_btstack_base and
   requires ENABLE_PRINTF_HEXDUMP regardless of whether logging is on.
   The dump code is never invoked at runtime (we never call
   hci_dump_init/set_handler), so this is purely to satisfy the
   compile-time #error guard. */
#define ENABLE_PRINTF_HEXDUMP

#endif /* BTSTACK_CONFIG_H */
