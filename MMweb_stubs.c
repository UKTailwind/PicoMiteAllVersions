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

/* MM.MESSAGE$ / MM.ADDRESS$ / MM.TOPIC$ buffer accessor. On WEB,
 * port_fun_mm_mqtt_copy() in MMMqtt.c copies from messagebuff /
 * addressbuff / topicbuff. Non-WEB builds have no MQTT state, so this
 * stub writes an empty MMBasic string (length byte zero) and the
 * BASIC function returns "". Kept out of MMweb_stubs.c's larger
 * buffer declarations to avoid ~550 bytes of dead BSS on RAM-tight
 * VGA builds. */
void port_fun_mm_mqtt_copy(int which, unsigned char *out) {
    (void)which;
    out[0] = 0;
    out[1] = 0;
}

/* ClearRuntime TCP-state teardown. WEB real impl in MMtcpserver.c;
 * non-WEB has no TCP state so the hook is a no-op. */
void port_web_clear_runtime_state(void) {}

/* TCP server + client teardown hooks called from Commands.c's cmd_run
 * and cmd_mmcls2; WEB has real impls in MMtcpserver.c / MMTCPclient.c.
 * Non-WEB builds need the symbols defined so Commands.c can call them
 * unconditionally. */
void cleanserver(void) {}
void close_tcpclient(void) {}

void port_web_print_options(void) {}
int  port_web_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }
int  port_web_mminfo(unsigned char *ep, int64_t *out_iret,
                     unsigned char *out_sret, int *out_targ)
{ (void)ep; (void)out_iret; (void)out_sret; (void)out_targ; return 0; }
int  port_web_get_ssid(unsigned char *out_sret, int *out_targ)
{ (void)out_sret; (void)out_targ; return 0; }
