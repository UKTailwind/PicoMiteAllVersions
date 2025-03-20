/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

Custom.h

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved. 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: 
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed 
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed 
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software 
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

************************************************************************************************************************/
#ifdef PICOMITEWEB
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
#endif

/* ********************************************************************************
 the C language function associated with commands, functions or operators should be
 declared here
**********************************************************************************/
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
// format:
//      void cmd_???(void)
//      void fun_???(void)
//      void op_???(void)
extern uint8_t pioTXlast[4][3];
extern char *pioRXinterrupts[4][3];
extern char *pioTXinterrupts[4][3];
#ifdef PICOMITEWEB
	extern void GetNTPTime(void);
	extern void checkTCPOptions(void);
    extern void open_tcp_server(void);
    extern void open_udp_server(void);
    extern volatile bool TCPreceived;
    extern char *TCPreceiveInterrupt;
    extern void TelnetPutC(int c,int flush);
    extern int cmd_mqtt(void);
    extern void cmd_ntp(unsigned char *tp);
    extern void cmd_udp(unsigned char *tp);
    extern int cmd_tcpclient(void);
    extern int cmd_tcpserver(void);
    extern int cmd_tftp_server_init(void);
    extern int cmd_tls();
    extern void closeMQTT(void);
    typedef struct TCP_SERVER_T_ {
        struct tcp_pcb *server_pcb;
        struct tcp_pcb *telnet_pcb;
        struct tcp_pcb* client_pcb[MaxPcb];
        volatile int inttrig[MaxPcb]; 
        uint8_t *buffer_sent[MaxPcb];
        uint8_t* buffer_recv[MaxPcb];
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
    extern TCP_CLIENT_T* tcp_client_init(void);
    extern void tcp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg);
    extern void starttelnet(struct tcp_pcb *client_pcb, int pcb, void *arg);
    extern char *UDPinterrupt;
    extern volatile bool UDPreceive;
#endif
extern int piointerrupt;
extern char *DMAinterruptRX;
extern char *DMAinterruptTX;
extern uint32_t dma_rx_chan;
extern uint32_t dma_tx_chan;
extern uint32_t dma_rx_chan2;
extern uint32_t dma_tx_chan2;
extern int dma_tx_pio;
extern int dma_tx_sm;
extern int dma_rx_pio;
extern int dma_rx_sm;
extern int dirOK;
extern bool PIO0;
extern bool PIO1;
extern bool PIO2;
extern uint64_t piomap[];

extern uint8_t nextline[4];

#define TCP_READ_BUFFER_SIZE 2048

#endif

/*  @endcond */

