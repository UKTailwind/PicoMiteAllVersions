/*
 * shared/net/mm_net_ntp.h - NTP packet helpers for BASIC WEB surfaces.
 */

#ifndef MM_NET_NTP_H
#define MM_NET_NTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MM_NET_NTP_PACKET_LEN 48u
#define MM_NET_NTP_UNIX_DELTA 2208988800UL

void mm_net_ntp_build_request(uint8_t packet[MM_NET_NTP_PACKET_LEN]);
int mm_net_ntp_parse_unix_seconds(const uint8_t *packet, size_t len,
                                  uint32_t *unix_seconds);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_NTP_H */
