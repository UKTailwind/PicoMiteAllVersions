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
#define DEBUG_printf
TCP_CLIENT_T *TCP_CLIENT=NULL;
int streampointer=0;
// Perform initialisation
TCP_CLIENT_T* tcp_client_init(void) {
    TCP_CLIENT_T *state = calloc(1, sizeof(TCP_CLIENT_T));
    if (!state) {
//        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
//  ip4addr_aton(TEST_TCP_SERVER_IP, &state->remote_addr);
    return state;
}
// Call back with a DNS result
void tcp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (ipaddr) {
        state->remote_addr = *ipaddr;
        char buff[STRINGSIZE]={0};
        sprintf(buff,"tcp address %s\r\n", ip4addr_ntoa(ipaddr));
        if(!optionsuppressstatus)MMPrintString(buff);
        state->complete=1;
//        ntp_request(state);
    } else {
        free(state);
        error("tcp dns request failed");
    }
}
static err_t tcp_client_close(void *arg) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    err_t err = ERR_OK;
    if (state->tcp_pcb != NULL) {
        tcp_arg((struct tcp_pcb *)state->tcp_pcb, NULL);
        tcp_poll((struct tcp_pcb *)state->tcp_pcb, NULL, 0);
        tcp_sent((struct tcp_pcb *)state->tcp_pcb, NULL);
        tcp_recv((struct tcp_pcb *)state->tcp_pcb, NULL);
        tcp_err((struct tcp_pcb *)state->tcp_pcb, NULL);
        err = tcp_close((struct tcp_pcb *)state->tcp_pcb);
        if (err != ERR_OK) {
//            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort((struct tcp_pcb *)state->tcp_pcb);
            err = ERR_ABRT;
        }
        state->tcp_pcb = NULL;
    }
    return err;
}
static err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
//        TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
//        DEBUG_printf("tcp_client_sent %u\r\n", len);
        return ERR_OK;
}

static void tcp_client_err(void *arg, err_t err) {
    if (err != ERR_ABRT) {
        error("TCP client");
    }
}
err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (!p) {
        return ERR_OK;
    }
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
//    cyw43_arch_lwip_check();
    if (p->tot_len > 0) {
        routinechecks(); //don't know why I'm doing this but it solves a race condition for the RP2350
        // Receive the buffer
        const uint16_t buffer_left = state->BUF_SIZE - state->buffer_len;
        state->buffer_len += pbuf_copy_partial(p, (void *)state->buffer + state->buffer_len,
                                               p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);
        tcp_recved(tpcb, p->tot_len);
        cyw43_arch_lwip_begin();
        uint64_t *x=(uint64_t *)state->buffer;
        x--;
        *x=state->buffer_len;
        cyw43_arch_lwip_end();
    }
    pbuf_free(p);
    return ERR_OK;
}
err_t tcp_client_recv_stream(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (!p) {
        return ERR_OK;
    }
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
//    cyw43_arch_lwip_check();
    if (p->tot_len > 0) {
        for(int j=0;j<p->tot_len;j++){
            state->buffer[*state->buffer_write]= ((char *)p->payload)[j];
            *state->buffer_write = (*state->buffer_write + 1) % state->BUF_SIZE;     // advance the head of the queue
            if(*state->buffer_write == *state->buffer_read) {                           // if the buffer has overflowed
                *state->buffer_read = (*state->buffer_read + 1) % state->BUF_SIZE;  // throw away the oldest char
            }
        }
//        cyw43_arch_lwip_end();
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}
static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (err != ERR_OK) {
        error("connect failed %", err);
    }
    if(!optionsuppressstatus)MMPrintString("Connected\r\n");
    state->connected = true;
    return ERR_OK;
}


static bool tcp_client_open(void *arg) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    char buff[STRINGSIZE]={0};
    sprintf("Connecting to %s port %u\r\n", ip4addr_ntoa(&state->remote_addr), state->TCP_PORT);
    if(!optionsuppressstatus)MMPrintString(buff);
    state->tcp_pcb = tcp_new_ip_type(IP_GET_TYPE(&state->remote_addr));
    if (!state->tcp_pcb) {
        error("failed to create pcb");
        return false;
    }

    tcp_arg((struct tcp_pcb *)state->tcp_pcb, state);
//    tcp_poll(state->tcp_pcb, tcp_client_poll, POLL_TIME_S * 2);
    tcp_sent((struct tcp_pcb *)state->tcp_pcb, tcp_client_sent);
    if(state->buffer_write==NULL)tcp_recv((struct tcp_pcb *)state->tcp_pcb, tcp_client_recv);
    else tcp_recv((struct tcp_pcb *)state->tcp_pcb, tcp_client_recv_stream);
    tcp_err((struct tcp_pcb *)state->tcp_pcb, tcp_client_err);

    state->buffer_len = 0;

    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
//    cyw43_arch_lwip_begin();
    err_t err = tcp_connect((struct tcp_pcb *)state->tcp_pcb, &state->remote_addr, state->TCP_PORT, tcp_client_connected);
//    cyw43_arch_lwip_end();

    return err == ERR_OK;
}
void close_tcpclient(void){
    TCP_CLIENT_T *state = TCP_CLIENT;
    if(!state)return;
    tcp_client_close(state) ;
    free(state);
    TCP_CLIENT=NULL;
}

int cmd_tcpclient(void){
    unsigned char *tp;
    tp=checkstring(cmdline, (unsigned char *)"OPEN TCP CLIENT");
    if(tp){
            int timeout=5000;
            getargs(&tp,5,(unsigned char *)",");
            if(argc<3)error("Syntax");
            ip4_addr_t remote_addr;
            char *IP=GetTempMemory(STRINGSIZE);
            TCP_CLIENT_T *state = tcp_client_init();
            IP=(char *)getCstring(argv[0]);
            int port=getint(argv[2],1,65535);
            if(argc==5)timeout=getint(argv[4],1,100000);
            TCP_CLIENT=state;
            state->TCP_PORT=port;
            state->buffer_write=NULL;
            if(!isalpha((uint8_t)*IP) && strchr(IP,'.') && strchr(IP,'.')<IP+4){
                    if(!ip4addr_aton(IP, &remote_addr))error("Invalid address format");
                    state->remote_addr=remote_addr;
            } else {
                    int err = dns_gethostbyname(IP, &remote_addr, tcp_dns_found, state);
                    if(err==ERR_OK)state->remote_addr=remote_addr;
                    else if(err==ERR_INPROGRESS){
                        Timer4=timeout;
                        while(!state->complete && Timer4 && !(err==ERR_OK))if(startupcomplete)cyw43_arch_poll();
                        if(!Timer4)error("Failed to convert web address");
                        state->complete=0;
                    } else error("Failed to find TCP address");
            }
            if (!tcp_client_open(state)) {
                    error("Failed to open client");
            }

            Timer4=timeout;
            while(!state->connected && Timer4)if(startupcomplete)cyw43_arch_poll();
            if(!Timer4)error("No response from client");
            return 1;
    }
    tp=checkstring(cmdline, (unsigned char *)"OPEN TCP STREAM");
    if(tp){
            int timeout=5000;
            getargs(&tp,5,(unsigned char *)",");
            if(argc<3)error("Syntax");
            ip4_addr_t remote_addr;
            char *IP=GetTempMemory(STRINGSIZE);
            TCP_CLIENT_T *state = tcp_client_init();
            IP=(char *)getCstring(argv[0]);
            int port=getint(argv[2],1,65535);
            if(argc==5)timeout=getint(argv[4],1,100000);
            TCP_CLIENT=state;
            state->TCP_PORT=port;
            state->buffer_write=&streampointer;
            if(!isalpha((uint8_t)*IP) && strchr(IP,'.') && strchr(IP,'.')<IP+4){
                    if(!ip4addr_aton(IP, &remote_addr))error("Invalid address format");
                    state->remote_addr=remote_addr;
            } else {
                    int err = dns_gethostbyname(IP, &remote_addr, tcp_dns_found, state);
                    if(err==ERR_OK)state->remote_addr=remote_addr;
                    else if(err==ERR_INPROGRESS){
                        Timer4=timeout;
                        while(!state->complete && Timer4 && !(err==ERR_OK))ProcessWeb(0);
                        if(!Timer4)error("Failed to convert web address");
                        state->complete=0;
                    } else error("Failed to find TCP address");
            }
            if (!tcp_client_open(state)) {
                    error("Failed to open client");
            }

            Timer4=timeout;
            while(!state->connected && Timer4){{if(startupcomplete)ProcessWeb(0);}}
            if(!Timer4)error("No response from client");
            return 1;
    }

    tp=checkstring(cmdline, (unsigned char *)"TCP CLIENT REQUEST");
    if(tp){
            int64_t *dest=NULL;
            uint8_t *q=NULL;
            int size=0, timeout=5000;
            TCP_CLIENT_T *state = TCP_CLIENT;
            getargs(&tp,5,(unsigned char *)",");
            if(!state)error("No connection");
            if(!state->connected)error("No connection");
            if(argc<3)error("Syntax");
            char *request=(char *)getstring(argv[0]);
            size=parseintegerarray(argv[2],&dest,2,1,NULL,true)*8;
            dest[0]=0;
            q=(uint8_t *)&dest[1];
            if(argc==5)timeout=getint(argv[4],1,100000);
            state->BUF_SIZE=size;
            state->buffer=q;
            state->buffer_len=0;
            err_t err = tcp_write((struct tcp_pcb *)state->tcp_pcb, &request[1], (uint32_t)request[0], 0);
            if(err)error("write failed %",err);
            Timer4=timeout;
            while(!state->buffer_len && Timer4)ProcessWeb(0);
            if(!Timer4)error("No response from server");
            else Timer4=200;
            while(Timer4)ProcessWeb(0);
            return 1;
    }
    tp=checkstring(cmdline, (unsigned char *)"TCP CLIENT STREAM");
    if(tp){
            void *ptr1 = NULL;
            int64_t *dest=NULL;
            uint8_t *q=NULL;
            int size=0;
            TCP_CLIENT_T *state = TCP_CLIENT;
            getargs(&tp,7,(unsigned char *)",");
            if(!state)error("No connection");
            if(!state->connected)error("No connection");
            if(argc!=7)error("Syntax");
            char *request=(char *)getstring(argv[0]);
            size=parseintegerarray(argv[2],&dest,2,1,NULL,true)*8;
            dest[0]=0;
            q=(uint8_t *)&dest[1];
            ptr1 = findvar(argv[4], V_FIND | V_NOFIND_ERR);
            if(g_vartbl[g_VarIndex].type & T_INT) {
                    if(g_vartbl[g_VarIndex].dims[0] != 0) error("Argument 3 must be an integer");
                    state->buffer_read = (int *)ptr1;
            } else error("Argument 3 must be an integer");
            ptr1 = findvar(argv[6], V_FIND | V_NOFIND_ERR);
            if(g_vartbl[g_VarIndex].type & T_INT) {
                    if(g_vartbl[g_VarIndex].dims[0] != 0) error("Argument 4 must be an integer");
                    state->buffer_write = (int *)ptr1;
            } else error("Argument 4 must be an integer");
            state->BUF_SIZE=size;
            state->buffer=q;
            state->buffer_len=0;
            err_t err = tcp_write((struct tcp_pcb *)state->tcp_pcb, &request[1], (uint32_t)request[0], 0);
            if(err)error("write failed %",err);
            return 1;
    }
    tp=checkstring(cmdline, (unsigned char *)"CLOSE TCP CLIENT");
    if(tp){
            TCP_CLIENT_T *state = TCP_CLIENT;
            if(!state)error("No connection");
            close_tcpclient();
            return 1;
    }
    return 0;
}

