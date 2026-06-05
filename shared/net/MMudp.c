/*
 * MMudp.c - Pico WEB UDP command surface backed by hal_net.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_net.h"
#include "shared/net/mm_net_interrupts.h"
#include "shared/net/mm_net_service.h"
#include "shared/net/mm_net_udp_cmd.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

static mm_net_udp_service_t pico_udp_service;

void pico_udp_poll(void) {
    mm_net_udp_service_poll(&pico_udp_service);
}

int open_udp_server(uint16_t port) {
    if (!port) return 1;
    if (WIFIconnected && !optionsuppressstatus) {
        MMPrintString("Starting UDP server at ");
        MMPrintString(ip4addr_ntoa(netif_ip4_addr(netif_list)));
        MMPrintString(" on port ");
        PInt(port);
        PRet();
    }
    if (!mm_net_udp_service_open(&pico_udp_service, port)) {
        error("Failed to create UDP server");
    }
    return 1;
}

void close_udp_server(void) {
    mm_net_udp_service_stop(&pico_udp_service);
}

void cmd_udp(unsigned char * p) {
    unsigned char * tp;
    tp = checkstring(p, (unsigned char *)"INTERRUPT");
    if (tp) {
        UDPinterrupt = mm_net_udp_parse_interrupt(tp);
        InterruptUsed = true;
        UDPreceive = 0;
        return;
    }
    tp = checkstring(p, (unsigned char *)"SEND");
    if (tp) {
        mm_net_udp_send_args_t parsed;
        mm_net_udp_parse_send(tp, &parsed);
        int rc = hal_net_udp_send(parsed.host, (uint16_t)parsed.port,
                                  parsed.payload, parsed.payload_len, 5000);
        if (rc == HAL_NET_TIMEOUT) error("Failed to convert web address");
        if (rc != HAL_NET_OK) error("Failed to send UDP packet");
        return;
    }
    error("Syntax");
}
