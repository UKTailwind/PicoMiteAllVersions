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
bool optionsuppressstatus=0;
int TCP_PORT ;
//#define //DEBUG_printf printf
#define DEBUG_printf
const char httpheadersfail[]="HTTP/1.0 404\r\n\r\n";
TCP_SERVER_T *TCPstate=NULL;
jmp_buf recover;
static TCP_SERVER_T* tcp_server_init(void) {
    if(!TCPstate) {
        TCPstate = (TCP_SERVER_T*)calloc(1,sizeof(TCP_SERVER_T));
        memset(TCPstate,0,sizeof(TCP_SERVER_T));
    }
    if (!TCPstate) {
        //DEBUG_printf("failed to allocate state\r\n");
        return NULL;
    }
    for(int i=0;i<MaxPcb;i++){
        TCPstate->client_pcb[i]=NULL;
    }
    TCPstate->telnet_pcb_no=99;
    return TCPstate;
}
err_t tcp_server_close(void *arg, int pcb) {
    if(pcb==99)return ERR_OK;
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    err_t err = ERR_OK;
    if (state->client_pcb[pcb] != 0) {
        tcp_arg(state->client_pcb[pcb], NULL);
        tcp_sent(state->client_pcb[pcb], NULL);
        tcp_recv(state->client_pcb[pcb], NULL);
        tcp_err(state->client_pcb[pcb], NULL);
        tcp_poll(state->client_pcb[pcb], NULL,0);
        err = tcp_close(state->client_pcb[pcb]);
        if (err != ERR_OK) {
            tcp_abort(state->client_pcb[pcb]);
            error("close failed %, calling abort", err);
            err = ERR_ABRT;
        }
        //DEBUG_printf("Close success %x on pcb %x\r\n",state->client_pcb[pcb],pcb);
        state->recv_len[pcb]=0;
        state->client_pcb[pcb]=NULL;
        FreeMemorySafe((void **)&state->buffer_recv[pcb]);
    }
    return err;
}

static err_t tcp_server_result(void *arg, int status) {
        return ERR_OK;
}

static err_t tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len, int pcb) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    ////DEBUG_printf("tcp_server_sent %u\r\n", len);
    state->pcbopentime[pcb]=time_us_64();
    state->sent_len[pcb] += len;
    return ERR_OK;
}
static err_t tcp_server_sent0(void *arg, struct tcp_pcb *tpcb, u16_t len) {
        return tcp_server_sent(arg, tpcb, len, 0);
}
static err_t tcp_server_sent1(void *arg, struct tcp_pcb *tpcb, u16_t len) {
        return tcp_server_sent(arg, tpcb, len, 1);
}
static err_t tcp_server_sent2(void *arg, struct tcp_pcb *tpcb, u16_t len) {
        return tcp_server_sent(arg, tpcb, len, 2);
}
static err_t tcp_server_sent3(void *arg, struct tcp_pcb *tpcb, u16_t len) {
        return tcp_server_sent(arg, tpcb, len, 3);
}
static err_t tcp_server_sent4(void *arg, struct tcp_pcb *tpcb, u16_t len) {
        return tcp_server_sent(arg, tpcb, len, 4);
}
static err_t tcp_server_sent5(void *arg, struct tcp_pcb *tpcb, u16_t len) {
        return tcp_server_sent(arg, tpcb, len, 5);
}
static err_t tcp_server_sent6(void *arg, struct tcp_pcb *tpcb, u16_t len) {
        return tcp_server_sent(arg, tpcb, len, 6);
}
static err_t tcp_server_sent7(void *arg, struct tcp_pcb *tpcb, u16_t len) {
        return tcp_server_sent(arg, tpcb, len, 7);
}
err_t tcp_server_send_data(void *arg, struct tcp_pcb *tpcb, int pcb)
{
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;

//    state->sent_len[pcb] = 0;
//    if(pcb!=state->telnet_pcb_no)//DEBUG_printf("Writing %d bytes to client %x\r\n",state->to_send[pcb], (uint32_t)tpcb);
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    int t;
    err_t err;
    uint64_t timestart=time_us_64()+5000000;
    while((t=tcp_sndqueuelen(tpcb))>6 && time_us_64()<timestart){
        //DEBUG_printf("Send queue %u\r\n", t);
        if(startupcomplete)cyw43_arch_poll();
    } 
    state->pcbopentime[pcb]=time_us_64();
    if(time_us_64()<timestart){
           err = tcp_write(tpcb, state->buffer_sent[pcb], state->to_send[pcb],  0);
    } else err=1;
    if (err != ERR_OK) {
        tcp_server_close(state,pcb);
    }
    return ERR_OK;
}
err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err, int pcb) {
//        static int count=0;
        TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
        if (!p) {
                return ERR_OK;
        }
        // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
        // can use this method to cause an assertion in debug mode, if this method is called when
        // cyw43_arch_lwip_begin IS needed
//        cyw43_arch_lwip_check();
        if (p->tot_len > 0) {
                TCPreceived=1;
    	        if(!CurrentLinePtr){  // deal with requests when we don't want them
                        tcp_recved(tpcb, p->tot_len);
                        //DEBUG_printf("Sending 404 on pcb %d, rbuff address is %x ",pcb, (uint32_t)state->buffer_recv[pcb]);
                        state->sent_len[pcb]=0;
                        state->to_send[pcb]= state->total_sent[pcb] = strlen(httpheadersfail);
                        state->buffer_sent[pcb]=(unsigned char *)httpheadersfail;
                        if(state->client_pcb[pcb]){
                                tcp_server_send_data(state, state->client_pcb[pcb],pcb);
                                checksent(state,0, pcb);
                                tcp_server_close(state, pcb) ;
                        }
                } else {
                        OptionErrorSkip = 1;
                        if(state->buffer_recv[pcb]!=NULL){
                                FreeMemorySafe((void **)&state->buffer_recv[pcb]);
//                                MMPrintString("Internal error in tcp_server_recv - Attempting recovery");
                        }
                        state->buffer_recv[pcb]=GetMemory(p->tot_len);
                        state->inttrig[pcb]=1;
                        //DEBUG_printf("Tcp_HTTP_recv on pcb %d / %d\r\n",pcb, p->tot_len);
                        state->recv_len[pcb] = pbuf_copy_partial(p, state->buffer_recv[pcb] , p->tot_len, 0);
                        if(state->recv_len[pcb]!=p->tot_len) MMPrintString("Warning: WebMite Internal error");
                        for(int i=0;i<p->tot_len;i++)if(state->buffer_recv[pcb][i]==0)state->buffer_recv[pcb][i]=32;
                        tcp_recved(tpcb, p->tot_len);
                        state->pcbopentime[pcb]=time_us_64();
                }
        }
        //DEBUG_printf("Stack pointer is %x free space on heap %u\r\n",((uint32_t)__get_MSP()),getFreeHeap());
        pbuf_free(p);
        return ERR_OK;
}

err_t tcp_server_recv0(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        return tcp_server_recv(arg, tpcb, p, err,0);
}
err_t tcp_server_recv1(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        return tcp_server_recv(arg, tpcb, p, err,1);
}
err_t tcp_server_recv2(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        return tcp_server_recv(arg, tpcb, p, err,2);
}
err_t tcp_server_recv3(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        return tcp_server_recv(arg, tpcb, p, err,3);
}
err_t tcp_server_recv4(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        return tcp_server_recv(arg, tpcb, p, err,4);
}
err_t tcp_server_recv5(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        return tcp_server_recv(arg, tpcb, p, err,5);
}
err_t tcp_server_recv6(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        return tcp_server_recv(arg, tpcb, p, err,6);
}
err_t tcp_server_recv7(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
        return tcp_server_recv(arg, tpcb, p, err,7);
}

static void tcp_server_err(void *arg, err_t err, int pcb) {
    if (err != ERR_ABRT) {
        //DEBUG_printf("tcp_client_err_fn %d\r\n", err);
        tcp_server_close(arg, pcb);
    }
}
static void tcp_server_err0(void *arg, err_t err) {
        tcp_server_err(arg, err, 0);
}
static void tcp_server_err1(void *arg, err_t err) {
        tcp_server_err(arg, err, 1);
}
static void tcp_server_err2(void *arg, err_t err) {
        tcp_server_err(arg, err, 2);
}
static void tcp_server_err3(void *arg, err_t err) {
        tcp_server_err(arg, err, 3);
}
static void tcp_server_err4(void *arg, err_t err) {
        tcp_server_err(arg, err, 4);
}
static void tcp_server_err5(void *arg, err_t err) {
        tcp_server_err(arg, err, 5);
}
static void tcp_server_err6(void *arg, err_t err) {
        tcp_server_err(arg, err, 6);
}
static void tcp_server_err7(void *arg, err_t err) {
        tcp_server_err(arg, err, 7);
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    int pcb=0;
    if (err != ERR_OK || client_pcb == NULL) {
        //DEBUG_printf("Failure in accept\r\n");
        tcp_server_result(arg, err);
        return ERR_VAL;
    }
    for(pcb=0;pcb<=MaxPcb;pcb++){
        if(pcb==MaxPcb)MMPrintString("Warning: No free connections\r\n");
//        if(pcb==MaxPcb)error("No free connections");
        if(state->client_pcb[pcb]==NULL){
                state->client_pcb[pcb] = client_pcb;
                break;
        }
    }
    
    if(client_pcb->local_port==23 && Option.Telnet){
        starttelnet(client_pcb, pcb, arg) ;
        return ERR_OK;
    } else if(client_pcb->local_port==TCP_PORT && TCP_PORT){
        //DEBUG_printf("HTTP Client connected %x on pcb %d\r\n",(uint32_t)client_pcb,pcb);
        tcp_arg(client_pcb, state);
        state->keepalive[pcb]=0;
        tcp_arg(client_pcb, state);
        switch(pcb){
                case 0:
                        tcp_sent(client_pcb, tcp_server_sent0);
                        tcp_recv(client_pcb, tcp_server_recv0);
                        tcp_err(client_pcb, tcp_server_err0);
                        break;
                case 1:
                        tcp_sent(client_pcb, tcp_server_sent1);
                        tcp_recv(client_pcb, tcp_server_recv1);
                        tcp_err(client_pcb, tcp_server_err1);
                        break;
                case 2:
                        tcp_sent(client_pcb, tcp_server_sent2);
                        tcp_recv(client_pcb, tcp_server_recv2);
                        tcp_err(client_pcb, tcp_server_err2);
                        break;
                case 3:
                        tcp_sent(client_pcb, tcp_server_sent3);
                        tcp_recv(client_pcb, tcp_server_recv3);
                        tcp_err(client_pcb, tcp_server_err3);
                        break;
                case 4:
                        tcp_sent(client_pcb, tcp_server_sent4);
                        tcp_recv(client_pcb, tcp_server_recv4);
                        tcp_err(client_pcb, tcp_server_err4);
                        break;
                case 5:
                        tcp_sent(client_pcb, tcp_server_sent5);
                        tcp_recv(client_pcb, tcp_server_recv5);
                        tcp_err(client_pcb, tcp_server_err5);
                        break;
                case 6:
                        tcp_sent(client_pcb, tcp_server_sent6);
                        tcp_recv(client_pcb, tcp_server_recv6);
                        tcp_err(client_pcb, tcp_server_err6);
                        break;
                case 7:
                        tcp_sent(client_pcb, tcp_server_sent7);
                        tcp_recv(client_pcb, tcp_server_recv7);
                        tcp_err(client_pcb, tcp_server_err7);
                        break;
        }
        state->pcbopentime[pcb]=time_us_64();
    } //else DEBUG_printf("Attempted connection on port %d\r\n",client_pcb->local_port);
   int t=0;
    for(int i=0;i<MaxPcb;i++){
        if(state->client_pcb[i]==NULL){
                t++;
        } 
    }
    //DEBUG_printf("Connection still free %u\r\n", t);

    return ERR_OK;
}
static bool tcp_server_open(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if(TCP_PORT && WIFIconnected){
        MMPrintString("Starting TCP server at ");
        MMPrintString(ip4addr_ntoa(netif_ip4_addr(netif_list)));
        MMPrintString(" on port ");
        PInt(TCP_PORT);PRet();
    }

    struct tcp_pcb *httppcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    struct tcp_pcb *telnet_pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!httppcb || !telnet_pcb) {
        //DEBUG_printf("failed to create pcbs\r\n");
        return false;
    }
    err_t err;
    if(TCP_PORT){
        err = tcp_bind(httppcb, NULL, TCP_PORT);
        if (err) {
                char buff[STRINGSIZE]={0};
                sprintf(buff,"failed to bind to port %d\n",TCP_PORT);
                if(!optionsuppressstatus)MMPrintString(buff);
                return false;
        }
        state->server_pcb = tcp_listen_with_backlog(httppcb, MaxPcb);
        if (!state->server_pcb) {
                //DEBUG_printf("failed to listen\r\n");
                if (httppcb) {
                tcp_close(httppcb);
                }
                return false;
        }
        tcp_arg(state->server_pcb, state);
        tcp_accept(state->server_pcb, tcp_server_accept);
    }
    if(Option.Telnet){    
        err = tcp_bind(telnet_pcb, NULL, 23);
        if (err) {
                char buff[STRINGSIZE]={0};
                sprintf(buff,"failed to bind to port %d\n",23);
                if(!optionsuppressstatus)MMPrintString(buff);
                return false;
        }
        state->telnet_pcb = tcp_listen_with_backlog(telnet_pcb, MaxPcb);
        if (!state->telnet_pcb) {
                //DEBUG_printf("failed to listen\r\n");
                if (telnet_pcb) {
                tcp_close(telnet_pcb);
                }
                return false;
        }
        tcp_arg(state->telnet_pcb, state);
        tcp_accept(state->telnet_pcb, tcp_server_accept);
    }


    return true;
}
void checksent(void *arg, int fn, int pcb){
  TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
  int loopcount=1000000;

  while(state->sent_len[pcb]!=state->total_sent[pcb]  && loopcount ){
      loopcount--;
      CheckAbort();
  }
  if(loopcount==0){
      if(fn)ForceFileClose(fn);
      tcp_server_close(state, pcb) ;
      MMPrintString("Warning: LWIP send data timeout\r\n");
  }
}
const char httpheaders[]="HTTP/1.1 200 OK\r\nServer:CPi\r\nConnection:close\r\nContent-type:";
const char httptail[]="\r\nContent-Length:";
const char httpend[]="\r\n\r\n";
const char httphtml[]="text/html\r\nContent-Length:";

void cleanserver(void){
    if(!TCPstate)return;
    TCP_SERVER_T *state = (TCP_SERVER_T*)TCPstate;
        for(int i=0 ; i<MaxPcb ; i++){
                if(state->client_pcb[i] && i!=state->telnet_pcb_no)tcp_server_close(state,i);
        }
}
void cmd_transmit(unsigned char *cmd){
    unsigned char *tp;
    int tlen;
    tp=checkstring(cmd, (unsigned char *)"CODE");
    if(tp){
    	getargs(&tp, 3, (unsigned char *)",");
        if(argc != 3)error("Argument count");
        TCP_SERVER_T *state = (TCP_SERVER_T*)TCPstate;
        char httpheaders[]="HTTP/1.0 404\r\n\r\n";
        int pcb = getint(argv[0],1,MaxPcb)-1;
        tlen=getint(argv[2],100,999);
        IntToStr(&httpheaders[9],tlen,10);
        httpheaders[12]='\r';
        state->to_send[pcb]=strlen(httpheaders);
        state->sent_len[pcb]=0;
        state->buffer_sent[pcb]=(unsigned char *)httpheaders;
        state->total_sent[pcb]=strlen(httpheaders);
        if(setjmp(recover) != 0)error("Transmit failed");
        if(state->client_pcb[pcb]){
                tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                checksent(state,0,pcb);
                tcp_server_close(state,pcb);
        }
//        {if(startupcomplete)ProcessWeb(0);}
        return;
    } 

    if((tp=checkstring(cmd, (unsigned char *)"FILE"))){
        int32_t fn;
        char *fname;
        char *ctype;
        char *outstr=GetTempMemory(STRINGSIZE);
        char p[10]={0};
        int FileSize; 
        UINT n_read;
    	getargs(&tp, 5, (unsigned char *)",");
        if(argc != 5)error("Argument count");
        TCP_SERVER_T *state = (TCP_SERVER_T*)TCPstate;
        int pcb = getint(argv[0],1,MaxPcb)-1;
        fname=(char *)getCstring(argv[2]);
        ctype=(char *)getCstring(argv[4]);
        strcpy(outstr,httpheaders);
        strcat(outstr,ctype);
        strcat(outstr,httptail);
        if(*fname == 0) error("Cannot find file");
        if (ExistsFile(fname)) {
                fn = FindFreeFileNbr();
                if (!BasicFileOpen(fname, fn, FA_READ))
                return;
                if(FatFSFileSystem)  FileSize = f_size(FileTable[fn].fptr);
                else FileSize = lfs_file_size(&lfs,FileTable[fn].lfsptr);
                IntToStr(p,FileSize,10);
                strcat(outstr,p);
                strcat(outstr,httpend);
//                int i=0;
                state->sent_len[pcb]=0;
                state->to_send[pcb]= state->total_sent[pcb] = strlen(outstr);
                state->buffer_sent[pcb]=(unsigned char *)outstr;
                if(setjmp(recover) != 0)error("Transmit failed");
                //DEBUG_printf("sending file header to pcb %d\r\n",pcb);
                tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                if(FileSize<TCP_MSS*4 && FileSize<FreeSpaceOnHeap()/4){
                        char *pBuf=GetTempMemory(FileSize);
                        if(filesource[fn]!=FLASHFILE)  f_read(FileTable[fn].fptr, pBuf, FileSize, &n_read);
                        else n_read=lfs_file_read(&lfs, FileTable[fn].lfsptr, pBuf, FileSize);
                        state->buffer_sent[pcb]=(unsigned char *)pBuf;
                        while(n_read>TCP_MSS){
                                state->to_send[pcb]=TCP_MSS;
                                state->total_sent[pcb]+=TCP_MSS;
                                tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                                //DEBUG_printf("sending page to pcb %d\r\n",pcb);
                                n_read-=TCP_MSS;
                                state->buffer_sent[pcb]+=TCP_MSS;
                        }
                        state->to_send[pcb]=n_read;
                        state->total_sent[pcb]+=n_read;
                        tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                        //DEBUG_printf("sending page end to pcb %d\r\n",pcb);
                        checksent(state,0, pcb);
                } else {
                        char *pBuf=GetTempMemory(TCP_MSS);
                        while(1) {
                                if(
                                ((filesource[fn]==FLASHFILE) && (lfs_file_tell(&lfs,FileTable[fn].lfsptr)==lfs_file_size(&lfs,FileTable[fn].lfsptr)))
                                || ((filesource[fn]!=FLASHFILE) && f_eof(FileTable[fn].fptr))
                                ) break;
                                if(filesource[fn]!=FLASHFILE)  f_read(FileTable[fn].fptr, pBuf, TCP_MSS, &n_read);
                                else n_read=lfs_file_read(&lfs, FileTable[fn].lfsptr, pBuf, TCP_MSS);
                                state->to_send[pcb]=n_read;
                                state->buffer_sent[pcb]=(unsigned char *)pBuf;
                                state->total_sent[pcb]+=n_read;
                                //DEBUG_printf("sending file content to pcb %d\r\n",pcb);
                                tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                                checksent(state,fn, pcb);
                                state->total_sent[pcb]=0;
                                state->sent_len[pcb]=0;
                        }
                }
                tcp_server_close(state,pcb);
//                {if(startupcomplete)ProcessWeb(0);}
                FileClose(fn);
        } else {
                state->to_send[pcb]= state->total_sent[pcb] = strlen(httpheadersfail);
                state->buffer_sent[pcb]=(unsigned char *)httpheadersfail;
                tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                checksent(state,0, pcb);
        }
        tcp_server_close(state,pcb);
//        {if(startupcomplete)ProcessWeb(0);}
        return;
    }
    if((tp=checkstring(cmd, (unsigned char *)"PAGE"))){
	MMFLOAT f;
        int32_t fn;
        int64_t i64;
         int i;
        int t;
        char *fname;
        char c;
        char vartest[MAXVARLEN];
        int vartestp;
        int FileSize;
        int buffersize=4096;
        char p[10]={0};
    	getargs(&tp, 5, (unsigned char *)",");
        if(argc<3)error("Argument count");
        char *outstr=GetTempMemory(STRINGSIZE);
        char *valbuf=GetTempMemory(STRINGSIZE);
        strcat(outstr,httpheaders);
        strcat(outstr,httphtml);
        TCP_SERVER_T *state = (TCP_SERVER_T*)TCPstate;
        int pcb = getint(argv[0],1,MaxPcb)-1;
        fname=(char *)getCstring(argv[2]);
        if(*fname == 0) error("Cannot find file");
        if(argc==5)buffersize=getint(argv[4],0,heap_memory_size);
        if (ExistsFile(fname)) {
                fn = FindFreeFileNbr();
                if (!BasicFileOpen(fname, fn, FA_READ)) return;
                if(filesource[fn]!=FLASHFILE) FileSize = f_size(FileTable[fn].fptr);
                else FileSize = lfs_file_size(&lfs,FileTable[fn].lfsptr);
                char *SocketOut=GetMemory(FileSize+buffersize);
                int SocketOutPointer=0;
                while(1) {
                        if(FileEOF(fn)) break;
                        c=FileGetChar(fn);
                        if(c==26)continue; //deal with xmodem padding
                        if(SocketOutPointer>FileSize+256)break;
                        if(c=='{'){ //start of variable definition
                                vartestp=0;
                                while(c!='}'){
                                        c=FileGetChar(fn);
                                        if(vartestp==0 && c=='{') break;
                                        if(c!='}')vartest[vartestp++]=c;
                                }
                                if(c=='{')SocketOut[SocketOutPointer++]=c; 
                                else {
                                        vartest[vartestp]=0;
                                        unsigned char *s, *st, *temp_tknbuf;
                                        temp_tknbuf = GetMemory(STRINGSIZE);
                                        strcpy((char *)temp_tknbuf, (char *)tknbuf);                                    // first save the current token buffer in case we are in immediate mode
                                        // we have to fool the tokeniser into thinking that it is processing a program line entered at the console
                                        st = GetMemory(STRINGSIZE);
                                        inpbuf[0] = 'r'; inpbuf[1] = '=';                               // place a dummy assignment in the input buffer to keep the tokeniser happy
                                        strcpy((char *)inpbuf + 2, (char *)vartest);
                                        tokenise(true);                                                 // and tokenise it (the result is in tknbuf)
                                        strcpy((char *)st, (char *)(tknbuf + 2 + sizeof(CommandToken)));
                                        t = T_NOTYPE;
                                        int os=OptionExplicit;
                                        OptionExplicit = false;
                                        evaluate(st, &f, &i64, &s, &t, false);
                                        OptionExplicit = os;
                                        if(t & T_NBR) {
                                                FloatToStr(valbuf, f, 0, STR_AUTO_PRECISION, ' ');   // set the string value to be saved
                                                for(i=0;i<strlen(valbuf);i++)SocketOut[SocketOutPointer++]=valbuf[i];
                                        } else if(t & T_INT) {
                                                IntToStr(valbuf, i64, 10); // if positive output a space instead of the sign
                                                for(i=0;i<strlen(valbuf);i++)SocketOut[SocketOutPointer++]=valbuf[i];
                                        } else if(t & T_STR) {
                                                for(i=1;i<=s[0];i++)SocketOut[SocketOutPointer++]=s[i];
                                        } 
                                        strcpy((char *)tknbuf, (char *)temp_tknbuf);// restore the saved token buffer
                                        FreeMemory(temp_tknbuf) ;
                                        FreeMemory(st);
                                }
                        } else 
                        SocketOut[SocketOutPointer++]=c;
                }
                FileClose(fn);
                SocketOut[SocketOutPointer++]=10;
                SocketOut[SocketOutPointer++]=13;
                SocketOut[SocketOutPointer++]=10;
                SocketOut[SocketOutPointer++]=13;
                SocketOut[SocketOutPointer]=0;
                IntToStr(p,strlen(SocketOut),10);
                strcat(outstr,p);
                strcat(outstr,httpend);
//
                if(setjmp(recover) != 0)error("Transmit failed");
                state->to_send[pcb] = state->total_sent[pcb] = strlen(outstr);
                state->sent_len[pcb]=0;
                state->buffer_sent[pcb]=(unsigned char *)outstr;
                tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                //DEBUG_printf("sending page header to pcb %d\r\n",pcb);
//
//                state->to_send[pcb]=strlen(SocketOut);
                state->buffer_sent[pcb]=(unsigned char *)SocketOut;
                int bufflen=strlen(SocketOut);
                while(bufflen>TCP_MSS){
                        state->to_send[pcb]=TCP_MSS;
                        state->total_sent[pcb]+=TCP_MSS;
                        tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                        //DEBUG_printf("sending page to pcb %d\r\n",pcb);
                        bufflen-=TCP_MSS;
                        state->buffer_sent[pcb]+=TCP_MSS;
                }
                state->to_send[pcb]=bufflen;
                state->total_sent[pcb]+=bufflen;
                tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                //DEBUG_printf("sending page end to pcb %d\r\n",pcb);
                checksent(state,0, pcb);
//
                FreeMemory((void *)SocketOut);
        } else {
                state->to_send[pcb] = state->total_sent[pcb] = strlen(httpheadersfail);
                state->sent_len[pcb]=0;
                state->buffer_sent[pcb]=(unsigned char *)httpheadersfail;
                tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                checksent(state,0,pcb);
        }
        tcp_server_close(state,pcb);
//        ProcessWeb(0);
        return;
    }
    error("Invalid option");
}
void open_tcp_server(void){
        tcp_server_init();
        TCP_PORT=Option.TCP_PORT;
        if (!TCPstate) {
                MMPrintString("Failed to create TCP server\r\n");
        }
        if (!tcp_server_open(TCPstate)) {
                MMPrintString("Failed to create TCP server\r\n");
        }
        return;
}

int cmd_tcpserver(void){
    unsigned char *tp;
            tp=checkstring(cmdline, (unsigned char *)"TCP INTERRUPT");
        if(tp){
                getargs(&tp, 1, (unsigned char *)",");
                if(argc!=1)error("Syntax");
                TCPreceiveInterrupt= (char *)GetIntAddress(argv[0]);
                InterruptUsed=true;
                TCPreceived=0;
                return 1;
        }
        tp=checkstring(cmdline, (unsigned char *)"TCP CLOSE");
        if(tp){
                TCP_SERVER_T *state = TCPstate;
                getargs(&tp, 1, (unsigned char *)",");
                if(argc!=1)error("Syntax");
                int pcb = getint(argv[0],1,MaxPcb)-1;
                tcp_server_close(state, pcb) ;
                return 1;
        }
        tp=checkstring(cmdline, (unsigned char *)"TRANSMIT");
        if(tp){
                cmd_transmit(tp);
        
                return 1;
        }

        tp=checkstring(cmdline, (unsigned char *)"TCP READ");
        if(tp){
                void *ptr1 = NULL;
                int64_t *dest=NULL;
                uint8_t *q=NULL;
                int size=0;
                TCP_SERVER_T *state = TCPstate;
                getargs(&tp, 3, (unsigned char *)",");
                if(argc!=3)error("Syntax");
                int pcb=getint(argv[0],1,MaxPcb)-1;
                ptr1 = findvar(argv[2], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
                if(g_vartbl[g_VarIndex].type & T_INT) {
                        if(g_vartbl[g_VarIndex].dims[1] != 0) error("Invalid variable");
                        if(g_vartbl[g_VarIndex].dims[0] <= 0) {      // Not an array
                                error("Argument 2 must be integer array");
                        }
                        size=(g_vartbl[g_VarIndex].dims[0] - g_OptionBase +1)*8;
                        dest = (long long int *)ptr1;
                        dest[0]=0;
                        q=(uint8_t *)&dest[1];
                } else error("Argument 2 must be integer array");
                if(!state->inttrig[pcb]){
                        memset(ptr1,0,size);
                        return 1;
                }
                if(size-8<state->recv_len[pcb])error("array too small");
                memcpy(q,state->buffer_recv[pcb],state->recv_len[pcb]);
                dest[0]=  state->recv_len[pcb];             
                state->inttrig[pcb]=0;
                return 1;
        }
        tp=checkstring(cmdline, (unsigned char *)"TCP SEND");
        if(tp){
                TCP_SERVER_T *state = (TCP_SERVER_T*)TCPstate;
                int64_t *dest=NULL;
                uint8_t *q=NULL;
                getargs(&tp, 3, (unsigned char *)",");
                if(!TCPstate)error("Server not open");
                if(argc != 3)error("Argument count");
                int pcb = getint(argv[0],1,MaxPcb)-1;
                parseintegerarray(argv[2],&dest,2,1,NULL,false);
                q=(uint8_t *)&dest[1];
//                int j=(g_vartbl[g_VarIndex].dims[0] - g_OptionBase);
                state->buffer_sent[pcb]=q;
                int bufflen=dest[0];
                state->to_send[pcb] = state->total_sent[pcb] = state->sent_len[pcb]=0;
                while(bufflen>TCP_MSS){
                        state->to_send[pcb]=TCP_MSS;
                        state->total_sent[pcb]+=TCP_MSS;
                        tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                        //DEBUG_printf("sending page to pcb %d\r\n",pcb);
                        bufflen-=TCP_MSS;
                        state->buffer_sent[pcb]+=TCP_MSS;
                }
                state->to_send[pcb]=bufflen;
                state->total_sent[pcb]+=bufflen;
                tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                //DEBUG_printf("sending page end to pcb %d\r\n",pcb);
                checksent(state,0, pcb);
//                tcp_server_close(state,pcb);
//                ProcessWeb(0);
                return 1;   
        }
        return 0;
}
