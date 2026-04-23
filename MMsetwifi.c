/*
 * MMsetwifi.c — OPTION WIFI handler. Compiled only on PICOMITEWEB
 * builds (gated in CMakeLists). Uses lwIP's ip4addr_aton for the
 * optional static-IP arguments; that's the only WEB-only dependency.
 *
 * Lifted out of MM_Misc.c to drop the `#ifdef PICOMITEWEB` wrapper
 * that defined this function inline.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "lwip/ip4_addr.h"

extern char id_out[12];

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
