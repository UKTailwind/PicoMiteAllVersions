#include <stdint.h>

#include "hal/hal_net.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "shared/net/mm_net_service.h"
#include "shared/net/mm_net_udp_cmd.h"
#include "esp32_udp.h"

extern char *UDPinterrupt;
extern volatile bool UDPreceive;

static mm_net_udp_service_t s_udp;

int esp32_udp_interrupt_pending(void)
{
    return UDPreceive ? 1 : 0;
}

void esp32_udp_poll(void)
{
    mm_net_udp_service_poll(&s_udp);
}

void esp32_udp_server_stop(void)
{
    mm_net_udp_service_stop(&s_udp);
}

int esp32_udp_server_open(uint16_t port)
{
    if (!port) return 0;
    return mm_net_udp_service_open(&s_udp, port);
}

int esp32_udp_cmd(unsigned char *tp)
{
    unsigned char *arg;

    arg = checkstring(tp, (unsigned char *)"INTERRUPT");
    if (arg) {
        UDPinterrupt = mm_net_udp_parse_interrupt(arg);
        InterruptUsed = true;
        UDPreceive = false;
        return 1;
    }

    arg = checkstring(tp, (unsigned char *)"SEND");
    if (arg) {
        mm_net_udp_send_args_t parsed;
        mm_net_udp_parse_send(arg, &parsed);
        int rc = hal_net_udp_send(parsed.host, (uint16_t)parsed.port,
                                  parsed.payload, parsed.payload_len, 5000);
        if (rc != HAL_NET_OK)
            error("Failed to send UDP packet");
        return 1;
    }

    return 0;
}
