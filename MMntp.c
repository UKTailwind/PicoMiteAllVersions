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
NTP_T *NTPstate=NULL;
#define NTP_SERVER "pool.ntp.org"
#define NTP_MSG_LEN 48
#define NTP_PORT 123
#define NTP_DELTA 2208988800 // seconds between 1 Jan 1900 and 1 Jan 1970
#define NTP_TEST_TIME (30 * 1000)
#define NTP_RESEND_TIME (10 * 1000)
volatile time_t timeadjust=0;
volatile uint8_t seconds_buf[4] = {0};
// Called with results of operation
static void ntp_result(NTP_T* state, int status, time_t *result) {
    if (status == 0 && result) {
        int year, month, day, hour, minute, second;
        *result=*result+timeadjust;
        struct tm *utc = gmtime(result);
        char buff[STRINGSIZE]={0};
        sprintf(buff,"got ntp response: %02d/%02d/%04d %02d:%02d:%02d\r\n", utc->tm_mday, utc->tm_mon + 1, utc->tm_year + 1900,
               utc->tm_hour, utc->tm_min, utc->tm_sec);
        if(!optionsuppressstatus)MMPrintString(buff);
//        mT4IntEnable(0);
        hour = utc->tm_hour;
        minute = utc->tm_min;
        second = utc->tm_sec;
        day_of_week=utc->tm_wday;
        if(day_of_week==0)day_of_week=7;
        year = utc->tm_year + 1900;
        month = utc->tm_mon + 1;
        day = utc->tm_mday;
//        mT4IntEnable(1);
        TimeOffsetToUptime=get_epoch(year, month, day, hour, minute, second)-time_us_64()/1000000;
    }
}

//static int64_t ntp_failed_handler(alarm_id_t id, void *user_data);

// Make an NTP request
static void ntp_request(NTP_T *state) {
    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
    uint8_t *req = (uint8_t *) p->payload;
    memset(req, 0, NTP_MSG_LEN);
    req[0] = 0x1b;
    udp_sendto(state->ntp_pcb, p, &state->ntp_server_address, NTP_PORT);
    pbuf_free(p);
}

/*static int64_t ntp_failed_handler(alarm_id_t id, void *user_data)
{
    NTP_T* state = (NTP_T*)user_data;
    free(state);
    error("ntp request failed");
    return 0;
}*/

// Call back with a DNS result
static void ntp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    NTP_T *state = (NTP_T*)arg;
    if (ipaddr) {
        state->ntp_server_address = *ipaddr;
        state->complete=1;
    } else {
        free(state);
        error("ntp dns request failed");
    }
}

// NTP data received
static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    NTP_T *state = (NTP_T*)arg;
    uint8_t mode = pbuf_get_at(p, 0) & 0x7;
    uint8_t stratum = pbuf_get_at(p, 1);

    // Check the result
    if (ip_addr_cmp(addr, &state->ntp_server_address) && port == NTP_PORT && p->tot_len == NTP_MSG_LEN &&
        mode == 0x4 && stratum != 0) {
        pbuf_copy_partial(p, (void *)seconds_buf, sizeof(seconds_buf), 40);
//        uSec(200);
        state->complete=1;
    } else {
        pbuf_free(p);
        free(state);
        error("invalid ntp response");
    }
    pbuf_free(p);
}

// Perform initialisation
static NTP_T* ntp_init(void) {
    NTPstate = (NTP_T *)calloc(1,sizeof(NTP_T));
    NTP_T *state=NTPstate;
    if (!state) {
        error("failed to allocate state\n");
        return NULL;
    }
    return state;
}

void cmd_ntp(unsigned char *tp){
            getargs(&tp,5,(unsigned char *)",");
            NTP_T *state = ntp_init();
            int timeout=5000;
            if (!state) error("Can't create NTP structure");
            ip4_addr_t remote_addr;
            char *IP=GetTempMemory(STRINGSIZE);
            if (argc >=1){
                MMFLOAT adjust = getnumber(argv[0]);
                if (adjust < -12.0 || adjust > 14.0) error("Invalid Time Offset");
                timeadjust=(time_t)(adjust*3600.0);
            } else timeadjust=0;
            if(argc>=3 && *argv[2])strcpy(IP,(char *)getCstring(argv[2]));
            else strcpy(IP,NTP_SERVER);
            if(argc==5)timeout=getint(argv[4],0,100000);
            if(!isalpha((uint8_t)*IP) && strchr(IP,'.') && strchr(IP,'.')<IP+4){
                    if(!ip4addr_aton(IP, &remote_addr))error("Invalid address format");
                    state->ntp_server_address=remote_addr;
            } else {
                    int err = dns_gethostbyname(IP, &remote_addr, ntp_dns_found, state);
                    if(err==ERR_OK)state->ntp_server_address=remote_addr;
                    else if(err==ERR_INPROGRESS){
                        Timer4=timeout;
                        while(!state->complete && Timer4 && !(err==ERR_OK))if(startupcomplete)cyw43_arch_poll();
                        if(!Timer4)error("Failed to convert web address");
                        state->complete=0;
                    } else error("Failed to find NTP address");
            }
            char buff[STRINGSIZE]={0};
            sprintf(buff,"ntp address %s\r\n", ip4addr_ntoa(&state->ntp_server_address));
            if(!optionsuppressstatus)MMPrintString(buff);
            memset((void *)seconds_buf, 0, sizeof(seconds_buf));
            state->ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
            if (!state->ntp_pcb) {
                free((void *)state);
                error("failed to create pcb\n");
            } else udp_recv(state->ntp_pcb, ntp_recv, state);
            ntp_request(state);
            Timer4=timeout;
            while(!state->complete){
                if(startupcomplete)cyw43_arch_poll();
                if(!Timer4){
                        udp_remove(NTPstate->ntp_pcb);
//                        memset(NTPstate,0,sizeof(NTPstate));
                        free(NTPstate);
                        error("NTP timeout");
                }
            }
            uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
            if(seconds_since_1900){
                uint32_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
                time_t epoch = seconds_since_1970;
                ntp_result(state, 0, &epoch);
            }
            udp_remove(NTPstate->ntp_pcb);
//            memset(NTPstate,0,sizeof(NTPstate));
            free(NTPstate);
}