/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "lwip/udp.h"
#include "lwip/timeouts.h"
#include "lwip/debug.h"
struct udp_pcb *udppcb = NULL, *sendudppcb=NULL;
typedef struct UDP_T_ {
    ip_addr_t udp_server_address;
    struct udp_pcb *ntp_pcb;
    volatile bool complete;
} UDP_T;
UDP_T UDPstate;

static void
udp_recv_func(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
LWIP_UNUSED_ARG(arg);
  if(p == NULL)	return;
  int len = p->len;
  if(len>=sizeof(messagebuff))len=sizeof(messagebuff)-1;
  memset(messagebuff,0,sizeof(messagebuff));
  memcpy(&messagebuff[1],p->payload,len);
  messagebuff[0]=len;
  memset(addressbuff,0,sizeof(addressbuff));
  sprintf((char *)&addressbuff[1],"%d.%d.%d.%d",(int)addr->addr&0xff,(int)(addr->addr>>8)&0xff,(int)(addr->addr>>16)&0xff,(int)(addr->addr>>24)&0xff);
  addressbuff[0]=strlen((char *)&addressbuff[1]);
//	udp_sendto(upcb, p, addr, port);
  pbuf_free(p);
  UDPreceive=1;

}
void udp_server_init(void){
    udppcb=udp_new();
    ip_set_option(udppcb, SOF_BROADCAST);
//    err_t ret;
    if(Option.UDP_PORT && WIFIconnected && !optionsuppressstatus){
        MMPrintString("Starting UDP server at ");
        MMPrintString(ip4addr_ntoa(netif_ip4_addr(netif_list)));
        MMPrintString(" on port ");
        PInt(Option.UDP_PORT);PRet();
    }


  udp_bind(udppcb, IP_ADDR_ANY, Option.UDP_PORT);

  udp_recv(udppcb, udp_recv_func, udppcb);
}

void open_udp_server(void){
    udp_server_init();
    return;
}
static void udp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    UDP_T *state = (UDP_T*)arg;
    if (ipaddr) {
        state->udp_server_address = *ipaddr;
        state->complete=1;
    } else {
        free(state);
        error("udp dns request failed");
    }
}

void cmd_udp(unsigned char *p){
    unsigned char *tp;
    tp=checkstring(p, (unsigned char *)"INTERRUPT");
    if(tp){
        getargs(&tp, 1, (unsigned char *)",");
        if(argc!=1)error("Syntax");
        UDPinterrupt=(char *)GetIntAddress(argv[0]);
        InterruptUsed=true;
        UDPreceive=0;
        return;
    }
    tp=checkstring(p, (unsigned char *)"SEND");
    if(tp){
        getargs(&tp, 5, (unsigned char *)",");
        if(argc!=5)error("Syntax");
        UDP_T *state = &UDPstate;
        state->complete=0;
        ip4_addr_t remote_addr;
        int timeout=5000;
        char *IP=GetTempMemory(STRINGSIZE);
        strcpy(IP,(char *)getCstring(argv[0]));
        if(!isalpha((uint8_t)*IP) && strchr(IP,'.') && strchr(IP,'.')<IP+4){
            if(!ip4addr_aton(IP, &remote_addr))error("Invalid address format");
            state->udp_server_address=remote_addr;
        } else {
            int err = dns_gethostbyname(IP, &remote_addr, udp_dns_found, state);
            if(err==ERR_OK)state->udp_server_address=remote_addr;
            else if(err==ERR_INPROGRESS){
                Timer4=timeout;
                while(!state->complete && Timer4 && !(err==ERR_OK))if(startupcomplete)cyw43_arch_poll();
                if(!Timer4)error("Failed to convert web address");
            } else error("Failed to find UDP address");
        }
        unsigned char *data=getstring(argv[4]);
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (uint16_t)data[0], PBUF_RAM);
        char *req = (char *)p->payload;
        memset(req, 0, data[0]);
        int port=getint(argv[2],1,65535);
        memcpy(req,&data[1],data[0]);
        sendudppcb=udp_new();
        ip_set_option(sendudppcb, SOF_BROADCAST);
        err_t er = udp_sendto(sendudppcb, p, &state->udp_server_address, port);
        pbuf_free(p);
        if (er != ERR_OK) {
            printf("Failed to send UDP packet! error=%d", er);
        } 
//        ProcessWeb(0);
        udp_remove(sendudppcb);
        sendudppcb=NULL;
        return;
    }
}
/*  @endcond */
