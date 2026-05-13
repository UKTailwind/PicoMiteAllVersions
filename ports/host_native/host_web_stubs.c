/*
 * ports/host_native/host_web_stubs.c - no-network WEB/telnet surface
 * for host-derived ports that reuse host_runtime.c but do not link the
 * real host web server.
 */

#include <stdint.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void closeMQTT(void) {}
void ProcessWeb(int mode) { (void)mode; }
void host_telnet_putc(int c, int flush) { (void)c; (void)flush; }

void cmd_web(void) { error("WEB not supported on this port"); }

void port_web_clear_runtime_state(void) {}
void cleanserver(void) {}
void close_tcpclient(void) {}

void port_web_print_options(void) {}
int port_web_option_setter(unsigned char *cmdline)
{
    (void)cmdline;
    return 0;
}

int port_web_mminfo(unsigned char *ep, int64_t *out_iret,
                    unsigned char *out_sret, int *out_targ)
{
    (void)ep;
    (void)out_iret;
    (void)out_sret;
    (void)out_targ;
    return 0;
}

int port_web_get_ssid(unsigned char *out_sret, int *out_targ)
{
    (void)out_sret;
    (void)out_targ;
    return 0;
}
