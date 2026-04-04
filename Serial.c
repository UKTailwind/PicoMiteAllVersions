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
int com1 = 0;		  // true if COM1 is enabled
int com1_buf_size;	  // size of the buffer used to receive chars
int com1_baud = 0;	  // determines the baud rate
char *com1_interrupt; // pointer to the interrupt routine
int com1_ilevel;	  // number nbr of chars in the buffer for an interrupt
int com1_TX_complete = false;
unsigned char *com1Rx_buf;			   // pointer to the buffer for received characters
volatile int com1Rx_head, com1Rx_tail; // head and tail of the ring buffer for com1
unsigned char *com1Tx_buf;			   // pointer to the buffer for transmitted characters
volatile int com1Tx_head, com1Tx_tail; // head and tail of the ring buffer for com1
volatile int com1complete = 1;
uint16_t Rx1Buffer;
char com1_mode;				 // keeps track of the settings for com1
unsigned char com1_bit9 = 0; // used to track the 9th bit
extern uint32_t ticks_per_microsecond;

// variables for com2
int com2 = 0;		  // true if COM2 is enabled
int com2_buf_size;	  // size of the buffer used to receive chars
int com2_baud = 0;	  // determines the baud rate
char *com2_interrupt; // pointer to the interrupt routine
int com2_ilevel;	  // number nbr of chars in the buffer for an interrupt
int com2_TX_complete = false;
unsigned char *com2Rx_buf;			   // pointer to the buffer for received characters
volatile int com2Rx_head, com2Rx_tail; // head and tail of the ring buffer for com2 Rx
unsigned char *com2Tx_buf;			   // pointer to the buffer for transmitted characters
volatile int com2Tx_head, com2Tx_tail; // head and tail of the ring buffer for com2 Tx
volatile int com2complete = 1;
char com2_mode;				 // keeps track of the settings for com2
unsigned char com2_bit9 = 0; // used to track the 9th bit

// variables for USB CDC host ports (COM3-COM6)
#ifdef USBKEYBOARD
#include "tusb.h"

int com3 = 0;						   // true if COM3 (CDC idx 0) is enabled
int com3_buf_size;					   // size of the buffer used to receive chars
char *com3_interrupt;				   // pointer to the interrupt routine
int com3_ilevel;					   // number of chars in buffer for an interrupt
unsigned char *com3Rx_buf;			   // pointer to the buffer for received characters
volatile int com3Rx_head, com3Rx_tail; // head and tail of the ring buffer for com3

int com4 = 0;						   // true if COM4 (CDC idx 1) is enabled
int com4_buf_size;					   // size of the buffer used to receive chars
char *com4_interrupt;				   // pointer to the interrupt routine
int com4_ilevel;					   // number of chars in buffer for an interrupt
unsigned char *com4Rx_buf;			   // pointer to the buffer for received characters
volatile int com4Rx_head, com4Rx_tail; // head and tail of the ring buffer for com4

int com5 = 0;						   // true if COM5 (CDC idx 2) is enabled
int com5_buf_size;					   // size of the buffer used to receive chars
char *com5_interrupt;				   // pointer to the interrupt routine
int com5_ilevel;					   // number of chars in buffer for an interrupt
unsigned char *com5Rx_buf;			   // pointer to the buffer for received characters
volatile int com5Rx_head, com5Rx_tail; // head and tail of the ring buffer for com5

int com6 = 0;						   // true if COM6 (CDC idx 3) is enabled
int com6_buf_size;					   // size of the buffer used to receive chars
char *com6_interrupt;				   // pointer to the interrupt routine
int com6_ilevel;					   // number of chars in buffer for an interrupt
unsigned char *com6Rx_buf;			   // pointer to the buffer for received characters
volatile int com6Rx_head, com6Rx_tail; // head and tail of the ring buffer for com6

// Helper arrays indexed by CDC idx (0-3) to access per-port state
// These pointers are set once at startup and provide a uniform way to
// access per-port variables from callbacks without large switch/if chains.
static int *cdc_com_flag[4];		  // -> com3..com6
static int *cdc_buf_size[4];		  // -> com3_buf_size..com6_buf_size
static unsigned char **cdc_rx_buf[4]; // -> com3Rx_buf..com6Rx_buf
static volatile int *cdc_rx_head[4];  // -> com3Rx_head..com6Rx_head
static volatile int *cdc_rx_tail[4];  // -> com3Rx_tail..com6Rx_tail
static char **cdc_interrupt[4];		  // -> com3_interrupt..com6_interrupt
static int *cdc_ilevel[4];			  // -> com3_ilevel..com6_ilevel

static bool cdc_arrays_inited = false;
static void cdc_init_arrays(void)
{
	if (cdc_arrays_inited)
		return;
	cdc_com_flag[0] = &com3;
	cdc_com_flag[1] = &com4;
	cdc_com_flag[2] = &com5;
	cdc_com_flag[3] = &com6;
	cdc_buf_size[0] = &com3_buf_size;
	cdc_buf_size[1] = &com4_buf_size;
	cdc_buf_size[2] = &com5_buf_size;
	cdc_buf_size[3] = &com6_buf_size;
	cdc_rx_buf[0] = &com3Rx_buf;
	cdc_rx_buf[1] = &com4Rx_buf;
	cdc_rx_buf[2] = &com5Rx_buf;
	cdc_rx_buf[3] = &com6Rx_buf;
	cdc_rx_head[0] = &com3Rx_head;
	cdc_rx_head[1] = &com4Rx_head;
	cdc_rx_head[2] = &com5Rx_head;
	cdc_rx_head[3] = &com6Rx_head;
	cdc_rx_tail[0] = &com3Rx_tail;
	cdc_rx_tail[1] = &com4Rx_tail;
	cdc_rx_tail[2] = &com5Rx_tail;
	cdc_rx_tail[3] = &com6Rx_tail;
	cdc_interrupt[0] = &com3_interrupt;
	cdc_interrupt[1] = &com4_interrupt;
	cdc_interrupt[2] = &com5_interrupt;
	cdc_interrupt[3] = &com6_interrupt;
	cdc_ilevel[0] = &com3_ilevel;
	cdc_ilevel[1] = &com4_ilevel;
	cdc_ilevel[2] = &com5_ilevel;
	cdc_ilevel[3] = &com6_ilevel;
	cdc_arrays_inited = true;
}

// Fixed mapping: COM3 = CDC idx 0 (ch 5), COM4 = idx 1 (ch 6), COM5 = idx 2 (ch 7), COM6 = idx 3 (ch 8)

// TinyUSB CDC host callbacks
void tuh_cdc_mount_cb(uint8_t idx)
{
	if (idx >= 4)
		return;
	cdc_init_arrays();
	if (!CurrentLinePtr)
	{
		MMPrintString("USB CDC Device Connected on channel ");
		PInt(idx + 5);
		MMPrintString(" (COM");
		PInt(idx + 3);
		MMPrintString(")\r\n> ");
	}
	// If the port was already open by BASIC, re-assert DTR/RTS for seamless reconnect
	if (*cdc_com_flag[idx])
		tuh_cdc_set_control_line_state(idx, CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS, NULL, 0);
}

void tuh_cdc_umount_cb(uint8_t idx)
{
	if (idx >= 4)
		return;
	// If the port is open by BASIC, keep the state intact so reconnection works
	// transparently. Only print a message. SerialPutchar already guards on tuh_cdc_mounted().
	if (!CurrentLinePtr)
	{
		MMPrintString("USB CDC Device Disconnected on channel ");
		PInt(idx + 5);
		MMPrintString(" (COM");
		PInt(idx + 3);
		MMPrintString(")\r\n> ");
	}
	// Don't tear down BASIC state here - the port stays "open" so that
	// a reconnect resumes transparently. Cleanup happens in SerialClose.
}

void tuh_cdc_rx_cb(uint8_t idx)
{
	uint8_t buf[64];
	if (idx >= 4)
	{
		while (tuh_cdc_read(idx, buf, sizeof(buf)) > 0)
		{
		}
		return;
	}
	cdc_init_arrays();

	if (*cdc_com_flag[idx] && *cdc_rx_buf[idx] != NULL)
	{
		uint32_t count;
		int bsize = *cdc_buf_size[idx];
		unsigned char *rxbuf = *cdc_rx_buf[idx];
		volatile int *head = cdc_rx_head[idx];
		volatile int *tail = cdc_rx_tail[idx];
		while ((count = tuh_cdc_read(idx, buf, sizeof(buf))) > 0)
		{
			for (uint32_t i = 0; i < count; i++)
			{
				rxbuf[*head] = buf[i];
				int next = (*head + 1) % bsize;
				if (next == *tail)
				{
					*tail = (*tail + 1) % bsize;
				}
				*head = next;
			}
		}
	}
	else
	{
		// Port not open - drain the FIFO anyway to prevent the device from blocking
		while (tuh_cdc_read(idx, buf, sizeof(buf)) > 0)
		{
		}
	}
}

void tuh_cdc_tx_complete_cb(uint8_t idx)
{
	// TX complete - nothing special needed, writes are synchronous from BASIC's perspective
}
#endif // USBKEYBOARD
// uart interrupt handler
void on_uart_irq0()
{
	if (uart_is_readable(uart0))
	{
		char cc = uart_getc(uart0);
		if (!(Option.SerialConsole & 1))
		{
			if (GPSchannel == 1 || PinDef[Option.GPSTX].mode & UART0TX)
			{
				*gpsbuf = cc;
				gpsbuf++;
				gpscount++;
				if ((char)cc == 10 || gpscount == 128)
				{
					if (gpscurrent)
					{
						*gpsbuf = 0;
						gpscurrent = 0;
						gpscount = 0;
						gpsbuf = gpsbuf1;
						gpsready = gpsbuf2;
					}
					else
					{
						*gpsbuf = 0;
						gpscurrent = 1;
						gpscount = 0;
						gpsbuf = gpsbuf2;
						gpsready = gpsbuf1;
					}
				}
			}
			else
			{
				com1Rx_buf[com1Rx_head] = cc;					 // store the byte in the ring buffer
				com1Rx_head = (com1Rx_head + 1) % com1_buf_size; // advance the head of the queue
				if (com1Rx_head == com1Rx_tail)
				{													 // if the buffer has overflowed
					com1Rx_tail = (com1Rx_tail + 1) % com1_buf_size; // throw away the oldest char
				}
			}
		}
		else
		{
			ConsoleRxBuf[ConsoleRxBufHead] = cc; // store the byte in the ring buffer
			if (BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey)
			{										 // if the user wants to stop the progran
				MMAbort = true;						 // set the flag for the interpreter to see
				ConsoleRxBufHead = ConsoleRxBufTail; // empty the buffer
			}
			else
			{
				ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE; // advance the head of the queue
				if (ConsoleRxBufHead == ConsoleRxBufTail)
				{																	 // if the buffer has overflowed
					ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
				}
			}
		}
	}
	if (uart_is_writable(uart0))
	{
		if (!(Option.SerialConsole & 1))
		{
			if (com1Tx_head != com1Tx_tail)
			{
				uart_putc_raw(uart0, com1Tx_buf[com1Tx_tail]);
				com1Tx_tail = (com1Tx_tail + 1) % TX_BUFFER_SIZE; // advance the tail of the queue
			}
			else
			{
				uart_set_irq_enables(uart0, true, false);
				com1_TX_complete = true;
			}
		}
		else
		{
			if (ConsoleTxBufTail != ConsoleTxBufHead)
			{
				uart_putc_raw(uart0, ConsoleTxBuf[ConsoleTxBufTail]);
				ConsoleTxBufTail = (ConsoleTxBufTail + 1) % CONSOLE_TX_BUF_SIZE; // advance the tail of the queue
			}
			else
			{
				uart_set_irq_enables(uart0, true, false);
			}
		}
	}
}
void on_uart_irq1()
{
	if (uart_is_readable(uart1))
	{
		char cc = uart_getc(uart1);
		if (!(Option.SerialConsole & 2))
		{
			if (GPSchannel == 2 || PinDef[Option.GPSTX].mode & UART1TX)
			{
				*gpsbuf = cc;
				gpsbuf++;
				gpscount++;
				if ((char)cc == 10 || gpscount == 128)
				{
					if (gpscurrent)
					{
						*gpsbuf = 0;
						gpscurrent = 0;
						gpscount = 0;
						gpsbuf = gpsbuf1;
						gpsready = gpsbuf2;
					}
					else
					{
						*gpsbuf = 0;
						gpscurrent = 1;
						gpscount = 0;
						gpsbuf = gpsbuf2;
						gpsready = gpsbuf1;
					}
				}
			}
			else
			{
				com2Rx_buf[com2Rx_head] = cc;					 // store the byte in the ring buffer
				com2Rx_head = (com2Rx_head + 1) % com2_buf_size; // advance the head of the queue
				if (com2Rx_head == com2Rx_tail)
				{													 // if the buffer has overflowed
					com2Rx_tail = (com2Rx_tail + 1) % com2_buf_size; // throw away the oldest char
				}
			}
		}
		else
		{
			ConsoleRxBuf[ConsoleRxBufHead] = cc; // store the byte in the ring buffer
			if (BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey)
			{										 // if the user wants to stop the progran
				MMAbort = true;						 // set the flag for the interpreter to see
				ConsoleRxBufHead = ConsoleRxBufTail; // empty the buffer
			}
			else
			{
				ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE; // advance the head of the queue
				if (ConsoleRxBufHead == ConsoleRxBufTail)
				{																	 // if the buffer has overflowed
					ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
				}
			}
		}
	}
	if (uart_is_writable(uart1))
	{
		if (!(Option.SerialConsole & 2))
		{
			if (com2Tx_head != com2Tx_tail)
			{
				uart_putc_raw(uart1, com2Tx_buf[com2Tx_tail]);
				com2Tx_tail = (com2Tx_tail + 1) % TX_BUFFER_SIZE; // advance the tail of the queue
			}
			else
			{
				uart_set_irq_enables(uart1, true, false);
				com2_TX_complete = true;
			}
		}
		else
		{
			if (ConsoleTxBufTail != ConsoleTxBufHead)
			{
				uart_putc_raw(uart1, ConsoleTxBuf[ConsoleTxBufTail]);
				ConsoleTxBufTail = (ConsoleTxBufTail + 1) % CONSOLE_TX_BUF_SIZE; // advance the tail of the queue
			}
			else
			{
				uart_set_irq_enables(uart1, true, false);
			}
		}
	}
}

/***************************************************************************************************
Initialise the serial function including the timer and interrupts.
****************************************************************************************************/

#define UART_ID (uart ? uart1 : uart0)
void invert_serial(int uart)
{
	int txpin, rxpin;
	if (uart == 0)
	{
		txpin = PinDef[UART0TXpin].GPno;
		rxpin = PinDef[UART0RXpin].GPno;
	}
	else
	{
		txpin = PinDef[UART1TXpin].GPno;
		rxpin = PinDef[UART1RXpin].GPno;
	}
	gpio_set_outover(txpin, GPIO_OVERRIDE_INVERT);
	gpio_set_inover(rxpin, GPIO_OVERRIDE_INVERT);
}
void MIPS16 setupuart(int uart, int s2, int parity, int b7, int baud, int inv)
{
	uart_init(UART_ID, baud);
	uart_set_hw_flow(UART_ID, false, false);
	uart_set_format(UART_ID, b7, s2, parity);
	uart_set_fifo_enabled(UART_ID, false);
	if (inv)
		invert_serial(uart);
	int UART_IRQ = (UART_ID == uart0 ? UART0_IRQ : UART1_IRQ);
	if (uart)
	{
		irq_set_exclusive_handler(UART_IRQ, on_uart_irq1);
		irq_set_enabled(UART_IRQ, true);
	}
	else
	{
		irq_set_exclusive_handler(UART_IRQ, on_uart_irq0);
		irq_set_enabled(UART_IRQ, true);
	}
	uart_set_irq_enables(UART_ID, true, false);
	uart_set_irq_enables(UART_ID, true, false);
}
/***************************************************************************************************
Initialise the serial function including the timer and interrupts.
****************************************************************************************************/
void MIPS16 SerialOpen(unsigned char *spec)
{
	int baud, i, s2, parity, b7, bufsize, inv = 0, ilevel = 1;
	char *interrupt;

	getargs(&spec, 21, (unsigned char *)":,"); // this is a macro and must be the first executable stmt
	if (argc != 2 && (argc & 0x01) == 0)
		error("COM specification");

	b7 = 8;
	parity = UART_PARITY_NONE;
	s2 = 1;
	for (i = 0; i < 5; i++)
	{
		if (str_equal(argv[argc - 1], (unsigned char *)"EVEN"))
		{
			if (parity)
				SyntaxError();
			else
			{
				parity = UART_PARITY_EVEN;
				argc -= 2;
			} // set even parity
		}
		if (str_equal(argv[argc - 1], (unsigned char *)"ODD"))
		{
			if (parity)
				SyntaxError();
			else
			{
				parity = UART_PARITY_ODD;
				argc -= 2;
			} // set even parity
		}
		if (str_equal(argv[argc - 1], (unsigned char *)"INV"))
		{
			inv = 1;
			argc -= 2;
		}; // invert the serial port
		if (str_equal(argv[argc - 1], (unsigned char *)"DE"))
			error("DE not Supported"); // get the two stop bit option
		if (str_equal(argv[argc - 1], (unsigned char *)"OC"))
			error("OC not Supported"); // get the two stop bit option
		if (str_equal(argv[argc - 1], (unsigned char *)"9BIT"))
			error("9BIT not Supported"); // get the two stop bit option
		if (str_equal(argv[argc - 1], (unsigned char *)"S2"))
		{
			s2 = 2;
			argc -= 2;
		} // get the two stop bit option
		if (str_equal(argv[argc - 1], (unsigned char *)"7BIT"))
		{
			b7 = 7;
			argc -= 2;
		} // set the 7 bit byte option
	}
	if (argc < 1 || argc > 9)
		error("COM specification");

	if (argc >= 3 && *argv[2])
	{
		baud = getint(argv[2], Option.CPU_Speed * 1000 / 16 / 65535, 921600); // get the baud rate as a number
	}
	else
		baud = COM_DEFAULT_BAUD_RATE;

	if (argc >= 5 && *argv[4])
		bufsize = getinteger(argv[4]); // get the buffer size as a number
	else
		bufsize = COM_DEFAULT_BUF_SIZE;

	if (argc >= 7)
	{
		InterruptUsed = true;
		argv[6] = (unsigned char *)strupr((char *)argv[6]);
		interrupt = (char *)GetIntAddress(argv[6]); // get the interrupt location
	}
	else
		interrupt = NULL;

	if (argc == 9)
	{
		ilevel = getinteger(argv[8]); // get the buffer level for interrupt as a number
		if (ilevel < 1 || ilevel > bufsize)
			error("COM specification");
	}
	else
		ilevel = 1;

	/*	if(argc >= 11) {
			InterruptUsed = true;
			argv[6]=strupr(argv[10]);
			TXinterrupt = GetIntAddress(argv[10]);							// get the interrupt location
		} else
			TXinterrupt = NULL;
	*/

	if (spec[3] == '1')
	{
		///////////////////////////////// this is COM1 ////////////////////////////////////

		if (com1)
			StandardError(31);
		if (UART0TXpin == 99 || UART0RXpin == 99)
			error("Pins not set for COM1");

		com1_buf_size = bufsize; // extracted from the comspec above
		com1_interrupt = interrupt;
		com1_ilevel = ilevel;

		// setup for receive
		com1Rx_buf = GetMemory(com1_buf_size); // setup the buffer
		com1Rx_head = com1Rx_tail = 0;
		ExtCfg(UART0RXpin, EXT_COM_RESERVED, 0); // reserve the pin for com use

		// setup for transmit
		com1Tx_buf = GetMemory(TX_BUFFER_SIZE); // setup the buffer
		com1Tx_head = com1Tx_tail = 0;
		ExtCfg(UART0TXpin, EXT_COM_RESERVED, 0);

		setupuart(0, s2, parity, b7, baud, inv);
		com1 = true;
		uSec(1000);
		com1Rx_head = com1Rx_tail = 0;
		com1Tx_head = com1Tx_tail = 0;
	}
	else if (spec[3] == '2')
	{
		///////////////////////////////// this is COM2 ////////////////////////////////////

		if (com2)
			StandardError(31);
		if (UART1TXpin == 99 || UART1RXpin == 99)
			error("Pins not set for COM2");

		com2_buf_size = bufsize; // extracted from the comspec above
		com2_interrupt = interrupt;
		com2_ilevel = ilevel;
		//		com2_TX_interrupt = TXinterrupt;
		//		com2_TX_complete = false;

		// setup for receive
		com2Rx_buf = GetMemory(com2_buf_size); // setup the buffer
		com2Rx_head = com2Rx_tail = 0;
		ExtCfg(UART1RXpin, EXT_COM_RESERVED, 0); // reserve the pin for com use

		// setup for transmit
		com2Tx_buf = GetMemory(TX_BUFFER_SIZE); // setup the buffer
		com2Tx_head = com2Tx_tail = 0;
		ExtCfg(UART1TXpin, EXT_COM_RESERVED, 0); // reserve the pin for com use
		setupuart(1, s2, parity, b7, baud, inv);
		com2 = true;
		uSec(1000);
		com2Rx_head = com2Rx_tail = 0;
		com2Tx_head = com2Tx_tail = 0;
	}
#ifdef USBKEYBOARD
	else if (spec[3] >= '3' && spec[3] <= '6')
	{
		///////////////////////////////// this is COM3-COM6 (USB CDC host) ////////////////////////////////////
		int comnbr = spec[3] - '0'; // 3, 4, 5 or 6
		int cdc_idx = comnbr - 3;	// 0, 1, 2 or 3

		cdc_init_arrays();

		if (*cdc_com_flag[cdc_idx])
			StandardError(31);

		if (!tuh_cdc_mounted(cdc_idx))
		{
			char errmsg[48];
			sprintf(errmsg, "No USB CDC device on channel %d for COM%d", cdc_idx + 5, comnbr);
			error(errmsg);
		}

		*cdc_buf_size[cdc_idx] = bufsize;
		*cdc_interrupt[cdc_idx] = interrupt;
		*cdc_ilevel[cdc_idx] = ilevel;

		// setup for receive
		*cdc_rx_buf[cdc_idx] = GetMemory(bufsize);
		*cdc_rx_head[cdc_idx] = 0;
		*cdc_rx_tail[cdc_idx] = 0;

		*cdc_com_flag[cdc_idx] = true;

		// Assert DTR (and RTS) to signal the device we are ready
		tuh_cdc_set_control_line_state(cdc_idx, CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS, NULL, 0);
	}
#endif // USBKEYBOARD
}

/***************************************************************************************************
Close a serial port.
****************************************************************************************************/
void MIPS16 SerialClose(int comnbr)
{

	if (comnbr == 1 && com1)
	{
		uart_deinit(uart0);
		com1 = false;
		com1_interrupt = NULL;
		if (UART0RXpin != 99)
			ExtCfg(UART0RXpin, EXT_NOT_CONFIG, 0);
		if (UART0TXpin != 99)
			ExtCfg(UART0TXpin, EXT_NOT_CONFIG, 0);
		if (com1Rx_buf != NULL)
		{
			FreeMemory(com1Rx_buf);
			com1Rx_buf = NULL;
		}
		if (com1Tx_buf != NULL)
		{
			FreeMemory(com1Tx_buf);
			com1Tx_buf = NULL;
		}
	}

	else if (comnbr == 2 && com2)
	{
		uart_deinit(uart1);
		com2 = false;
		com2_interrupt = NULL;
		if (UART1RXpin != 99)
			ExtCfg(UART1RXpin, EXT_NOT_CONFIG, 0);
		if (UART1TXpin != 99)
			ExtCfg(UART1TXpin, EXT_NOT_CONFIG, 0);
		if (com2Rx_buf != NULL)
		{
			FreeMemory(com2Rx_buf);
			com2Rx_buf = NULL;
		}
		if (com2Tx_buf != NULL)
		{
			FreeMemory(com2Tx_buf);
			com2Tx_buf = NULL;
		}
	}
#ifdef USBKEYBOARD
	else if (comnbr >= 3 && comnbr <= 6)
	{
		int cdc_idx = comnbr - 3;
		cdc_init_arrays();
		if (*cdc_com_flag[cdc_idx])
		{
			if (tuh_cdc_mounted(cdc_idx))
				tuh_cdc_set_control_line_state(cdc_idx, 0, NULL, 0);
			*cdc_com_flag[cdc_idx] = false;
			*cdc_interrupt[cdc_idx] = NULL;
			if (*cdc_rx_buf[cdc_idx] != NULL)
			{
				FreeMemory(*cdc_rx_buf[cdc_idx]);
				*cdc_rx_buf[cdc_idx] = NULL;
			}
		}
	}
#endif // USBKEYBOARD
}

/***************************************************************************************************
Add a character to the serial output buffer.
****************************************************************************************************/
unsigned char SerialPutchar(int comnbr, unsigned char c)
{
	if (comnbr == 1)
	{
		while (com1Tx_tail == ((com1Tx_head + 1) % TX_BUFFER_SIZE)) // wait if the buffer is full
			if (MMAbort)
			{								   // allow the user to abort a hung serial port
				com1Tx_tail = com1Tx_head = 0; // clear the buffer
				longjmp(mark, 1);			   // and abort
			}
		int empty = uart_is_writable(uart0);
		com1Tx_buf[com1Tx_head] = c;					  // add the char
		com1Tx_head = (com1Tx_head + 1) % TX_BUFFER_SIZE; // advance the head of the queue
		if (empty)
		{
			uart_set_irq_enables(uart0, true, true);
			irq_set_pending(UART0_IRQ);
		}
	}
	else if (comnbr == 2)
	{
		while (com2Tx_tail == ((com2Tx_head + 1) % TX_BUFFER_SIZE)) // wait if the buffer is full
			if (MMAbort)
			{								   // allow the user to abort a hung serial port
				com2Tx_tail = com2Tx_head = 0; // clear the buffer
				longjmp(mark, 1);			   // and abort
			}
		int empty = uart_is_writable(uart1);
		com2Tx_buf[com2Tx_head] = c;					  // add the char
		com2Tx_head = (com2Tx_head + 1) % TX_BUFFER_SIZE; // advance the head of the queue
		if (empty)
		{
			uart_set_irq_enables(uart1, true, true);
			irq_set_pending(UART1_IRQ);
		}
	}
#ifdef USBKEYBOARD
	else if (comnbr >= 3 && comnbr <= 6)
	{
		int idx = comnbr - 3;
		if (tuh_cdc_mounted(idx))
		{
			// Write the byte directly via TinyUSB CDC host; wait if write buffer is full
			while (tuh_cdc_write_available(idx) == 0)
			{
				tuh_task(); // keep the USB stack running while we wait
				if (MMAbort)
					longjmp(mark, 1);
			}
			tuh_cdc_write(idx, &c, 1);
			tuh_cdc_write_flush(idx);
		}
	}
#endif // USBKEYBOARD
	return c;
}

/***************************************************************************************************
Get the status the serial receive buffer.
Returns the number of characters waiting in the buffer
****************************************************************************************************/
int SerialRxStatus(int comnbr)
{
	int i = 0;
	if (comnbr == 1)
	{
		uart_set_irq_enables(uart0, false, true);
		i = com1Rx_head - com1Rx_tail;
		uart_set_irq_enables(uart0, true, true);
		if (i < 0)
			i += com1_buf_size;
	}
	else if (comnbr == 2)
	{
		uart_set_irq_enables(uart1, false, true);
		i = com2Rx_head - com2Rx_tail;
		uart_set_irq_enables(uart1, true, true);
		if (i < 0)
			i += com2_buf_size;
	}
#ifdef USBKEYBOARD
	else if (comnbr >= 3 && comnbr <= 6)
	{
		int idx = comnbr - 3;
		cdc_init_arrays();
		i = *cdc_rx_head[idx] - *cdc_rx_tail[idx];
		if (i < 0)
			i += *cdc_buf_size[idx];
	}
#endif // USBKEYBOARD
	return i;
}

/***************************************************************************************************
Get the status the serial transmit buffer.
Returns the number of characters waiting in the buffer
****************************************************************************************************/
int SerialTxStatus(int comnbr)
{
	int i = 0;
	if (comnbr == 1)
	{
		i = com1Tx_head - com1Tx_tail;
		if (i < 0)
			i += TX_BUFFER_SIZE;
	}
	else if (comnbr == 2)
	{
		i = com2Tx_head - com2Tx_tail;
		if (i < 0)
			i += TX_BUFFER_SIZE;
	}
#ifdef USBKEYBOARD
	else if (comnbr >= 3 && comnbr <= 6)
	{
		// CDC host TX is written directly via TinyUSB, no local buffer
		i = 0;
	}
#endif // USBKEYBOARD
	return i;
}

/***************************************************************************************************
Get a character from the serial receive buffer.
Note that this is returned as an integer and -1 means that there are no characters available
****************************************************************************************************/
int SerialGetchar(int comnbr)
{
	int c;
	c = -1; // -1 is no data
	if (comnbr == 1)
	{
		uart_set_irq_enables(uart0, false, true);
		if (com1Rx_head != com1Rx_tail)
		{													 // if the queue has something in it
			c = com1Rx_buf[com1Rx_tail];					 // get the char
			com1Rx_tail = (com1Rx_tail + 1) % com1_buf_size; // and remove from the buffer
		}
		uart_set_irq_enables(uart0, true, true);
	}
	else if (comnbr == 2)
	{

		uart_set_irq_enables(uart1, false, true);
		if (com2Rx_head != com2Rx_tail)
		{													 // if the queue has something in it
			c = com2Rx_buf[com2Rx_tail];					 // get the char
			com2Rx_tail = (com2Rx_tail + 1) % com2_buf_size; // and remove from the buffer
		}
		uart_set_irq_enables(uart1, true, true);
	}
#ifdef USBKEYBOARD
	else if (comnbr >= 3 && comnbr <= 6)
	{
		int idx = comnbr - 3;
		cdc_init_arrays();
		if (*cdc_rx_head[idx] != *cdc_rx_tail[idx])
		{
			c = (*cdc_rx_buf[idx])[*cdc_rx_tail[idx]];
			*cdc_rx_tail[idx] = (*cdc_rx_tail[idx] + 1) % *cdc_buf_size[idx];
		}
	}
#endif // USBKEYBOARD
	return c;
}
