/***********************************************************************************************************************
PicoMite MMBasic

MMudp.c

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
/*
 * @cond
 * The following section will be excluded from the documentation.
 */
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "lwip/udp.h"
#include "lwip/timeouts.h"
#include "lwip/debug.h"
struct udp_pcb *udppcb = NULL, *sendudppcb = NULL;
typedef struct UDP_T_
{
    ip_addr_t udp_server_address;
    struct udp_pcb *ntp_pcb;
    volatile bool complete;
} UDP_T;
UDP_T UDPstate;

static void
udp_recv_func(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    LWIP_UNUSED_ARG(arg);
    if (p == NULL)
        return;
    int len = p->tot_len;
    if (len >= sizeof(messagebuff))
        len = sizeof(messagebuff) - 1;
    memset(messagebuff, 0, sizeof(messagebuff));
    pbuf_copy_partial(p, &messagebuff[1], len, 0);
    messagebuff[0] = len;
    memset(addressbuff, 0, sizeof(addressbuff));
    sprintf((char *)&addressbuff[1], "%d.%d.%d.%d", (int)addr->addr & 0xff, (int)(addr->addr >> 8) & 0xff, (int)(addr->addr >> 16) & 0xff, (int)(addr->addr >> 24) & 0xff);
    addressbuff[0] = strlen((char *)&addressbuff[1]);
    //	udp_sendto(upcb, p, addr, port);
    pbuf_free(p);
    UDPreceive = 1;
}
void udp_server_init(void)
{
    udppcb = udp_new();
    if (!udppcb)
    {
        MMPrintString("Failed to allocate UDP server pcb\r\n");
        return;
    }
    ip_set_option(udppcb, SOF_BROADCAST);
    //    err_t ret;
    if (Option.UDP_PORT && WIFIconnected && !optionsuppressstatus)
    {
        MMPrintString("Starting UDP server at ");
        MMPrintString(ip4addr_ntoa(netif_ip4_addr(netif_list)));
        MMPrintString(" on port ");
        PInt(Option.UDP_PORT);
        PRet();
    }

    udp_bind(udppcb, IP_ADDR_ANY, Option.UDP_PORT);

    udp_recv(udppcb, udp_recv_func, udppcb);
}

void open_udp_server(void)
{
    udp_server_init();
    return;
}
static void udp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
    UDP_T *state = (UDP_T *)arg;
    if (ipaddr)
    {
        state->udp_server_address = *ipaddr;
        state->complete = 1;
    }
    else
    {
        /* state is &UDPstate (global), never free() it */
        web_async_set_error("udp dns request failed");
    }
}

void cmd_udp(unsigned char *p)
{
    unsigned char *tp;
    tp = checkstring(p, (unsigned char *)"INTERRUPT");
    if (tp)
    {
        getcsargs(&tp, 1);
        if (argc != 1)
            SyntaxError();
        ;
        UDPinterrupt = (char *)GetIntAddress(argv[0]);
        InterruptUsed = true;
        UDPreceive = 0;
        return;
    }
    tp = checkstring(p, (unsigned char *)"SEND");
    if (tp)
    {
        getcsargs(&tp, 5);
        if (argc != 5)
            SyntaxError();
        ;
        UDP_T *state = &UDPstate;
        state->complete = 0;
        ip4_addr_t remote_addr;
        int timeout = 5000;
        char *IP = GetTempStrMemory();
        strcpy(IP, (char *)getCstring(argv[0]));
        int dots = 0;
        for (const char *p = IP; *p; p++) if (*p == '.') dots++;
        if (dots == 3 && ip4addr_aton(IP, &remote_addr))
        {
            state->udp_server_address = remote_addr;
        }
        else
        {
            int err = dns_gethostbyname(IP, &remote_addr, udp_dns_found, state);
            if (err == ERR_OK)
                state->udp_server_address = remote_addr;
            else if (err == ERR_INPROGRESS)
            {
                Timer4 = timeout;
                while (!state->complete && Timer4 && !(err == ERR_OK))
                    if (startupcomplete)
                        cyw43_arch_poll();
                web_async_check_error();
                if (!Timer4)
                    error("Failed to convert web address");
            }
            else
                error("Failed to find UDP address");
        }
        unsigned char *data = getstring(argv[4]);
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (uint16_t)data[0], PBUF_RAM);
        if (!p)
            error("Failed to allocate UDP pbuf");
        char *req = (char *)p->payload;
        memset(req, 0, data[0]);
        int port = getint(argv[2], 1, 65535);
        memcpy(req, &data[1], data[0]);
        sendudppcb = udp_new();
        if (!sendudppcb)
        {
            pbuf_free(p);
            error("Failed to allocate UDP send pcb");
        }
        ip_set_option(sendudppcb, SOF_BROADCAST);
        err_t er = udp_sendto(sendudppcb, p, &state->udp_server_address, port);
        pbuf_free(p);
        if (er != ERR_OK)
        {
            printf("Failed to send UDP packet! error=%d", er);
        }
        //        ProcessWeb(0);
        udp_remove(sendudppcb);
        sendudppcb = NULL;
        return;
    }
}
/*  @endcond */
