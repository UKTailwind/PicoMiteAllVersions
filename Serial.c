/***********************************************************************************************************************
PicoMite MMBasic

Serial.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved. 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: 
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed 
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed 
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software 
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

************************************************************************************************************************/


#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

// variables for com1
int com1 = 0;														// true if COM1 is enabled
int com1_buf_size;													// size of the buffer used to receive chars
int com1_baud = 0;													// determines the baud rate
char *com1_interrupt;												// pointer to the interrupt routine
int com1_ilevel;													// number nbr of chars in the buffer for an interrupt
int com1_TX_complete = false;
unsigned char *com1Rx_buf;											// pointer to the buffer for received characters
volatile int com1Rx_head, com1Rx_tail;								// head and tail of the ring buffer for com1
unsigned char *com1Tx_buf;											// pointer to the buffer for transmitted characters
volatile int com1Tx_head, com1Tx_tail;								// head and tail of the ring buffer for com1
volatile int com1complete=1;
uint16_t Rx1Buffer;
char com1_mode;                                                     // keeps track of the settings for com4
unsigned char com1_bit9 = 0;                                        // used to track the 9th bit
extern uint32_t ticks_per_microsecond;

// variables for com2
int com2 = 0;														// true if COM2 is enabled
int com2_buf_size;													// size of the buffer used to receive chars
int com2_baud = 0;													// determines the baud rate
char *com2_interrupt;												// pointer to the interrupt routine
int com2_ilevel;													// number nbr of chars in the buffer for an interrupt
int com2_TX_complete = false;
unsigned char *com2Rx_buf;											// pointer to the buffer for received characters
volatile int com2Rx_head, com2Rx_tail;								// head and tail of the ring buffer for com2 Rx
unsigned char *com2Tx_buf;											// pointer to the buffer for transmitted characters
volatile int com2Tx_head, com2Tx_tail;								// head and tail of the ring buffer for com2 Tx
volatile int com2complete=1;
char com2_mode;                                                     // keeps track of the settings for com4
unsigned char com2_bit9 = 0;                                        // used to track the 9th bit
// uart interrupt handler
void on_uart_irq0() {
    if(uart_is_readable(uart0)) {
		char cc = uart_getc(uart0); 
		if(!(Option.SerialConsole & 1)){
			if(GPSchannel==1){
				*gpsbuf=cc;
				gpsbuf++;
				gpscount++;
				if((char)cc==10 || gpscount==128){
					if(gpscurrent){
						*gpsbuf=0;
						gpscurrent=0;
						gpscount=0;
						gpsbuf=gpsbuf1;
						gpsready=gpsbuf2;
					} else {
						*gpsbuf=0;
						gpscurrent=1;
						gpscount=0;
						gpsbuf=gpsbuf2;
						gpsready=gpsbuf1;
					}
				}
			} else {
				com1Rx_buf[com1Rx_head]  =cc;   // store the byte in the ring buffer
				com1Rx_head = (com1Rx_head + 1) % com1_buf_size;     // advance the head of the queue
				if(com1Rx_head == com1Rx_tail) {                           // if the buffer has overflowed
					com1Rx_tail = (com1Rx_tail + 1) % com1_buf_size; // throw away the oldest char
				}
			}
		} else {
	  		ConsoleRxBuf[ConsoleRxBufHead]  = cc;   // store the byte in the ring buffer
	  		if(BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey) {// if the user wants to stop the progran
	  			MMAbort = true;                                        // set the flag for the interpreter to see
	  			ConsoleRxBufHead = ConsoleRxBufTail;                    // empty the buffer
	  		} else {
	  			ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;     // advance the head of the queue
	  			if(ConsoleRxBufHead == ConsoleRxBufTail) {                           // if the buffer has overflowed
	  				ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
	  			}
	  		}
		}
    }
    if(uart_is_writable(uart0)){
		if(!(Option.SerialConsole & 1)){
			if(com1Tx_head != com1Tx_tail) {
				uart_putc_raw(uart0,com1Tx_buf[com1Tx_tail]);
				com1Tx_tail = (com1Tx_tail + 1) % TX_BUFFER_SIZE;       // advance the tail of the queue
			} else {
				uart_set_irq_enables(uart0, true, false);
				com1_TX_complete=true;
			}
		} else {
	  		if(ConsoleTxBufTail != ConsoleTxBufHead) {
	  			uart_putc_raw(uart0,ConsoleTxBuf[ConsoleTxBufTail]);
	  			ConsoleTxBufTail = (ConsoleTxBufTail + 1) % CONSOLE_TX_BUF_SIZE; // advance the tail of the queue
	  		} else {
				uart_set_irq_enables(uart0, true, false);
	  		}
		}
    }
}
void on_uart_irq1() {
	if (uart_is_readable(uart1)) {
		char cc = uart_getc(uart1); 
		if(!(Option.SerialConsole & 2)){
			if(GPSchannel==2){
				*gpsbuf=cc;
				gpsbuf++;
				gpscount++;
				if((char)cc==10 || gpscount==128){
					if(gpscurrent){
						*gpsbuf=0;
						gpscurrent=0;
						gpscount=0;
						gpsbuf=gpsbuf1;
						gpsready=gpsbuf2;
					} else {
						*gpsbuf=0;
						gpscurrent=1;
						gpscount=0;
						gpsbuf=gpsbuf2;
						gpsready=gpsbuf1;
					}
				}
			} else {
				com2Rx_buf[com2Rx_head]  = cc;   // store the byte in the ring buffer
				com2Rx_head = (com2Rx_head + 1) % com2_buf_size;     // advance the head of the queue
				if(com2Rx_head == com2Rx_tail) {                           // if the buffer has overflowed
					com2Rx_tail = (com2Rx_tail + 1) % com2_buf_size; // throw away the oldest char
				}
			}
		} else {
	  		ConsoleRxBuf[ConsoleRxBufHead]  = cc;   // store the byte in the ring buffer
	  		if(BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey) {// if the user wants to stop the progran
	  			MMAbort = true;                                        // set the flag for the interpreter to see
	  			ConsoleRxBufHead = ConsoleRxBufTail;                    // empty the buffer
	  		} else {
	  			ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;     // advance the head of the queue
	  			if(ConsoleRxBufHead == ConsoleRxBufTail) {                           // if the buffer has overflowed
	  				ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
	  			}
	  		}
		}
    }
    if(uart_is_writable(uart1)){
		if(!(Option.SerialConsole & 2)){
			if(com2Tx_head != com2Tx_tail) {
				uart_putc_raw(uart1,com2Tx_buf[com2Tx_tail]);
				com2Tx_tail = (com2Tx_tail + 1) % TX_BUFFER_SIZE;       // advance the tail of the queue
			} else {
				uart_set_irq_enables(uart1, true, false);
				com2_TX_complete=true;
			}
		} else {
	  		if(ConsoleTxBufTail != ConsoleTxBufHead) {
	  			uart_putc_raw(uart1,ConsoleTxBuf[ConsoleTxBufTail]);
	  			ConsoleTxBufTail = (ConsoleTxBufTail + 1) % CONSOLE_TX_BUF_SIZE; // advance the tail of the queue
	  		} else {
				uart_set_irq_enables(uart1, true, false);
	  		}
		}
    }
}

/***************************************************************************************************
Initialise the serial function including the timer and interrupts.
****************************************************************************************************/

#define UART_ID  (uart ? uart1: uart0)
void invert_serial(int uart){
	int txpin, rxpin;
	if(uart==0){
		txpin=PinDef[UART0TXpin].GPno;
		rxpin=PinDef[UART0RXpin].GPno;
	} else {
		txpin=PinDef[UART1TXpin].GPno;
		rxpin=PinDef[UART1RXpin].GPno;
	}
	gpio_set_outover(txpin, GPIO_OVERRIDE_INVERT);
	gpio_set_inover(rxpin, GPIO_OVERRIDE_INVERT);
}
void MIPS16 setupuart(int uart, int s2,int parity, int b7, int baud, int inv){
	uart_init(UART_ID,baud);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, b7, s2, parity);
    uart_set_fifo_enabled(UART_ID, false);
	if(inv)invert_serial(uart);
    int UART_IRQ = (UART_ID == uart0 ? UART0_IRQ : UART1_IRQ);
    if(uart){
		irq_set_exclusive_handler(UART_IRQ, on_uart_irq1);
    	irq_set_enabled(UART_IRQ, true);
	} else {
		irq_set_exclusive_handler(UART_IRQ, on_uart_irq0);
    	irq_set_enabled(UART_IRQ, true);
	}
    uart_set_irq_enables(UART_ID, true, false);
	uart_set_irq_enables(UART_ID, true, false); 
}
/***************************************************************************************************
Initialise the serial function including the timer and interrupts.
****************************************************************************************************/
void MIPS16 SerialOpen(unsigned char *spec) {
	int baud, i, s2, parity, b7, bufsize, inv=0, ilevel=1;
	char *interrupt;

	getargs(&spec, 21, (unsigned char *)":,");										// this is a macro and must be the first executable stmt
	if(argc != 2 && (argc & 0x01) == 0) error("COM specification");

    b7 = 8;
	parity = UART_PARITY_NONE;
	s2 = 1;
    for(i = 0; i < 5; i++) {
    	if(str_equal(argv[argc - 1], (unsigned char *)"EVEN")) {
    		if(parity)error("Syntax");
    		else {parity = UART_PARITY_EVEN; argc -= 2; }	// set even parity
    	}
    	if(str_equal(argv[argc - 1], (unsigned char *)"ODD")) {
    		if(parity)error("Syntax");
    		else {parity = UART_PARITY_ODD; argc -= 2; }	// set even parity
    	}
    	if(str_equal(argv[argc - 1], (unsigned char *)"INV")) { inv = 1; argc -= 2; };	// invert the serial port
    	if(str_equal(argv[argc - 1], (unsigned char *)"DE")) error("DE not Supported");	// get the two stop bit option
	   	if(str_equal(argv[argc - 1], (unsigned char *)"OC")) error("OC not Supported");	// get the two stop bit option
    	if(str_equal(argv[argc - 1], (unsigned char *)"9BIT")) error("9BIT not Supported");	// get the two stop bit option
     	if(str_equal(argv[argc - 1], (unsigned char *)"S2")) { s2 = 2; argc -= 2; }	// get the two stop bit option
    	if(str_equal(argv[argc - 1], (unsigned char *)"7BIT")) { b7 = 7; argc -= 2; }	// set the 7 bit byte option
    }
	if(argc < 1 || argc > 9) error("COM specification");

	if(argc >= 3 && *argv[2]) {
		baud = getint(argv[2],Option.CPU_Speed*1000/16/65535,921600);									// get the baud rate as a number
	} else
		baud = COM_DEFAULT_BAUD_RATE;

	if(argc >= 5 && *argv[4])
		bufsize = getinteger(argv[4]);								// get the buffer size as a number
	else
		bufsize = COM_DEFAULT_BUF_SIZE;

	if(argc >= 7) {
    	InterruptUsed = true;
    	argv[6]=(unsigned char *)strupr((char *)argv[6]);
		interrupt = (char *)GetIntAddress(argv[6]);							// get the interrupt location
	} else
		interrupt = NULL;

	if(argc == 9) {
		ilevel = getinteger(argv[8]);								// get the buffer level for interrupt as a number
		if(ilevel < 1 || ilevel > bufsize) error("COM specification");
	} else
		ilevel = 1;

/*	if(argc >= 11) {
    	InterruptUsed = true;
    	argv[6]=strupr(argv[10]);
		TXinterrupt = GetIntAddress(argv[10]);							// get the interrupt location
	} else
		TXinterrupt = NULL;
*/

	if(spec[3] == '1') {
	///////////////////////////////// this is COM1 ////////////////////////////////////

		if(com1) error("Already open");
		if(UART0TXpin==99 || UART0RXpin==99)error("Pins not set for COM1");

 		com1_buf_size = bufsize;									// extracted from the comspec above
		com1_interrupt = interrupt;
		com1_ilevel	= ilevel;

		// setup for receive
		com1Rx_buf = GetMemory(com1_buf_size);						// setup the buffer
		com1Rx_head = com1Rx_tail = 0;
		ExtCfg(UART0RXpin, EXT_COM_RESERVED, 0);                   // reserve the pin for com use


		// setup for transmit
		com1Tx_buf = GetMemory(TX_BUFFER_SIZE);						// setup the buffer
		com1Tx_head = com1Tx_tail = 0;
		ExtCfg(UART0TXpin, EXT_COM_RESERVED, 0);

        setupuart(0, s2, parity, b7, baud, inv);
        com1 = true;
		uSec(1000);
		com1Rx_head = com1Rx_tail = 0;
		com1Tx_head = com1Tx_tail = 0;

	}
	else if (spec[3] == '2') {
	///////////////////////////////// this is COM2 ////////////////////////////////////

		if(com2) error("Already open");
		if(UART1TXpin==99 || UART1RXpin==99)error("Pins not set for COM2");

 		com2_buf_size = bufsize;									// extracted from the comspec above
		com2_interrupt = interrupt;
		com2_ilevel	= ilevel;
//		com2_TX_interrupt = TXinterrupt;
//		com2_TX_complete = false;

		// setup for receive
		com2Rx_buf = GetMemory(com2_buf_size);						// setup the buffer
		com2Rx_head = com2Rx_tail = 0;
		ExtCfg(UART1RXpin, EXT_COM_RESERVED, 0);                   // reserve the pin for com use


		// setup for transmit
		com2Tx_buf = GetMemory(TX_BUFFER_SIZE);						// setup the buffer
		com2Tx_head = com2Tx_tail = 0;
		ExtCfg(UART1TXpin, EXT_COM_RESERVED, 0);                   // reserve the pin for com use
        setupuart(1, s2, parity, b7, baud, inv);
        com2 = true;
		uSec(1000);
		com2Rx_head = com2Rx_tail = 0;
		com2Tx_head = com2Tx_tail = 0;
	}
}

/***************************************************************************************************
Close a serial port.
****************************************************************************************************/
void MIPS16 SerialClose(int comnbr) {

	if(comnbr == 1 && com1) {
		uart_deinit(uart0);
		com1 = false;
		com1_interrupt = NULL;
		if(UART0RXpin!=99)ExtCfg(UART0RXpin, EXT_NOT_CONFIG, 0);
		if(UART0TXpin!=99)ExtCfg(UART0TXpin, EXT_NOT_CONFIG, 0);
		if(com1Rx_buf!=NULL){FreeMemory(com1Rx_buf); com1Rx_buf=NULL;}
		if(com1Tx_buf!=NULL){FreeMemory(com1Tx_buf); com1Tx_buf=NULL;}
	}

	else if(comnbr == 2 && com2) {
		uart_deinit(uart1);
		com2 = false;
		com2_interrupt = NULL;
		if(UART1RXpin!=99)ExtCfg(UART1RXpin, EXT_NOT_CONFIG, 0);
		if(UART1TXpin!=99)ExtCfg(UART1TXpin, EXT_NOT_CONFIG, 0);
		if(com2Rx_buf!=NULL){FreeMemory(com2Rx_buf); com2Rx_buf=NULL;}
		if(com2Tx_buf!=NULL){FreeMemory(com2Tx_buf); com2Tx_buf=NULL;}
	}
}



/***************************************************************************************************
Add a character to the serial output buffer.
****************************************************************************************************/
unsigned char SerialPutchar(int comnbr, unsigned char c) {
	if(comnbr == 1) {
      	while(com1Tx_tail == ((com1Tx_head + 1) % TX_BUFFER_SIZE))    // wait if the buffer is full
          if(MMAbort) {                                             // allow the user to abort a hung serial port
              com1Tx_tail = com1Tx_head = 0;                        // clear the buffer
              longjmp(mark, 1);                                     // and abort
        }
        int empty=uart_is_writable(uart0);
		com1Tx_buf[com1Tx_head] = c;							// add the char
		com1Tx_head = (com1Tx_head + 1) % TX_BUFFER_SIZE;		   // advance the head of the queue
		if(empty){
	        uart_set_irq_enables(uart0, true, true);
			irq_set_pending(UART0_IRQ);
		}
	}
	else if(comnbr == 2) {
      	while(com2Tx_tail == ((com2Tx_head + 1) % TX_BUFFER_SIZE))    // wait if the buffer is full
          if(MMAbort) {                                             // allow the user to abort a hung serial port
              com2Tx_tail = com2Tx_head = 0;                        // clear the buffer
              longjmp(mark, 1);                                     // and abort
        }
        int empty=uart_is_writable(uart1);
		com2Tx_buf[com2Tx_head] = c;							// add the char
		com2Tx_head = (com2Tx_head + 1) % TX_BUFFER_SIZE;		   // advance the head of the queue
		if(empty){
	        uart_set_irq_enables(uart1, true, true);
			irq_set_pending(UART1_IRQ);
		}
	}
	return c;
}



/***************************************************************************************************
Get the status the serial receive buffer.
Returns the number of characters waiting in the buffer
****************************************************************************************************/
int SerialRxStatus(int comnbr) {
	int i = 0;
	if(comnbr == 1) {
	    uart_set_irq_enables(uart0, false, true);
		i = com1Rx_head - com1Rx_tail;
		uart_set_irq_enables(uart0, true, true);
		if(i < 0) i += com1_buf_size;
	}
	else if(comnbr == 2) {
	    uart_set_irq_enables(uart1, false, true);
		i = com2Rx_head - com2Rx_tail;
	    uart_set_irq_enables(uart1, true, true);
		if(i < 0) i += com2_buf_size;
	}
	return i;
}


/***************************************************************************************************
Get the status the serial transmit buffer.
Returns the number of characters waiting in the buffer
****************************************************************************************************/
int SerialTxStatus(int comnbr) {
	int i = 0;
	if(comnbr == 1) {
		i = com1Tx_head - com1Tx_tail;
		if(i < 0) i += TX_BUFFER_SIZE;
	}
	else if(comnbr == 2) {
		i = com2Tx_head - com2Tx_tail;
		if(i < 0) i += TX_BUFFER_SIZE;
	}
	return i;
}



/***************************************************************************************************
Get a character from the serial receive buffer.
Note that this is returned as an integer and -1 means that there are no characters available
****************************************************************************************************/
int SerialGetchar(int comnbr) {
	int c;
    c = -1;                                                         // -1 is no data
	if(comnbr == 1) {
	    uart_set_irq_enables(uart0, false, true);
		if(com1Rx_head != com1Rx_tail) {                            // if the queue has something in it
			c = com1Rx_buf[com1Rx_tail];                            // get the char
 			com1Rx_tail = (com1Rx_tail + 1) % com1_buf_size;        // and remove from the buffer
		}
		uart_set_irq_enables(uart0, true, true);
	}
	else if(comnbr == 2) {

	    uart_set_irq_enables(uart1, false, true);
		if(com2Rx_head != com2Rx_tail) {                            // if the queue has something in it
			c = com2Rx_buf[com2Rx_tail];                            // get the char
 			com2Rx_tail = (com2Rx_tail + 1) % com2_buf_size;        // and remove from the buffer
		}
	    uart_set_irq_enables(uart1, true, true);
	}
	return c;
}

