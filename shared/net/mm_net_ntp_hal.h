/*
 * shared/net/mm_net_ntp_hal.h - HAL-backed NTP exchange helper.
 */

#ifndef MM_NET_NTP_HAL_H
#define MM_NET_NTP_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int mm_net_ntp_query_unix_seconds(const char * host, uint16_t port,
                                  uint32_t timeout_ms,
                                  uint32_t * unix_seconds);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_NTP_HAL_H */
