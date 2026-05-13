/*
 * shared/net/mm_net_ntp.c - NTP packet helpers for BASIC WEB surfaces.
 */

#include <string.h>

#include "shared/net/mm_net_ntp.h"

void mm_net_ntp_build_request(uint8_t packet[MM_NET_NTP_PACKET_LEN]) {
    if (!packet) return;
    memset(packet, 0, MM_NET_NTP_PACKET_LEN);
    packet[0] = 0x1b; /* LI=0, version=3, mode=3 client. */
}

int mm_net_ntp_parse_unix_seconds(const uint8_t *packet, size_t len,
                                  uint32_t *unix_seconds) {
    if (!packet || len < MM_NET_NTP_PACKET_LEN || !unix_seconds) return 0;
    uint8_t mode = packet[0] & 0x07u;
    uint8_t stratum = packet[1];
    if ((mode != 4u && mode != 5u) || stratum == 0u) return 0;
    uint32_t ntp_seconds = ((uint32_t)packet[40] << 24) |
                           ((uint32_t)packet[41] << 16) |
                           ((uint32_t)packet[42] << 8) |
                           (uint32_t)packet[43];
    if (ntp_seconds < MM_NET_NTP_UNIX_DELTA) return 0;
    *unix_seconds = ntp_seconds - MM_NET_NTP_UNIX_DELTA;
    return 1;
}
