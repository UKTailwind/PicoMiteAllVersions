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

/* altcp is always on for PICOMITEWEB so the TCP client code path is the
   same whether TLS is compiled in or not — altcp_tcp_* on plain TCP, and
   altcp_tls_* on TLS sessions when PICOMITEWEB_TLS is defined. */
#define LWIP_ALTCP               1
#ifdef PICOMITEWEB_TLS
#define LWIP_ALTCP_TLS           1
#define LWIP_ALTCP_TLS_MBEDTLS   1
/* mbedtls allocations are routed through lwIP's mem_malloc, capped at
   MEM_SIZE. tls_malloc in altcp_tls_mbedtls_mem.c rejects any request
   bigger than MEM_SIZE, so this must hold the largest single allocation
   (the 8 KB TLS IN buffer + ~256 bytes overhead) plus the rest of the
   session state. Peak ~22 KB for one TLS session; 40 KB gives headroom
   for X.509 cert chain parsing transients. */
#define MEM_SIZE                 40960
#endif
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
