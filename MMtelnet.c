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
//#define DEBUG_printf printf
#define DEBUG_printf
static char Telnetbuff[256]={0};
static int Telnetpos=0;
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

static err_t tcp_telnet_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
//    DEBUG_printf("telnet_server_sent %u\n", len);
    state->sent_len[state->telnet_pcb_no] = len;
    return ERR_OK;
}
void TelnetPutCommand(int command, int option){
        Telnetbuff[Telnetpos]=255;
        Telnetpos++;
        Telnetbuff[Telnetpos]=command;
        Telnetpos++;
        if(option){
                Telnetbuff[Telnetpos]=255;
                Telnetpos++;
        }
}
void __not_in_flash_func(TelnetPutC)(int c,int flush){
        TCP_SERVER_T *state = (TCP_SERVER_T*)TCPstate;
        if(state->telnet_pcb_no==99 || !WIFIconnected )return;
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
        if(Telnetpos>=sizeof(Telnetbuff-4) || (flush==-1 && Telnetpos)){
                int pcb=state->telnet_pcb_no;
                state->to_send[pcb]=Telnetpos;
                state->buffer_sent[state->telnet_pcb_no]=(uint8_t *)Telnetbuff;
                if(state->client_pcb[pcb]){
//                        cyw43_arch_lwip_check();
                        tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                }
                Telnetpos=0;
        }
}
err_t tcp_telnet_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        static int lastchar=-1;
//        TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
//        int pcb=state->telnet_pcb_no;
        if (!p) {
                return ERR_OK;
        }
//        cyw43_arch_lwip_check();
        if (p->tot_len > 0) {
                tcp_recved(tpcb, p->tot_len);
                if(((char *)p->payload)[0]==255){
//                        for(int i=0;i<p->tot_len;i++)DEBUG_printf("%d,",((char *)p->payload)[i]);
//                        DEBUG_printf("\r\n");
                } else {
                        for(int j=0;j<p->tot_len;j++){
                                ConsoleRxBuf[ConsoleRxBufHead] = ((char *)p->payload)[j];
                                if((lastchar==13 && ConsoleRxBuf[ConsoleRxBufHead]==0) ||
                                (lastchar==255 && ConsoleRxBuf[ConsoleRxBufHead]==255)){
                                        lastchar=-1;
                                        continue;
                                }
                                if(BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey) {// if the user wants to stop the progran
                                        MMAbort = true;                                        // set the flag for the interpreter to see
                                        ConsoleRxBufHead = ConsoleRxBufTail;                    // empty the buffer
                                } else if(ConsoleRxBuf[ConsoleRxBufHead] ==keyselect && KeyInterrupt!=NULL){
                                        Keycomplete=1;
                                } else {
                                        lastchar=ConsoleRxBuf[ConsoleRxBufHead];
                                        ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;     // advance the head of the queue
                                        if(ConsoleRxBufHead == ConsoleRxBufTail) {                           // if the buffer has overflowed
                                        ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
                                        }
                                }
                        }
                }
        }
        pbuf_free(p);
        return ERR_OK;
}

void tcp_telnet_err(void *arg, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
//    if (err != ERR_ABRT) {
//        char buff[STRINGSIZE]={0};
//        DEBUG_printf("Telnet disconnected %d\r\n", err);
        tcp_server_close(arg,state->telnet_pcb_no);
        state->telnet_pcb_no=99;
//        if(!CurrentLinePtr) longjmp(mark, 1);  
//        else longjmp(ErrNext,1) ;
//    }
}

/*static err_t tcp_telnet_poll(void *arg, struct tcp_pcb *tpcb) {
        TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
//        cyw43_arch_lwip_check();
        int i=0;
        while(state->client_pcb[i]!=tpcb && i<=MaxPcb)i++;
        if(i==MaxPcb)error("Internal TCP receive error");
        state->write_pcb=i;
        DEBUG_printf("tcp_server_poll_fn\r\n");
        return tcp_server_close(argstate->telnet_pcb_no); // no activity so close the connection
}*/

void starttelnet(struct tcp_pcb *client_pcb, int pcb, void *arg){
        TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
//        DEBUG_printf("Telnet Client connected %x on pcb %d\r\n",(uint32_t)client_pcb,pcb);
        tcp_arg(client_pcb, state);
        tcp_sent(client_pcb, tcp_telnet_sent);
        tcp_recv(client_pcb, tcp_telnet_recv);
        tcp_err(client_pcb, tcp_telnet_err);
        state->telnet_pcb_no=pcb;
        state->keepalive[pcb]=1;
        err_t err = tcp_write(client_pcb, telnet_init_options, sizeof(telnet_init_options),  0);
        if (err != ERR_OK) { 
                tcp_server_close(state,pcb);
        }

}
