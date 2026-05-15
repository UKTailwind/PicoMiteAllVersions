#include "pico_runtime_internal.h"

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
char MMputchar(char c, int flush) {
    putConsole(c, flush);
    if(isprint(c)) MMCharPos++;
    if(c == '\r') {
        MMCharPos = 1;
    }
    return c;
}
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
    unsigned int c = -1;                                            // default no character
    unsigned int tc = -1;                                           // default no character
    unsigned int ttc = -1;                                          // default no character
    static unsigned int c1 = -1;
    static unsigned int c2 = -1;
    static unsigned int c3 = -1;
    static unsigned int c4 = -1;
//	static int crseen = 0;

    if(c1 != -1) {                                                  // check if there are discarded chars from a previous sequence
        c = c1; c1 = c2; c2 = c3; c3 = c4; c4 = -1;                 // shuffle the queue down
        return c;                                                   // and return the head of the queue
    }

    c = getConsole();                                               // do discarded chars so get the char
    /* hal_keyboard_service is a no-op on USB ports (TinyUSB pumps
     * itself); on PS/2 it runs CheckKeyboard. */
    if (c == -1) hal_keyboard_service();
    if(!(c==0x1b))return c;
    InkeyTimer = 0;                                             // start the timer
    while((c = getConsole()) == -1 && InkeyTimer < 30);         // get the second char with a delay of 30mS to allow the next char to arrive
    if(c == 'O'){   //support for many linux terminal emulators
        while((c = getConsole()) == -1 && InkeyTimer < 50);        // delay some more to allow the final chars to arrive, even at 1200 baud
        if(c == 'P') return F1;
        if(c == 'Q') return F2;
        if(c == 'R') return F3;
        if(c == 'S') return F4;
        if(c == 'T') return F5;
        if(c == '2'){
            while((tc = getConsole()) == -1 && InkeyTimer < 70);        // delay some more to allow the final chars to arrive, even at 1200 baud
            if(tc == 'R') return F3 + 0x20;
            c1 = 'O'; c2 = c; c3 = tc; return 0x1b;                 // not a valid 4 char code
        }
        c1 = 'O'; c2 = c; return 0x1b;                 // not a valid 4 char code
    }
    if(c != '[') { c1 = c; return 0x1b; }                       // must be a square bracket
    while((c = getConsole()) == -1 && InkeyTimer < 50);         // get the third char with delay
    if(c == 'A') return UP;                                     // the arrow keys are three chars
    if(c == 'B') return DOWN;
    if(c == 'C') return RIGHT;
    if(c == 'D') return LEFT;
    if(c < '1' && c > '6') { c1 = '['; c2 = c; return 0x1b; }   // the 3rd char must be in this range
    while((tc = getConsole()) == -1 && InkeyTimer < 70);        // delay some more to allow the final chars to arrive, even at 1200 baud
    if(tc == '~') {                                             // all 4 char codes must be terminated with ~
        if(c == '1') return HOME;
        if(c == '2') return INSERT;
        if(c == '3') return DEL;
        if(c == '4') return END;
        if(c == '5') return PUP;
        if(c == '6') return PDOWN;
        c1 = '['; c2 = c; c3 = tc; return 0x1b;                 // not a valid 4 char code
    }
    while((ttc = getConsole()) == -1 && InkeyTimer < 90);       // get the 5th char with delay
    if(ttc == '~') {                                            // must be a ~
        if(c == '1') {
            if(tc >='1' && tc <= '5') return F1 + (tc - '1');   // F1 to F5
            if(tc >='7' && tc <= '9') return F6 + (tc - '7');   // F6 to F8
        }
        if(c == '2') {
            if(tc =='0' || tc == '1') return F9 + (tc - '0');   // F9 and F10
            if(tc =='3' || tc == '4') return F11 + (tc - '3');  // F11 and F12
            if(tc =='5' || tc=='6') return F3 + 0x20 + tc-'5';                      // SHIFT-F3 and F4
            if(tc =='8' || tc=='9') return F5 + 0x20 + tc-'8';                      // SHIFT-F5 and F6
        }
        if(c == '3') {
            if(tc >='1' && tc <= '4') return F7 + 0x20 + (tc - '1');   // SHIFT-F7 to F10
        }
        //NB: SHIFT F1, F2, F11, and F12 don't appear to generate anything
    }
    // nothing worked so bomb out
    c1 = '['; c2 = c; c3 = tc; c4 = ttc;
    return 0x1b;
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
// print a string to the console interfaces
void MMPrintString(char* s) {
    while(*s) {
        if(s[1])MMputchar(*s,0);
        else MMputchar(*s,1);
        s++;
    }
    fflush(stdout);
}
void SSPrintString(char* s) {
    while(*s) {
        SerialConsolePutC(*s,0);
        s++;
    }
    fflush(stdout);
}
