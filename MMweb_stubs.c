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
void TelnetPutC(int c, int flush) { (void)c; (void)flush; }
void WebConnect(void) {}
/* port_repl_wifi_arch_init_and_connect is a no-op on non-WiFi ports;
 * the WiFi init call from MMBasic_REPL.c falls through to nothing. */
void port_repl_wifi_arch_init_and_connect(void) {}
/* WEB cmd token is wired into the AllCommands.h table via the WiFi
 * port_tokens.h palette; non-WiFi ports use a different palette and
 * never invoke this stub. The definition keeps the symbol available
 * if some build path adds the token regardless. */
void cmd_web(void) { error("WEB not supported on this port"); }

/* Whether the WiFi telnet client is configured (Option.Telnet != -1).
 * Real impl on WiFi ports lives in MMtelnet.c; stub returns 1 on
 * non-WiFi so the stdio console path runs unconditionally. */
int wifi_serial_telnet_configured(void) { return 1; }

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

#include "hal/hal_option_setters.h"
#include "hal/hal_pin.h"

/* OPTION PICO ON/OFF — exposes/hides CYW43-shadow pins (41/42/44).
 * RP2350B not supported (no shadow needed). */
int port_setter_pico_pins(unsigned char *cmdline) {
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"PICO");
    if (!tp) return 0;
#ifdef rp2350
    if (!rp2350a) error("Invalid for RP2350B");
#endif
    if (checkstring(tp, (unsigned char *)"OFF") || checkstring(tp, (unsigned char *)"DISABLE"))
        Option.AllPins = 1;
    else if (checkstring(tp, (unsigned char *)"ON") || checkstring(tp, (unsigned char *)"ENABLE"))
        Option.AllPins = 0;
    else error("Syntax");
    SaveOptions();
    if (Option.AllPins == 0) {
        if (CheckPin(41, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(41, EXT_DIG_OUT, Option.PWM);
        if (CheckPin(42, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(42, EXT_DIG_IN, 0);
        if (CheckPin(44, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(44, EXT_ANA_IN, 0);
    } else {
        if (CheckPin(41, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(41, EXT_NOT_CONFIG, 0);
        if (CheckPin(42, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(42, EXT_NOT_CONFIG, 0);
        if (CheckPin(44, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(44, EXT_NOT_CONFIG, 0);
    }
    return 1;
}

/* OPTION HEARTBEAT — non-WiFi ports allow pin reassignment in addition
 * to ON/OFF. */
int port_setter_heartbeat(unsigned char *cmdline) {
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"HEARTBEAT");
    if (!tp) return 0;
    if (checkstring(tp, (unsigned char *)"OFF") || checkstring(tp, (unsigned char *)"DISABLE")) {
        Option.NoHeartbeat = 1;
    } else {
        unsigned char *p = NULL;
        p = checkstring(tp, (unsigned char *)"ON");
        if (p == NULL) p = checkstring(tp, (unsigned char *)"ENABLE");
        if (p) {
            getargs(&p, 1, (unsigned char *)",");
            if (argc) {
                unsigned char code, pin1;
                if (!(code = codecheck(p))) p += 2;
                pin1 = getinteger(p);
                if (!code) pin1 = codemap(pin1);
                if (IsInvalidPin(pin1)) error("Invalid pin");
                if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin1, pin1);
                Option.NoHeartbeat = 0;
                Option.heartbeatpin = pin1;
                SaveOptions();
                _excep_code = RESET_COMMAND;
                SoftReset();
            } else Option.NoHeartbeat = 0;
        } else error("Syntax");
    }
    SaveOptions();
    if (CheckPin(HEARTBEATpin, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) {
        if (Option.NoHeartbeat == 0) {
            hal_pin_set_mode(PinDef[HEARTBEATpin].GPno, HAL_PIN_MODE_OUTPUT);
            ExtCurrentConfig[PinDef[HEARTBEATpin].pin] = EXT_HEARTBEAT;
        } else ExtCfg(HEARTBEATpin, EXT_NOT_CONFIG, 0);
    } else error("Pin %/| is reserved", HEARTBEATpin, HEARTBEATpin);
    return 1;
}
