/*
 * shared/net/mm_net_wifi_cmd.c - shared WiFi BASIC command parsing.
 */

#include <string.h>

#include "MMBasic_Includes.h"
#include "MATHS.h"
#include "Memory.h"
#include "hal/hal_net.h"
#include "shared/net/mm_net_wifi_cmd.h"

void mm_net_wifi_parse_scan(unsigned char *arg, mm_net_scan_args_t *out) {
    memset(out, 0, sizeof(*out));
    if (!arg || !*arg) return;

    int size = parseintegerarray(arg, &out->dest, 1, 1, NULL, true) * 8;
    out->capacity = size - 8;
    if (out->capacity <= 0) error("array too small");
    out->buffer = (char *)&out->dest[1];
    out->dest[0] = 0;
}

void mm_net_wifi_scan_command(unsigned char *arg) {
    mm_net_scan_args_t parsed;
    mm_net_wifi_parse_scan(arg, &parsed);

    char *scan_out = parsed.buffer;
    size_t scan_cap = parsed.capacity;
    if (!parsed.dest) {
        scan_cap = 8192;
        scan_out = (char *)GetMemory(scan_cap);
    }
    size_t written = 0;
    int rc = hal_net_wifi_scan(scan_out, scan_cap, &written, 0);
    if (rc != HAL_NET_OK) {
        if (!parsed.dest) FreeMemory((unsigned char *)scan_out);
        error(parsed.dest ? "array too small" : "WiFi scan failed");
    }
    if (parsed.dest) {
        parsed.dest[0] = (int64_t)written;
    } else {
        MMPrintString(scan_out);
        FreeMemory((unsigned char *)scan_out);
    }
}

static void copy_checked(char *dest, size_t dest_len, const char *src,
                         const char *err) {
    if (strlen(src) >= dest_len) error((char *)err);
    strcpy(dest, src);
}

void mm_net_wifi_parse_credentials(unsigned char *arg,
                                   const char *default_hostname,
                                   mm_net_wifi_credentials_t *out) {
    memset(out, 0, sizeof(*out));
    getargs(&arg, 11, (unsigned char *)",");
    if (!(argc == 3 || argc == 5 || argc == 11)) error("Syntax");
    if (CurrentLinePtr) error("Invalid in a program");

    copy_checked(out->ssid, sizeof(out->ssid),
                 (char *)getCstring(argv[0]),
                 "SSID too long, max 63 chars");
    copy_checked(out->password, sizeof(out->password),
                 (char *)getCstring(argv[2]),
                 "Password too long, max 63 chars");

    if (argc >= 5 && *argv[4]) {
        copy_checked(out->hostname, sizeof(out->hostname),
                     (char *)getCstring(argv[4]),
                     "Hostname too long, max 31 chars");
    } else if (default_hostname) {
        copy_checked(out->hostname, sizeof(out->hostname), default_hostname,
                     "Hostname too long, max 31 chars");
    }

    if (argc == 11) {
        out->has_static_ip = 1;
        copy_checked(out->ipaddress, sizeof(out->ipaddress),
                     (char *)getCstring(argv[6]), "Invalid IP address");
        copy_checked(out->mask, sizeof(out->mask),
                     (char *)getCstring(argv[8]), "Invalid mask address");
        copy_checked(out->gateway, sizeof(out->gateway),
                     (char *)getCstring(argv[10]), "Invalid gateway address");
    }
}
