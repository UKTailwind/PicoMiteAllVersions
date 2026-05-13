/***********************************************************************************************************************
PicoMite MMBasic

custom.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4. All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
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
#include "shared/net/mm_net_ntp_hal.h"
#include "pico/time.h"

#define NTP_SERVER "pool.ntp.org"
#define NTP_PORT 123

static void pico_ntp_apply(uint32_t unix_seconds, MMFLOAT adjust_hours) {
    time_t adjusted = (time_t)unix_seconds + (time_t)(adjust_hours * 3600.0);
    struct tm *utc = gmtime(&adjusted);
    if (!utc) error("invalid ntp response");

    char buff[STRINGSIZE] = {0};
    snprintf(buff, sizeof buff,
             "got ntp response: %02d/%02d/%04d %02d:%02d:%02d\r\n",
             utc->tm_mday, utc->tm_mon + 1, utc->tm_year + 1900,
             utc->tm_hour, utc->tm_min, utc->tm_sec);
    if (!optionsuppressstatus) MMPrintString(buff);

    day_of_week = utc->tm_wday;
    if (day_of_week == 0) day_of_week = 7;
    TimeOffsetToUptime = get_epoch(utc->tm_year + 1900, utc->tm_mon + 1,
                                   utc->tm_mday, utc->tm_hour, utc->tm_min,
                                   utc->tm_sec) -
                         time_us_64() / 1000000;
}

void cmd_ntp(unsigned char *tp) {
    getargs(&tp, 5, (unsigned char *)",");
    if (!(argc == 0 || argc == 1 || argc == 3 || argc == 5)) error("Syntax");

    MMFLOAT adjust = 0.0;
    const char *server = NTP_SERVER;
    int timeout = 5000;
    uint16_t port = NTP_PORT;

    if (argc >= 1 && *argv[0]) {
        adjust = getnumber(argv[0]);
        if (adjust < -12.0 || adjust > 14.0) error("Invalid Time Offset");
    }
    if (argc >= 3 && *argv[2]) server = (const char *)getCstring(argv[2]);
    if (argc == 5 && *argv[4]) timeout = getint(argv[4], 0, 100000);

    char hostbuf[STRINGSIZE];
    strncpy(hostbuf, server, sizeof(hostbuf) - 1);
    hostbuf[sizeof(hostbuf) - 1] = 0;
    char *colon = strrchr(hostbuf, ':');
    if (colon && colon[1]) {
        int parsed_port = atoi(colon + 1);
        if (parsed_port > 0 && parsed_port <= 65535) {
            *colon = 0;
            port = (uint16_t)parsed_port;
        }
    }

    if (!optionsuppressstatus) {
        char buff[STRINGSIZE] = {0};
        snprintf(buff, sizeof buff, "ntp address %s\r\n", hostbuf);
        MMPrintString(buff);
    }

    uint32_t unix_seconds = 0;
    int rc = mm_net_ntp_query_unix_seconds(hostbuf, port, (uint32_t)timeout,
                                           &unix_seconds);
    if (rc == HAL_NET_TIMEOUT) error("NTP timeout");
    if (rc != HAL_NET_OK) error("NTP request failed");
    pico_ntp_apply(unix_seconds, adjust);
}
