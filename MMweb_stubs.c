/*
 * MMweb_stubs.c — no-op WEB-stack stubs for non-WEB device builds.
 * Linked when COMPILE != WEB / WEBRP2350 (CMakeLists). Real impls
 * live in MMsetwifi.c, MMMqtt.c, MMtcpserver.c on WEB. Host gets the
 * same stubs in host_runtime.c.
 *
 * MM_Misc.c reads the WEB-stack symbols (closeMQTT, ProcessWeb,
 * tcp_*_recv_buffers, port_web_*) unconditionally so its source stays
 * preprocessor-clean.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void closeMQTT(void) {}
void ProcessWeb(int mode) { (void)mode; }
int  startupcomplete = 0;
void tcp_free_recv_buffers(void) {}
void tcp_realloc_recv_buffers(void) {}

void port_web_print_options(void) {}
int  port_web_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }
int  port_web_mminfo(unsigned char *ep, int64_t *out_iret,
                     unsigned char *out_sret, int *out_targ)
{ (void)ep; (void)out_iret; (void)out_sret; (void)out_targ; return 0; }
int  port_web_get_ssid(unsigned char *out_sret, int *out_targ)
{ (void)out_sret; (void)out_targ; return 0; }
