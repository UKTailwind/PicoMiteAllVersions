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

#if LWIP_TCP

/** Define this to a compile-time IP address initialization
 * to connect anything else than IPv4 loopback
 */
#ifndef LWIP_MQTT_EXAMPLE_IPADDR_INIT
#if LWIP_IPV4
#define LWIP_MQTT_EXAMPLE_IPADDR_INIT = IPADDR4_INIT(PP_HTONL(0x144F466D))
#else
#define LWIP_MQTT_EXAMPLE_IPADDR_INIT
#endif
#endif
#undef LWIP_PLATFORM_DIAG
#define LWIP_PLATFORM_DIAG
enum {
  TCP_DISCONNECTED,
  TCP_CONNECTING,
  MQTT_CONNECTING,
  MQTT_CONNECTED
};

//static ip_addr_t mqtt_ip LWIP_MQTT_EXAMPLE_IPADDR_INIT;

static mqtt_client_t* mqtt_client=NULL;
unsigned char topicbuff[STRINGSIZE]={0};
unsigned char messagebuff[STRINGSIZE]={0};
unsigned char addressbuff[20]={0};
typedef struct mqtt_dns_t_ {
        ip_addr_t remote;
        int complete;
} mqtt_dns_t;

mqtt_dns_t mqtt_dns = {
        {0},
        0
}; 
typedef struct mqtt_subs_t_ {
        char topic[STRINGSIZE];
        int complete;
} mqtt_subs_t;
mqtt_subs_t mqtt_subs={
  "",
  0
};
static void mqtt_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    mqtt_dns_t *dns=(mqtt_dns_t *)arg;
    if (ipaddr) {
        dns->remote = *ipaddr;
        dns->complete=1;
        char buff[STRINGSIZE]={0};
        sprintf(buff,"tcp address %s\r\n", ip4addr_ntoa(ipaddr));
        if(!optionsuppressstatus)MMPrintString(buff);
    } 
} 

struct mqtt_connect_client_info_t mqtt_client_info =
{
  NULL,
  NULL, /* user */
  NULL, /* pass */
  100,  /* keep alive */
  NULL, /* will_topic */
  NULL, /* will_msg */
  1,    /* will_qos */
  1    /* will_retain */

#if LWIP_ALTCP && LWIP_ALTCP_TLS
  , NULL
#endif
};

static void
mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    int mylen=len;
    if(mylen>255)mylen=255;
    memset(messagebuff,0,sizeof(messagebuff));
    memcpy(&messagebuff[1],data,mylen);
    messagebuff[0]=mylen;
    MQTTComplete=1;
}

static void
mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    int mylen=strlen(topic);
    if(mylen>255)mylen=255;
    memset(topicbuff,0,sizeof(topicbuff));
    memcpy(&topicbuff[1],topic,mylen);
    topicbuff[0]=mylen;
}

static void
mqtt_submessage_cb(void *arg, err_t err)
{
  mqtt_subs_t *state=(mqtt_subs_t *)arg;
  state->complete=1;
}
static void
mqtt_unsubmessage_cb(void *arg, err_t err)
{
  mqtt_subs_t *state=(mqtt_subs_t *)arg;
  state->complete=1;
}

static void
mqtt_request_cb(void *arg, err_t err)
{
//  const struct mqtt_connect_client_info_t* client_info = (const struct mqtt_connect_client_info_t*)arg;

//  LWIP_PLATFORM_DIAG(("MQTT client \"%s\" request cb: err %d\n", client_info->client_id, (int)err));
}

static void
mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
//  const struct mqtt_connect_client_info_t* client_info = (const struct mqtt_connect_client_info_t*)arg;
//  LWIP_UNUSED_ARG(client);

//  LWIP_PLATFORM_DIAG(("MQTT client \"%s\" mqtt connection cb: status %d\n", client_info->client_id, (int)status));
}
#endif /* LWIP_TCP */
void
mqtt_example_init(ip_addr_t *mqtt_ip, int port)
{
#if LWIP_TCP
//mqtt_connection_status_t status;
  mqtt_client = mqtt_client_new();
  mqtt_client->keep_alive=100;
    if(!optionsuppressstatus)printf("Connecting to %s port %u\r\n", ip4addr_ntoa(mqtt_ip), port);

  mqtt_client_connect(mqtt_client,
          mqtt_ip, port,
          mqtt_connection_cb, &mqtt_client_info,
          &mqtt_client_info);
  Timer4=5000;
  while(Timer4 && mqtt_client->conn_state != MQTT_CONNECTED)if(startupcomplete)cyw43_arch_poll();
  if(Timer4==0)error("Failed to connect");
  mqtt_set_inpub_callback(mqtt_client,
          mqtt_incoming_publish_cb,
          mqtt_incoming_data_cb,
          &mqtt_client_info);

#endif /* LWIP_TCP */
}
void closeMQTT(void){
    if(mqtt_client){
      mqtt_disconnect(mqtt_client);
      mqtt_client_free(mqtt_client);
      mqtt_client=NULL;
    }

}
//static struct altcp_tls_config *tls_config = NULL;

int cmd_mqtt(void){
    unsigned char *tp=checkstring(cmdline,(unsigned char *)"MQTT CONNECT");
    if(tp){
        getargs(&tp,9,(unsigned char *)",");
        char *IP=GetTempMemory(STRINGSIZE);
        char *ID=GetTempMemory(STRINGSIZE);
        if(mqtt_client)error("Already connected");
        int timeout=5000;
        if(!(argc==7 || argc==9))error("Syntax");
        IP=(char *)getCstring(argv[0]);
        int port=getint(argv[2],1,65535);
        ip4_addr_t remote_addr;
//        if(port==8883){
//          tls_config = altcp_tls_create_config_client(NULL, 0);
//          mqtt_client_info.tls_config=tls_config;
//        }
        mqtt_client_info.client_user=(char *)getCstring(argv[4]);
        mqtt_client_info.client_pass=(char *)getCstring(argv[6]);
        if(argc==9){
          MQTTInterrupt=(char *)GetIntAddress(argv[8]);
          InterruptUsed=true;
        }
        else MQTTInterrupt=NULL;
        MQTTComplete=0;
        strcpy(ID,"WebMite");
        strcat(ID,id_out);
        IntToStr(&ID[strlen(ID)],time_us_64(),16);
        mqtt_client_info.client_id=ID;

        if(!isalpha((uint8_t)*IP) && strchr(IP,'.') && strchr(IP,'.')<IP+4){
                if(!ip4addr_aton(IP, &remote_addr))error("Invalid address format");
                mqtt_dns.remote=remote_addr;
        } else {
                int err = dns_gethostbyname(IP, &remote_addr, mqtt_dns_found, &mqtt_dns);
                Timer4=timeout;
                while(!mqtt_dns.complete && Timer4 && !(err==ERR_OK))if(startupcomplete)cyw43_arch_poll();
                if(!Timer4)error("Failed to convert web address");
                mqtt_dns.complete=0;
        }
        mqtt_example_init(&mqtt_dns.remote,port);
        return 1;
    }
    tp=checkstring(cmdline,(unsigned char *)"MQTT PUBLISH");
    if(tp){
        getargs(&tp,7,(unsigned char *)",");
        int qos=1;
        int retain=1;
        if(argc<3)error("Syntax");
        char *topic=(char *)getCstring(argv[0]);
        char *msg=(char *)getCstring(argv[2]);
        if(argc>=5 && *argv[4])qos=getint(argv[4],0,2);
        if(argc==7)retain=getint(argv[6],0,1);
        mqtt_publish(mqtt_client, topic, msg, strlen(msg), qos, retain, mqtt_request_cb , (void *)&mqtt_client_info);
      return 1;
    }
    tp=checkstring(cmdline,(unsigned char *)"MQTT SUBSCRIBE");
    if(tp){
      getargs(&tp,3,(unsigned char *)",");
      if(!(argc>=1))error("Syntax");
      strcpy(mqtt_subs.topic,(char *)getCstring(argv[0]));
      int qos=0;
      if(argc==3)qos=getint(argv[2],0,2);
      mqtt_subs.complete=0;
      Timer4=4000;
      mqtt_sub_unsub(mqtt_client, mqtt_subs.topic, qos, mqtt_submessage_cb, &mqtt_subs, 1);
      while(Timer4 && !mqtt_subs.complete)if(startupcomplete)cyw43_arch_poll();
      if(Timer4==0)error("Failed to subscribe to $",mqtt_subs.topic);
      return 1;
    }
    tp=checkstring(cmdline,(unsigned char *)"MQTT UNSUBSCRIBE");
    if(tp){
      strcpy(mqtt_subs.topic,(char *)getCstring(tp));
      mqtt_subs.complete=0;
      Timer4=4000;
      mqtt_sub_unsub(mqtt_client, mqtt_subs.topic, 0, mqtt_unsubmessage_cb, &mqtt_subs, 0);
      while(Timer4 && !mqtt_subs.complete)if(startupcomplete)cyw43_arch_poll();
      if(Timer4==0)error("Failed to unsubscribe to $",mqtt_subs.topic);
      return 1;
    }
    tp=checkstring(cmdline,(unsigned char *)"MQTT CLOSE");
    if(tp){
      if(!mqtt_client)return 1;
      closeMQTT();
      return 1;
    }
    return 0;
}