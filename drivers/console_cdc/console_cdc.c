/*
 * drivers/console_cdc/console_cdc.c — RP2350 native USB CDC stdio
 * helpers shared across every port that runs the USB peripheral in
 * device mode. See console_cdc.h for the contract.
 *
 * Linked alongside whichever HAL keyboard backend the port uses (PS/2
 * or CDC-only). USB-host-keyboard ports own the USB peripheral in host
 * mode and must NOT link this file.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "console_cdc.h"
#include "tusb.h"
#include "class/cdc/cdc_device.h"
#include "pico/stdlib.h"

void console_cdc_boot_setup(void) {
    stdio_set_translate_crlf(&stdio_usb, false);
    if (!(Option.SerialConsole == 1 || Option.SerialConsole == 2) || Option.Telnet == -1) {
        uint64_t t = time_us_64();
        while (1) {
            if (tud_cdc_connected()) break;
            if (time_us_64() - t > 5000000) break;
        }
    }
}

void console_cdc_putc(char c, int flush) {
    if (Option.SerialConsole == 0 || Option.SerialConsole > 4) {
        if (tud_cdc_connected()) {
            putc(c, stdout);
            if (flush) {
                fflush(stdout);
            }
        }
    }
}

void console_cdc_drain_to_rxbuf(void) {
    int c;
    /* Pump CDC bytes whenever a host is connected and the active console
     * is stdio (Option.SerialConsole == 0 || > 4). The historical
     * `Option.Telnet != -1` gate that wrapped this on WiFi ports was
     * redundant — without telnet configured, the CDC console is the
     * only way for a developer to type at the REPL on those ports. */
    if (tud_cdc_connected() && (Option.SerialConsole == 0 || Option.SerialConsole > 4)) {
        while ((c = tud_cdc_read_char()) != -1) {
            ConsoleRxBuf[ConsoleRxBufHead] = c;
            if (BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey) {
                MMAbort = true;
                ConsoleRxBufHead = ConsoleRxBufTail;
            } else if (ConsoleRxBuf[ConsoleRxBufHead] == keyselect && KeyInterrupt != NULL) {
                Keycomplete = true;
            } else {
                ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;
                if (ConsoleRxBufHead == ConsoleRxBufTail) {
                    ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;
                }
            }
        }
    }
}
