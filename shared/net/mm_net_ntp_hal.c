/*
 * shared/net/mm_net_ntp_hal.c - HAL-backed NTP exchange helper.
 */

#include "hal/hal_net.h"
#include "hal/hal_time.h"
#include "shared/net/mm_net_ntp.h"
#include "shared/net/mm_net_ntp_hal.h"

int mm_net_ntp_query_unix_seconds(const char * host, uint16_t port,
                                  uint32_t timeout_ms,
                                  uint32_t * unix_seconds) {
    if (!host || !unix_seconds) return HAL_NET_ERR;
    *unix_seconds = 0;

    hal_net_udp_socket_t sock = 0;
    int rc = hal_net_udp_bind(0, &sock);
    if (rc != HAL_NET_OK) return rc;

    uint8_t request[MM_NET_NTP_PACKET_LEN];
    mm_net_ntp_build_request(request);
    rc = hal_net_udp_socket_send(sock, host, port, request, sizeof(request),
                                 timeout_ms);
    if (rc != HAL_NET_OK) {
        hal_net_udp_close(sock);
        return rc;
    }

    uint64_t deadline = hal_time_us_64() + (uint64_t)timeout_ms * 1000u;
    uint8_t response[MM_NET_NTP_PACKET_LEN];
    while (hal_time_us_64() < deadline) {
        hal_net_poll();
        size_t len = 0;
        hal_net_addr_t from;
        rc = hal_net_udp_recv_event(sock, &from, response, sizeof(response),
                                    &len);
        if (rc == HAL_NET_OK) {
            if (mm_net_ntp_parse_unix_seconds(response, len, unix_seconds)) {
                hal_net_udp_close(sock);
                return HAL_NET_OK;
            }
            hal_net_udp_close(sock);
            return HAL_NET_ERR;
        }
        if (rc != HAL_NET_WOULD_BLOCK) {
            hal_net_udp_close(sock);
            return rc;
        }
        hal_time_sleep_us(10000);
    }

    hal_net_udp_close(sock);
    return HAL_NET_TIMEOUT;
}
