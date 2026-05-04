/*
 * wifi_includes.h — lwIP / CYW43 header set + TCP/NTP/UDP type
 * declarations used only by WiFi-stack TUs (MMtcpserver.c,
 * MMtelnet.c, MMntp.c, MMTCPclient.c, MMsetwifi.c, Custom.c on WiFi
 * ports). Pulled out of Custom.h so the universal header stays
 * preprocessor-clean.
 *
 * Only WiFi-stack TUs include this file directly. Non-WiFi builds
 * never see the lwIP types or the typedefs.
 */

#ifndef WIFI_INCLUDES_H
#define WIFI_INCLUDES_H

#include "port_config.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/apps/mqtt.h"
#include "lwip/apps/mqtt_priv.h"
#include "lwip/timeouts.h"
#include "lwip/ip_addr.h"
#include "lwip/mem.h"
#include "lwip/err.h"

extern void GetNTPTime(void);
extern void checkTCPOptions(void);
extern void open_tcp_server(void);
extern void open_udp_server(void);
extern volatile bool TCPreceived;
extern char *TCPreceiveInterrupt;
extern void TelnetPutC(int c, int flush);
extern int  cmd_mqtt(void);
extern void cmd_ntp(unsigned char *tp);
extern void cmd_udp(unsigned char *tp);
extern int  cmd_tcpclient(void);
extern int  cmd_tcpserver(void);
extern int  cmd_tftp_server_init(void);
extern int  cmd_tls(void);
extern void closeMQTT(void);

typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    struct tcp_pcb *telnet_pcb;
    struct tcp_pcb *client_pcb[MaxPcb];
    volatile int inttrig[MaxPcb];
    uint8_t *buffer_sent[MaxPcb];
    uint8_t *buffer_recv[MaxPcb];
    volatile int sent_len[MaxPcb];
    volatile int recv_len[MaxPcb];
    volatile int total_sent[MaxPcb];
    volatile int to_send[MaxPcb];
    volatile uint64_t pcbopentime[MaxPcb];
    volatile int keepalive[MaxPcb];
    volatile int telnet_pcb_no;
    volatile int telnet_init_sent;
} TCP_SERVER_T;

typedef struct NTP_T_ {
    ip_addr_t ntp_server_address;
    bool dns_request_sent;
    struct udp_pcb *ntp_pcb;
    absolute_time_t ntp_test_time;
    alarm_id_t ntp_resend_alarm;
    volatile bool complete;
} NTP_T;

typedef struct TCP_CLIENT_T_ {
    volatile struct tcp_pcb *tcp_pcb;
    ip_addr_t remote_addr;
    volatile uint8_t *buffer;
    volatile int buffer_len;
    volatile bool complete;
    volatile bool connected;
    volatile int BUF_SIZE;
    volatile int TCP_PORT;
    volatile int *buffer_write;
    volatile int *buffer_read;
    volatile char *hostname;
} TCP_CLIENT_T;

extern TCP_SERVER_T *TCPstate;
extern void cleanserver(void);
extern err_t tcp_server_close(void *arg, int pcb);
extern err_t tcp_server_send_data(void *arg, struct tcp_pcb *tpcb, int pcb);
extern void checksent(void *arg, int fn, int pcb);
extern TCP_CLIENT_T *TCP_CLIENT;
extern TCP_CLIENT_T *tcp_client_init(void);
extern void tcp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg);
extern void starttelnet(struct tcp_pcb *client_pcb, int pcb, void *arg);
extern char *UDPinterrupt;
extern volatile bool UDPreceive;

#endif /* WIFI_INCLUDES_H */
