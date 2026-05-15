#include "pico_runtime_internal.h"
#include "runtime/runtime_console_escdecode.h"

/* Per-call timeout byte-reader for the shared escape decoder. Uses the
 * legacy InkeyTimer pattern: zero the timer, busy-wait on getConsole()
 * until either a byte arrives or the timer ticks past timeout_ms. */
static int __not_in_flash_func(pico_escdecode_read_byte_ms)(int timeout_ms) {
    InkeyTimer = 0;
    int c;
    unsigned int deadline = (timeout_ms < 0) ? 0u : (unsigned int)timeout_ms;
    while ((c = getConsole()) == -1 && InkeyTimer < deadline);
    return c;
}

int __not_in_flash_func(getConsole)(void) {
    int c=-1;
    ProcessWeb(1);          /* stub no-op on non-WiFi (MMweb_stubs.c) */
    CheckAbort();
    if(ConsoleRxBufHead != ConsoleRxBufTail) {                            // if the queue has something in it
        c = ConsoleRxBuf[ConsoleRxBufTail];
        ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;   // advance the head of the queue
	}
    return c;
}

void __not_in_flash_func(putConsole)(int c, int flush) {
    if(OptionConsole & 2)DisplayPutC(c);
    if(OptionConsole & 1)SerialConsolePutC(c, flush);
}
// put a character out to the serial console
char  __not_in_flash_func(SerialConsolePutC)(char c, int flush) {
		if(c == '\b') {
		    if (MMCharPos!=1){
			    MMCharPos -= 1;
		    }
		}
    /* USB-CDC always; the HAL impl already gates on tud_cdc_connected()
     * and Option.SerialConsole stdio mode internally, so without a
     * cable plugged in this is a no-op. The historical
     * wifi_serial_telnet_configured() gate that wrapped CDC output was
     * redundant and broke CDC-only debug ports. */
    hal_console_usb_cdc_putc(c, flush);
    /* UART output still rides the telnet gate on WiFi ports (mirroring
     * to UART when telnet is the primary console); non-WiFi ports'
     * stub returns 1 so UART always runs. */
    if (wifi_serial_telnet_configured()) {
        if(Option.SerialConsole){
            int empty=uart_is_writable((Option.SerialConsole & 3)==1 ? uart0 : uart1);
            while(ConsoleTxBufTail == ((ConsoleTxBufHead + 1) % CONSOLE_TX_BUF_SIZE));
            ConsoleTxBuf[ConsoleTxBufHead] = c;
            ConsoleTxBufHead = (ConsoleTxBufHead + 1) % CONSOLE_TX_BUF_SIZE;
            if(empty){
                while(hal_flash_write_active()){}
                uart_set_irq_enables((Option.SerialConsole & 3)==1 ? uart0 : uart1, true, true);
                irq_set_pending((Option.SerialConsole & 3)==1 ? UART0_IRQ : UART1_IRQ);
            }
        }
    }
    TelnetPutC(c, flush);
    ProcessWeb(1);
    return c;
}
// MMputchar lives in runtime/runtime_console_putchar.c — shared across every port.
// returns the number of character waiting in the console input queue
int kbhitConsole(void) {
    int i;
    i = ConsoleRxBufHead - ConsoleRxBufTail;
    if(i < 0) i += CONSOLE_RX_BUF_SIZE;
    return i;
}
// check if there is a keystroke waiting in the buffer and, if so, return with the char
// returns -1 if no char waiting
// the main work is to check for vt100 escape code sequences and map to Maximite codes
int HAL_PORT_MMINKEY_DECL(MMInkey)(void) {
    int c;
    /* Drain any chars left over from an earlier unrecognised escape
     * sequence before consulting the input source. */
    {
        int pb = mmbasic_escdecode_pop_pushback();
        if (pb >= 0) return pb;
    }

    c = getConsole();                                               // do discarded chars so get the char
    /* hal_keyboard_service is a no-op on USB ports (TinyUSB pumps
     * itself); on PS/2 it runs CheckKeyboard. */
    if (c == -1) hal_keyboard_service();
    if (c < 0) return c;
    if (c == 0x1b) return mmbasic_escdecode_run(pico_escdecode_read_byte_ms);
    return mmbasic_console_normalise_byte(c);
}
// MMgetline lives in runtime/runtime_getline.c — shared across every port.

// get a keystroke.  Will wait forever for input
// if the unsigned char is a cr then replace it with a newline (lf)
int MMgetchar(void) {
	int c;
	do {
		ShowCursor(1);
		c=MMInkey();
	} while(c == -1);
	ShowCursor(0);
	return c;
}
// MMPrintString / SSPrintString live in runtime/runtime_console_printstring.c.
