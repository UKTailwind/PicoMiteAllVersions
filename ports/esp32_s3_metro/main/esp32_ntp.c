#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hal/hal_net.h"
#include "hal/hal_time.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "shared/net/mm_net_ntp.h"
#include "shared/net/mm_net_ntp_hal.h"
#include "esp32_ntp.h"

#define ESP32_NTP_PORT 123

extern int64_t TimeOffsetToUptime;
extern volatile int day_of_week;

void esp32_ntp_cmd(unsigned char *arg)
{
    getargs(&arg, 5, (unsigned char *)",");
    if (!(argc == 0 || argc == 1 || argc == 3 || argc == 5)) error("Syntax");

    MMFLOAT adjust = 0.0;
    if (argc >= 1 && *argv[0]) {
        adjust = getnumber(argv[0]);
        if (adjust < -12.0 || adjust > 14.0) error("Invalid Time Offset");
    }
    char *host = GetTempMemory(STRINGSIZE);
    if (argc >= 3 && *argv[2]) strcpy(host, (char *)getCstring(argv[2]));
    else strcpy(host, "pool.ntp.org");
    uint16_t port = ESP32_NTP_PORT;
    char *colon = strrchr(host, ':');
    if (colon && colon[1]) {
        int parsed_port = atoi(colon + 1);
        if (parsed_port > 0 && parsed_port <= 65535) {
            *colon = 0;
            port = (uint16_t)parsed_port;
        }
    }

    int timeout = 5000;
    if (argc == 5) timeout = getint(argv[4], 0, 100000);

    if (!optionsuppressstatus) {
        char buff[STRINGSIZE];
        snprintf(buff, sizeof buff, "ntp address %s\r\n", host);
        MMPrintString(buff);
    }

    uint32_t unix_seconds = 0;
    int rc = mm_net_ntp_query_unix_seconds(host, port, (uint32_t)timeout,
                                           &unix_seconds);
    if (rc == HAL_NET_TIMEOUT) error("NTP timeout");
    if (rc != HAL_NET_OK) error("NTP request failed");

    int64_t timeadjust = (int64_t)(adjust * 3600.0);
    time_t epoch = (time_t)(unix_seconds + timeadjust);
    struct tm *utc = gmtime(&epoch);
    if (!utc) error("invalid ntp response");

    day_of_week = utc->tm_wday;
    if (day_of_week == 0) day_of_week = 7;
    TimeOffsetToUptime = (int64_t)epoch - (int64_t)(hal_time_us_64() / 1000000);

    if (!optionsuppressstatus) {
        char buff[STRINGSIZE];
        snprintf(buff, sizeof buff,
                 "got ntp response: %02d/%02d/%04d %02d:%02d:%02d\r\n",
                 utc->tm_mday, utc->tm_mon + 1, utc->tm_year + 1900,
                 utc->tm_hour, utc->tm_min, utc->tm_sec);
        MMPrintString(buff);
    }
}
