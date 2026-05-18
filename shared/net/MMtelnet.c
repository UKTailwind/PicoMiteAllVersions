/***********************************************************************************************************************
PicoMite MMBasic

custom.c

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
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_net.h"
#include "shared/net/mm_net_lifecycle.h"
#include "shared/net/mm_net_telnet_rx.h"
#include "pico/cyw43_arch.h"
//#define DEBUG_printf printf
#define DEBUG_printf

extern void pico_udp_poll(void);
extern void pico_tcp_client_stream_poll(void);
extern void pico_tcp_server_poll(void);
extern void pico_mqtt_poll(void);
extern void pico_tftp_poll(void);

static char Telnetbuff[256]={0};
static int Telnetpos=0;
static hal_net_tcp_server_t pico_telnet_server;
static hal_net_tcp_conn_t pico_telnet_conn;
/* RFC 854 IAC parser + CR-NUL dedup live in shared/net/mm_net_telnet_rx.c. */
static const uint8_t telnet_init_options[] =
{
//    TELNET_CHAR_IAC, TELNET_CHAR_WILL, TELNET_OPT_SUPPRESS_GO_AHEAD,
255,251,3,
//    TELNET_CHAR_IAC, TELNET_CHAR_DO, TELNET_OPT_SUPPRESS_GO_AHEAD,
255,253,3,
//    TELNET_CHAR_IAC, TELNET_CHAR_WILL, TELNET_OPT_ECHO,
255,251,1,
//
255,253,34,
255,254,34,

0
};

/* Whether the BASIC port has a configured telnet listener.
 * SerialConsolePutC() in PicoMite.c uses this to gate the stdio
 * (USB-CDC + UART) console path. Stub returns 1 on non-WiFi ports
 * so stdio always runs there. */
int wifi_serial_telnet_configured(void) {
    return Option.Telnet != -1;
}

static void pico_telnet_close_conn(void) {
        if (pico_telnet_conn) {
                hal_net_tcp_conn_close(pico_telnet_conn);
                pico_telnet_conn = 0;
        }
        Telnetpos = 0;
        mm_net_telnet_rx_reset();
}

void pico_telnet_close(void) {
        pico_telnet_close_conn();
        if (pico_telnet_server) {
                hal_net_tcp_server_close(pico_telnet_server);
                pico_telnet_server = 0;
        }
}

int pico_telnet_open(void) {
        if (!Option.Telnet || !WIFIconnected) return 1;
        if (pico_telnet_server) return 1;
        return hal_net_tcp_server_open(23, 1, &pico_telnet_server) == HAL_NET_OK;
}

void __not_in_flash_func(TelnetPutC)(int c,int flush){
        if(!pico_telnet_conn || !WIFIconnected )return;
        if(!(flush==-1)){
                Telnetbuff[Telnetpos]=c;
                Telnetpos++;
                if(c==255){
                        Telnetbuff[Telnetpos]=c;
                        Telnetpos++;
                }
                if(c==13){
                        Telnetbuff[Telnetpos]=0;
                        Telnetpos++;
                }
        }
        if(Telnetpos>=sizeof(Telnetbuff)-4 || (flush==-1 && Telnetpos)){
                if(hal_net_tcp_conn_send(pico_telnet_conn, Telnetbuff,
                                         (size_t)Telnetpos, 5000) !=
                   HAL_NET_OK) {
                        pico_telnet_close_conn();
                }
                Telnetpos=0;
        }
}

/* RFC 854 IAC parser lives in shared/net/mm_net_telnet_rx.c — shared
 * across every port. Previous body was buggy: it dropped the whole TCP
 * segment if the first byte was IAC, losing any keystrokes that arrived
 * in the same segment as a negotiation reply. */
static void pico_telnet_receive_bytes(const uint8_t *data, size_t len) {
        mm_net_telnet_rx_feed(data, len);
}

void pico_telnet_poll(int mode) {
        static uint64_t flushtimer=0;
        if(!Option.Telnet || !WIFIconnected) {
                pico_telnet_close();
                return;
        }
        if(!pico_telnet_server && !pico_telnet_open()) return;
        if(!pico_telnet_conn) {
                hal_net_tcp_conn_t conn = 0;
                int rc = hal_net_tcp_accept_conn(pico_telnet_server, &conn);
                if(rc == HAL_NET_OK) {
                        pico_telnet_conn = conn;
                        if(hal_net_tcp_conn_send(pico_telnet_conn,
                                                 telnet_init_options,
                                                 sizeof(telnet_init_options),
                                                 5000) != HAL_NET_OK) {
                                pico_telnet_close_conn();
                                return;
                        }
                } else if(rc != HAL_NET_WOULD_BLOCK) {
                        pico_telnet_close();
                        return;
                }
        }
        if(pico_telnet_conn) {
                uint8_t buf[128];
                for(;;) {
                        size_t len = 0;
                        int rc = hal_net_tcp_conn_recv(pico_telnet_conn, buf,
                                                       sizeof(buf), &len);
                        if(rc == HAL_NET_OK) {
                                pico_telnet_receive_bytes(buf, len);
                                continue;
                        }
                        if(rc != HAL_NET_WOULD_BLOCK)
                                pico_telnet_close_conn();
                        break;
                }
                if(mode && pico_telnet_conn && time_us_64() > flushtimer) {
                        TelnetPutC(0,-1);
                        flushtimer=time_us_64()+5000;
                }
        }
}

void __not_in_flash_func(ProcessWeb)(int mode){
    static const mm_net_lifecycle_poll_hooks_t hooks = {
        .poll_udp = pico_udp_poll,
        .poll_tftp = pico_tftp_poll,
        .poll_tcp_client_stream = pico_tcp_client_stream_poll,
        .poll_mqtt = pico_mqtt_poll,
        .poll_tcp_server = pico_tcp_server_poll,
        .poll_telnet = pico_telnet_poll,
    };
    static uint64_t lastusec=0;
    static int testcount=0;  
    static int lastonoff=0;
    static uint64_t lastheartmsec=0;
    uint64_t timenow=time_us_64();   
    if(!WIFIconnected && startupcomplete)goto flashonly;
    mm_net_lifecycle_poll(&hooks, mode, 0);
    if(testcount == 0 || timenow>lastusec){
        lastusec=timenow+1000;
        testcount = 0 ;
        if(startupcomplete)cyw43_arch_poll();
    }
    testcount++;
    if(testcount==100)testcount=0;
    if(!mode)return;
    flashonly:;
    if(Option.NoHeartbeat){
        if(lastonoff!=2){
            if(startupcomplete){
                if(cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN)) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
                lastonoff=2;
            }
        }
    } else {
        if(lastonoff==2)lastonoff=0;
        if(timenow-lastheartmsec>(WIFIconnected ? 500000:1000000) && startupcomplete){
            lastheartmsec=timenow;
            if(lastonoff)cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            else cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            lastonoff^=1;
        }
    }
}
