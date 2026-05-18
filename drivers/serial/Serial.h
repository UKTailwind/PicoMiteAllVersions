/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

Serial.h

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

/* ********************************************************************************
 All other tokens (keywords, functions, operators) should be inserted in this table
**********************************************************************************/
#ifdef INCLUDE_TOKEN_TABLE


#endif
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
// General definitions used by other modules

#ifndef SERIAL_HEADER
#define SERIAL_HEADER

#define	COM_DEFAULT_BAUD_RATE       9600
#define	COM_DEFAULT_BUF_SIZE	    1024
#define TX_BUFFER_SIZE              256

// global variables
	extern int com1;														// true if COM1 is enabled
	extern int com1_buf_size;													// size of the buffer used to receive chars
	extern int com1_baud;													// determines the baud rate
	extern char *com1_interrupt;												// pointer to the interrupt routine
	extern int com1_TX_complete;
	extern int com1_ilevel;													// number nbr of chars in the buffer for an interrupt
	extern unsigned char *com1Rx_buf;											// pointer to the buffer for received characters
//	extern volatile int com1Rx_head, com1Rx_tail;								// head and tail of the ring buffer for com1
	extern unsigned char *com1Tx_buf;											// pointer to the buffer for transmitted characters
//	extern volatile int com1Tx_head, com1Tx_tail;								// head and tail of the ring buffer for com1
	extern volatile int com1complete;
	extern uint16_t Rx1Buffer;
	#define COM1_9B       0b001                                         // 9 bit data enabled
	#define COM1_DE       0b010                                         // RS485 enable flag in use
//	char com2_mode;                                                     // keeps track of the settings for com1
	extern unsigned char com1_bit9;                                        // used to track the 9th bit


	// variables for com2
	extern int com2;														// true if COM2 is enabled
	extern int com2_buf_size;													// size of the buffer used to receive chars
	extern int com2_baud;													// determines the baud rate
	extern char *com2_interrupt, *com2_TX_interrupt;												// pointer to the interrupt routine
	extern int com2_TX_complete;
	extern int com2_ilevel;													// number nbr of chars in the buffer for an interrupt
	extern unsigned char *com2Rx_buf;											// pointer to the buffer for received characters
	extern volatile int com2Rx_head, com2Rx_tail;								// head and tail of the ring buffer for com2 Rx
	extern unsigned char *com2Tx_buf;											// pointer to the buffer for transmitted characters
	extern volatile int com2Tx_head, com2Tx_tail;								// head and tail of the ring buffer for com2 Tx
	extern volatile int com2complete;
	extern void on_uart_irq0();
	extern void on_uart_irq1();
// global functions
void SerialOpen(unsigned char *spec);
void SerialClose(int comnbr);
unsigned char SerialPutchar(int comnbr, unsigned char c);
int SerialRxStatus(int comnbr);
int SerialTxStatus(int comnbr);
int SerialGetchar(int comnbr);
void setupuart(int uart, int s2,int b9,int b7, int baud, int inv);
extern void start_console(void);
extern void stop_console(void);
extern void start_com1(void);
extern void stop_com1e(void);
extern void start_com2(void);
extern void stop_com2(void);
extern void MX_USART1_UART_Init1(void);

#endif
#endif
/*  @endcond */
