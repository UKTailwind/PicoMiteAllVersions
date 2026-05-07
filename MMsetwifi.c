/*
 * MMsetwifi.c — WEB-only OPTION handlers + MM.* info dispatch +
 * printoptions block. Compiled only on PICOMITEWEB builds (gated in
 * CMakeLists). Lifted out of MM_Misc.c so its OPTION setters and
 * fun_mminfo paths stay preprocessor-clean. Stubs for non-WEB live in
 * host_runtime.c (host) and ports/pico_sdk_common/cmd_files_hooks.c
 * (device, since cmd_files_hooks.c is the device-side companion to
 * the host-runtime stubs). Real impls below set `iret`/`sret`/`targ`
 * the same way the original inline code did.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "wifi_includes.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#define CJSON_NESTING_LIMIT 100
#include "cJSON.h"

extern char id_out[12];
extern bool optionsuppressstatus;
extern void setwifi(unsigned char *tp);

void setwifi(unsigned char *tp)
{
    getargs(&tp, 11, (unsigned char *)",");
    if (!(argc == 3 || argc == 5 || argc == 11)) error("Syntax");
    if (CurrentLinePtr) error("Invalid in a program");
    char *ssid     = GetTempMemory(STRINGSIZE);
    char *password = GetTempMemory(STRINGSIZE);
    char *hostname = GetTempMemory(STRINGSIZE);
    char *ipaddress = GetTempMemory(STRINGSIZE);
    char *mask     = GetTempMemory(STRINGSIZE);
    char *gateway  = GetTempMemory(STRINGSIZE);
    strcpy(ssid,     (char *)getCstring(argv[0]));
    strcpy(password, (char *)getCstring(argv[2]));
    if (strlen(ssid)     > MAXKEYLEN - 1) error("SSID too long, max 63 chars");
    if (strlen(password) > MAXKEYLEN - 1) error("Password too long, max 63 chars");
    if (argc == 11) {
        strcpy(ipaddress, (char *)getCstring(argv[6]));
        strcpy(mask,      (char *)getCstring(argv[8]));
        strcpy(gateway,   (char *)getCstring(argv[10]));
        ip4_addr_t ipaddr;
        if (!ip4addr_aton(ipaddress, &ipaddr)) error("Invalid IP address");
        if (!ip4addr_aton(mask,      &ipaddr)) error("Invalid mask address");
        if (!ip4addr_aton(gateway,   &ipaddr)) error("Invalid gateway address");
    }
    if (argc >= 5 && *argv[4]) {
        strcpy(hostname, (char *)getCstring(argv[4]));
        if (strlen(hostname) > 31) error("Hostname too long, max 31 chars");
    } else {
        strcpy(hostname, "PICO");
        strcat(hostname, id_out);
    }
    strcpy((char *)Option.SSID,     ssid);
    strcpy((char *)Option.PASSWORD, password);
    if (argc == 11) {
        strcpy(Option.ipaddress, ipaddress);
        strcpy(Option.mask,      mask);
        strcpy(Option.gateway,   gateway);
    } else {
        memset(Option.ipaddress, 0, 16);
        memset(Option.mask,      0, 16);
        memset(Option.gateway,   0, 16);
    }
    strcpy(Option.hostname, hostname);
    SaveOptions();
}

/* WEB-specific printoptions block. Called from MM_Misc.c's printoptions
 * unconditionally; non-WEB builds get a no-op stub elsewhere. */
extern void PO(const char *p);
extern void PO2Int(const char *fld, int val);
extern void PO3Int(const char *fld, int v1, int v2);
extern void PO2Str(const char *fld, const char *val);
extern void PRet(void);
extern void MMPrintString(char *s);
extern char MMputchar(char c, int flush);

void port_web_print_options(void)
{
    if(*Option.SSID){
        char password[]="****************************************************************";
        password[strlen((char *)Option.PASSWORD)]=0;
        PO("WIFI");
        MMPrintString((char *)Option.SSID);MMputchar(',',1);MMputchar(' ',1);
        MMPrintString(password);
        MMputchar(',',1);
        MMputchar(' ',1);
        MMPrintString(Option.hostname);
        if(*Option.ipaddress){
            MMputchar(',',1);
            MMputchar(' ',1);
            MMPrintString(Option.ipaddress);
            MMputchar(',',1);
            MMputchar(' ',1);
            MMPrintString(Option.mask);
            MMputchar(',',1);
            MMputchar(' ',1);
            MMPrintString(Option.gateway);
        }
        PRet();
    }
    if(Option.TCP_PORT && Option.ServerResponceTime!=5000)PO3Int("TCP SERVER PORT", Option.TCP_PORT, Option.ServerResponceTime);
    if(Option.TCP_PORT && Option.ServerResponceTime==5000)PO2Int("TCP SERVER PORT", Option.TCP_PORT);
    if(Option.UDP_PORT && Option.UDPServerResponceTime!=5000)PO3Int("UDP SERVER PORT", Option.UDP_PORT, Option.UDPServerResponceTime);
    if(Option.UDP_PORT && Option.UDPServerResponceTime==5000)PO2Int("UDP SERVER PORT", Option.UDP_PORT);
    if(Option.Telnet==1)PO2Str("TELNET", "CONSOLE ON");
    if(Option.Telnet==-1)PO2Str("TELNET", "CONSOLE ONLY");
    if(Option.disabletftp==1)PO2Str("TFTP", "OFF");
}

/* WEB-only OPTION setters: WEB MESSAGES / WIFI / TCP SERVER PORT /
 * UDP SERVER PORT / TELNET / TFTP. Returns 1 if matched (function
 * usually never returns — SoftReset). */
int port_web_option_setter(unsigned char *cmdline)
{
    unsigned char *tp;
    tp = checkstring(cmdline, (unsigned char *)"WEB MESSAGES");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"OFF"))	{ optionsuppressstatus=1; return 1; }
        if(checkstring(tp, (unsigned char *)"ON"))	{ optionsuppressstatus=0; return 1; }
    }
    tp = checkstring(cmdline, (unsigned char *)"WIFI");
    if(tp) {
        setwifi(tp);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TCP SERVER PORT");
    if(tp) {
        getargs(&tp,3,(unsigned char *)",");
        if(CurrentLinePtr) error("Invalid in a program");
        Option.TCP_PORT=getint(argv[0],0,65535);
        Option.ServerResponceTime=5000;
        if(argc==3)Option.ServerResponceTime=getint(argv[2],1000,20000);
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"UDP SERVER PORT");
    if(tp) {
        getargs(&tp,3,(unsigned char *)",");
        if(CurrentLinePtr) error("Invalid in a program");
        Option.UDP_PORT=getint(argv[0],0,65535);
        Option.UDPServerResponceTime=5000;
        if(argc==3)Option.UDPServerResponceTime=getint(argv[2],1000,20000);
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TELNET CONSOLE");
    if(tp) {
        if(CurrentLinePtr) error("Invalid in a program");
        if(checkstring(tp, (unsigned char *)"OFF"))Option.Telnet=0;
        else if(checkstring(tp, (unsigned char *)"ON"))Option.Telnet=1;
        else if(checkstring(tp, (unsigned char *)"ONLY")) Option.Telnet=-1;
        else error("Syntax");
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TFTP");
    if(tp) {
        if(CurrentLinePtr) error("Invalid in a program");
        if(checkstring(tp, (unsigned char *)"OFF"))Option.disabletftp=1;
        else if(checkstring(tp, (unsigned char *)"ON"))Option.disabletftp=0;
        else if(checkstring(tp, (unsigned char *)"ENABLE"))Option.disabletftp=0;
        else if(checkstring(tp, (unsigned char *)"DISABLE"))Option.disabletftp=1;
        else error("Syntax");
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    return 0;
}

/* WEB-only MM.<X> info function: TCP REQUEST / TCP PORT / UDP PORT /
 * IP ADDRESS / MAX CONNECTIONS / WIFI STATUS / TCPIP STATUS. */
int port_web_mminfo(unsigned char *ep, int64_t *out_iret,
                    unsigned char *out_sret, int *out_targ)
{
    unsigned char *tp;
    if((tp=checkstring(ep, (unsigned char *)"TCP REQUEST"))){
        int i=getint(tp,1,MaxPcb)-1;
        *out_iret=TCPstate->inttrig[i];
        *out_targ=T_INT;
        return 1;
    }
    if((tp=checkstring(ep, (unsigned char *)"TCP PORT"))){
        *out_iret=Option.TCP_PORT;
        *out_targ=T_INT;
        return 1;
    }
    if((tp=checkstring(ep, (unsigned char *)"UDP PORT"))){
        *out_iret=Option.UDP_PORT;
        *out_targ=T_INT;
        return 1;
    }
    if(checkstring(ep,(unsigned char *)"IP ADDRESS")){
        strcpy((char *)out_sret,ip4addr_ntoa(netif_ip4_addr(netif_list)));
        CtoM(out_sret);
        *out_targ=T_STR;
        return 1;
    }
    if(checkstring(ep,(unsigned char *)"MAX CONNECTIONS")){
        *out_iret=MaxPcb;
        *out_targ=T_INT;
        return 1;
    }
    if(checkstring(ep,(unsigned char *)"WIFI STATUS")){
        *out_iret=cyw43_wifi_link_status(&cyw43_state,CYW43_ITF_STA);
        *out_targ=T_INT;
        return 1;
    }
    if(checkstring(ep,(unsigned char *)"TCPIP STATUS")){
        *out_iret=cyw43_tcpip_link_status(&cyw43_state,CYW43_ITF_STA);
        *out_targ=T_INT;
        return 1;
    }
    return 0;
}

/* OPTION SSID$ getter — separate so MM_Misc.c's OPTION info chain stays
 * unconditional. */
int port_web_get_ssid(unsigned char *out_sret, int *out_targ)
{
    strcpy((char *)out_sret,(char *)Option.SSID);
    CtoM(out_sret);
    *out_targ=T_STR;
    return 1;
}

/* WebConnect: relocated from PicoMite.c so the call site there can
 * be unconditional. Linked only on WiFi ports (MMsetwifi.c is in the
 * WiFi-port source list); MMweb_stubs.c provides the no-op shim on
 * non-WiFi devices. */
volatile int WIFIconnected = 0;
int startupcomplete = 0;
extern void open_tcp_server(void);
extern void open_udp_server(void);

void WebConnect(void){
    if(*Option.SSID){
        if(*Option.ipaddress){
            cyw43_arch_enable_sta_mode();
            dhcp_stop(cyw43_state.netif);
            ip4_addr_t ipaddr, gateway, mask;
            ip4addr_aton(Option.ipaddress, &ipaddr);
            ip4addr_aton(Option.gateway, &gateway);
            ip4addr_aton(Option.mask, &mask);
            netif_set_addr( cyw43_state.netif,&ipaddr,&mask,&gateway);
        } else cyw43_arch_enable_sta_mode();
        if(*Option.hostname){
            MMPrintString(Option.hostname);
            netif_set_hostname(cyw43_state.netif, Option.hostname);
        }
        cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
        MMPrintString(" connecting to WiFi...\r\n");
        if (cyw43_arch_wifi_connect_timeout_ms((char *)Option.SSID, (char *)(*Option.PASSWORD ? Option.PASSWORD : NULL), (*Option.PASSWORD ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN), 30000)) {
            MMPrintString("failed to connect.\r\n");
            WIFIconnected=0;
        } else {
            char buff[STRINGSIZE]={0};
            sprintf(buff,"Connected %s\r\n",ip4addr_ntoa(netif_ip4_addr(netif_list)));
            MMPrintString(buff);
            WIFIconnected=1;
            open_tcp_server();
            if(!Option.disabletftp)cmd_tftp_server_init();
            if(Option.UDP_PORT)open_udp_server();
        }
    } else {
        cyw43_arch_enable_sta_mode();
        cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
    }
    cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM & ~0xf);
}

#include "hal/hal_option_setters.h"

/* WiFi ports don't expose OPTION PICO ON/OFF (CYW43 actually owns the
 * shadow pins). */
int port_setter_pico_pins(unsigned char *cmdline) {
    (void)cmdline;
    return 0;
}

/* REPL-startup WiFi init: cyw43_arch_init + WebConnect. */
#include "hal/hal_main_init.h"
#include "hardware/clocks.h"
#include "pico/cyw43_driver.h"
extern int startupcomplete;

#if CYW43_PIO_CLOCK_DIV_DYNAMIC
/* CYW43 gSPI tops out at ~50 MHz.  Pick the smallest integer divider
 * that keeps clk_sys/div under 45 MHz so we leave a little margin and
 * the same divider is safe across small clock-source jitter.  Called
 * before cyw43_arch_init so the bus initialises with the right rate
 * for whatever Option.CPU_Speed the user has persisted.
 *
 * Examples:
 *   252 MHz → div 6 → 42.0 MHz gSPI
 *   315 MHz → div 7 → 45.0 MHz
 *   378 MHz → div 9 → 42.0 MHz
 *   126 MHz → div 3 → 42.0 MHz   (low-power modes still safe) */
static void cyw43_pio_divider_for_clk_sys(void) {
    uint32_t clk_hz = clock_get_hz(clk_sys);
    uint32_t div = (clk_hz + 44999999u) / 45000000u;
    if (div < 2) div = 2;   /* SDK minimum */
    cyw43_set_pio_clock_divisor((uint16_t)div, 0);
}
#endif

void port_repl_wifi_arch_init_and_connect(void) {
#if CYW43_PIO_CLOCK_DIV_DYNAMIC
    cyw43_pio_divider_for_clk_sys();
#endif
    if (cyw43_arch_init() == 0) {
        startupcomplete = 1;
        WebConnect();
    }
}

/* WiFi ports leave GPIO 23 to the CYW43 module — no PWM shadow. */
void hal_pwm_mode_shadow_apply(void) { }

/* WiFi PIO pin-reset loop walks the full NBRPINS range — the CYW43
 * radio doesn't expose a shadow-pin range that needs to be skipped. */
void port_pio_pin_reset_inputs(void) {
    for (int i = 1; i < NBRPINS; i++) {
        if (CheckPin(i, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) {
            gpio_set_input_enabled(PinDef[i].GPno, true);
        }
    }
}

/* WiFi ports limit OPTION HEARTBEAT to ON/OFF — no pin reassignment
 * (the heartbeat LED lives on the CYW43 module). */
int port_setter_heartbeat(unsigned char *cmdline) {
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"HEARTBEAT");
    if (!tp) return 0;
    if (checkstring(tp, (unsigned char *)"OFF") || checkstring(tp, (unsigned char *)"DISABLE")) {
        Option.NoHeartbeat = 1;
    } else if (checkstring(tp, (unsigned char *)"ON") || checkstring(tp, (unsigned char *)"ENABLE")) {
        Option.NoHeartbeat = 0;
    } else error("Syntax");
    SaveOptions();
    return 1;
}

/* WiFi-only command + function bodies (cmd_web / fun_json / scan
 * result callback) relocated from Custom.c. Custom.c is universal
 * across device ports; this code references CYW43 APIs that only
 * exist on WiFi builds. Non-WiFi ports get a `cmd_web` stub from
 * MMweb_stubs.c and never reference fun_json (excluded from the
 * non-WiFi token-palette in port_tokens.h). */

extern void cmd_ntp(unsigned char *cmdline);
extern void cmd_udp(unsigned char *cmdline);
extern int  cmd_mqtt(void);
extern int  cmd_tcpclient(void);
extern int  cmd_tcpserver(void);
extern int  startupcomplete;

char *scan_dest = NULL;
volatile char *scan_dups = NULL;
volatile int scan_size;

static int scan_result(void *env, const cyw43_ev_scan_result_t *result) {
    if (result) {
        Timer4 = 5000;
        char buff[STRINGSIZE] = {0};
        if (scan_dups == NULL) return 0;
        for (int i = 0; i < scan_dups[32 * 100]; i++) {
            if (strcmp((char *)result->ssid, (char *)&scan_dups[32 * i]) == 0) return 0;
        }
        for (int i = 0; i < 100; i++) {
            if (scan_dups[32 * i] == 0) {
                strcpy((char *)&scan_dups[32 * i], (char *)result->ssid);
                scan_dups[32 * 100]++;
                break;
            }
        }
        sprintf(buff, "ssid: %-32s rssi: %4d chan: %3d mac: %02x:%02x:%02x:%02x:%02x:%02x sec: %u\r\n",
            result->ssid, result->rssi, result->channel,
            result->bssid[0], result->bssid[1], result->bssid[2], result->bssid[3], result->bssid[4], result->bssid[5],
            result->auth_mode);
        if (scan_dest != NULL) {
            if (strlen(((char *)&scan_dest[8]) + strlen(buff)) > scan_size) {
                FreeMemorySafe((void **)&scan_dups);
                scan_dest = NULL;
                error("Array too small");
            }
            if (scan_dest[8] == 0) strcpy(&scan_dest[8], buff);
            else strcat(&scan_dest[8], buff);
        } else MMPrintString(buff);
    }
    return 0;
}

void cmd_web(void) {
    unsigned char *tp;
    tp = checkstring(cmdline, (unsigned char *)"CONNECT");
    if (tp) {
        if (*tp) {
            setwifi(tp);
            WebConnect();
        } else {
            if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) < 0) {
                WebConnect();
            }
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"SCAN");
    if (tp) {
        void *ptr1 = NULL;
        if (*tp) {
            ptr1 = findvar(tp, V_FIND | V_EMPTY_OK);
            if (g_vartbl[g_VarIndex].type & T_INT) {
                if (g_vartbl[g_VarIndex].dims[1] != 0) error("Invalid variable");
                if (g_vartbl[g_VarIndex].dims[0] <= 0) {
                    error("Argument 1 must be integer array");
                }
                scan_size = (g_vartbl[g_VarIndex].dims[0] - g_OptionBase) * 8;
                scan_dest = (char *)ptr1;
                scan_dest[8] = 0;
            } else error("Argument 1 must be integer array");
        }
        scan_dups = GetMemory(32 * 100 + 1);
        cyw43_wifi_scan_options_t scan_options = {0};
        int err = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, scan_result);
        if (err == 0) {
            MMPrintString("\nPerforming wifi scan\r\n");
        } else {
            char buff[STRINGSIZE] = {0};
            sprintf(buff, "Failed to start scan: %d\r\n", err);
            MMPrintString(buff);
        }
        Timer4 = 500;
        while (Timer4) if (startupcomplete) ProcessWeb(0);
        if (scan_dest) {
            uint64_t *p = (uint64_t *)scan_dest;
            *p = strlen(&scan_dest[8]);
        }
        scan_dest = NULL;
        FreeMemorySafe((void **)&scan_dups);
        return;
    }
    if (!(WIFIconnected && cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP))
        error("WIFI not connected");
    tp = checkstring(cmdline, (unsigned char *)"NTP");
    if (tp) { cmd_ntp(tp); return; }
    tp = checkstring(cmdline, (unsigned char *)"UDP");
    if (tp) { cmd_udp(tp); return; }
    if (cmd_mqtt())      return;
    if (cmd_tcpclient()) return;
    if (cmd_tcpserver()) return;
    error("Syntax");
}

void fun_json(void) {
    char *json_string = NULL;
    const cJSON *root = NULL;
    void *ptr1 = NULL;
    char *p;
    sret = GetTempMemory(STRINGSIZE);
    int64_t *dest = NULL;
    MMFLOAT tempd;
    int i, j, k, mode, index;
    char field[32], num[6];
    getargs(&ep, 3, (unsigned char *)",");
    char *a = GetTempMemory(STRINGSIZE);
    ptr1 = findvar(argv[0], V_FIND | V_EMPTY_OK);
    if (g_vartbl[g_VarIndex].type & T_INT) {
        if (g_vartbl[g_VarIndex].dims[1] != 0) error("Invalid variable");
        if (g_vartbl[g_VarIndex].dims[0] <= 0) {
            error("Argument 1 must be integer array");
        }
        dest = (long long int *)ptr1;
        json_string = (char *)&dest[1];
    } else error("Argument 1 must be integer array");
    cJSON_InitHooks(NULL);
    cJSON *parse = cJSON_Parse(json_string);
    if (parse == NULL) error("Invalid JSON data");
    root = parse;
    p = (char *)getCstring((unsigned char *)argv[2]);
    int len = strlen(p);
    memset(field, 0, 32);
    memset(num, 0, 6);
    i = 0; j = 0; k = 0; mode = 0;
    while (i < len) {
        if (p[i] == '[') {
            mode = 1;
            field[j] = 0;
            root = cJSON_GetObjectItemCaseSensitive(root, field);
            memset(field, 0, 32);
            j = 0;
        }
        if (p[i] == ']') {
            num[k] = 0;
            index = atoi(num);
            root = cJSON_GetArrayItem(root, index);
            memset(num, 0, 6);
            k = 0;
        }
        if (p[i] == '.') {
            if (mode == 0) {
                field[j] = 0;
                root = cJSON_GetObjectItemCaseSensitive(root, field);
                memset(field, 0, 32);
                j = 0;
            } else {
                mode = 0;
            }
        } else {
            if (mode == 0) field[j++] = p[i];
            else if (p[i] != '[') num[k++] = p[i];
        }
        i++;
    }
    root = cJSON_GetObjectItem(root, field);

    if (cJSON_IsObject(root))  { cJSON_Delete(parse); error("Not an item"); return; }
    if (cJSON_IsInvalid(root)) { cJSON_Delete(parse); error("Not an item"); return; }
    if (cJSON_IsNumber(root)) {
        tempd = root->valuedouble;
        if ((MMFLOAT)((int64_t)tempd) == tempd) IntToStr(a, (int64_t)tempd, 10);
        else FloatToStr(a, tempd, 0, STR_AUTO_PRECISION, ' ');
        cJSON_Delete(parse);
        sret = (unsigned char *)a;
        sret = CtoM(sret);
        targ = T_STR;
        return;
    }
    if (cJSON_IsBool(root)) {
        int64_t tempint = root->valueint;
        cJSON_Delete(parse);
        if (tempint) strcpy((char *)sret, "true");
        else strcpy((char *)sret, "false");
        sret = CtoM(sret);
        targ = T_STR;
        return;
    }
    if (cJSON_IsString(root)) {
        strcpy(a, root->valuestring);
        cJSON_Delete(parse);
        sret = (unsigned char *)a;
        sret = CtoM(sret);
        targ = T_STR;
        return;
    }
    cJSON_Delete(parse);
    targ = T_STR;
    sret = (unsigned char *)a;
}
