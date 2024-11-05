/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Generally you would define your own explicit list of lwIP options
// (see https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html)
//
// This example uses a common include to avoid repetition
#include "lwipopts_examples_common.h"
#include "ffconf.h"
//#undef TCP_WND
//#define TCP_WND  16384

//#define LWIP_ALTCP               1
//#define LWIP_ALTCP_TLS           1
//#define LWIP_ALTCP_TLS_MBEDTLS   1
#define DNS_TABLE_SIZE           1
//#define IP_SOF_BROADCAST         1
//#define IP_SOF_BROADCAST_RECV    1
#define LWIP_DEBUG 1
#define TFTP_MAX_FILENAME_LEN FF_MAX_LFN
//#define ALTCP_MBEDTLS_DEBUG  LWIP_DBG_ON
#define MEMP_NUM_SYS_TIMEOUT   (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 3) // <-------- +1 for MQTT
extern int MMerrno;
extern char MMErrMsg[]; 
#define LWIP_PLATFORM_DIAG(x) {MMerrno=17;}
#include <stdio.h>
#include <stdlib.h>
#endif
/*  @endcond */
