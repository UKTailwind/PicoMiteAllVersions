/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
 * PicoMite MMBasic - Serial.h
 *
 * Serial communication interface definitions for COM1 and COM2 ports
 *
 * <COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
 * Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided with the distribution.
 * 3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the
 *    original copyright message be displayed on the console at startup (additional copyright messages may be added).
 * 4. All advertising materials mentioning features or use of this software must display the following acknowledgement:
 *    This product includes software developed by the <copyright holder>.
 * 5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote
 *    products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 **********************************************************************************************************************/

/* ==============================================================================================================
 * TOKEN TABLE
 * All other tokens (keywords, functions, operators) should be inserted in this table
 * ============================================================================================================== */
#ifdef INCLUDE_TOKEN_TABLE

#endif

#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)

#ifndef SERIAL_HEADER
#define SERIAL_HEADER

/* ==============================================================================================================
 * SERIAL CONFIGURATION CONSTANTS
 * ============================================================================================================== */
#define COM_DEFAULT_BAUD_RATE 9600
#define COM_DEFAULT_BUF_SIZE 1024
#define TX_BUFFER_SIZE 256

/* ==============================================================================================================
 * COM PORT MODE FLAGS
 * ============================================================================================================== */
#define COM1_9B 0b001 // 9 bit data enabled
#define COM1_DE 0b010 // RS485 enable flag in use

/* ==============================================================================================================
 * COM1 GLOBAL VARIABLES
 * ============================================================================================================== */
extern int com1;				  // true if COM1 is enabled
extern int com1_buf_size;		  // size of the buffer used to receive chars
extern int com1_baud;			  // baud rate
extern int com1_ilevel;			  // number of chars in buffer for an interrupt
extern int com1_TX_complete;	  // TX completion flag
extern volatile int com1complete; // completion flag
extern char *com1_interrupt;	  // pointer to the interrupt routine
extern unsigned char *com1Rx_buf; // pointer to the receive buffer
extern unsigned char *com1Tx_buf; // pointer to the transmit buffer
extern unsigned char com1_bit9;	  // used to track the 9th bit
extern uint16_t Rx1Buffer;		  // receive buffer

// Note: Ring buffer head/tail are managed internally
// extern volatile int com1Rx_head, com1Rx_tail;
// extern volatile int com1Tx_head, com1Tx_tail;

/* ==============================================================================================================
 * COM2 GLOBAL VARIABLES
 * ============================================================================================================== */
extern int com2;				  // true if COM2 is enabled
extern int com2_buf_size;		  // size of the buffer used to receive chars
extern int com2_baud;			  // baud rate
extern int com2_ilevel;			  // number of chars in buffer for an interrupt
extern int com2_TX_complete;	  // TX completion flag
extern volatile int com2complete; // completion flag
extern char *com2_interrupt;	  // pointer to the RX interrupt routine
extern char *com2_TX_interrupt;	  // pointer to the TX interrupt routine
extern unsigned char *com2Rx_buf; // pointer to the receive buffer
extern unsigned char *com2Tx_buf; // pointer to the transmit buffer
extern volatile int com2Rx_head;  // head of the ring buffer for COM2 Rx
extern volatile int com2Rx_tail;  // tail of the ring buffer for COM2 Rx
extern volatile int com2Tx_head;  // head of the ring buffer for COM2 Tx
extern volatile int com2Tx_tail;  // tail of the ring buffer for COM2 Tx

/* ==============================================================================================================
 * UART INTERRUPT HANDLERS
 * ============================================================================================================== */
extern void on_uart_irq0(void);
extern void on_uart_irq1(void);

/* ==============================================================================================================
 * SERIAL PORT FUNCTION PROTOTYPES
 * ============================================================================================================== */

/**
 * Open a serial port with specified configuration
 * @param spec Configuration specification string
 */
void SerialOpen(unsigned char *spec);

/**
 * Close a serial port
 * @param comnbr COM port number (1 or 2)
 */
void SerialClose(int comnbr);

/**
 * Send a character on the serial port
 * @param comnbr COM port number (1 or 2)
 * @param c Character to send
 * @return Character sent
 */
unsigned char SerialPutchar(int comnbr, unsigned char c);

/**
 * Check receive buffer status
 * @param comnbr COM port number (1 or 2)
 * @return Number of characters available in receive buffer
 */
int SerialRxStatus(int comnbr);

/**
 * Check transmit buffer status
 * @param comnbr COM port number (1 or 2)
 * @return Number of free spaces in transmit buffer
 */
int SerialTxStatus(int comnbr);

/**
 * Get a character from the serial port
 * @param comnbr COM port number (1 or 2)
 * @return Character received, or -1 if none available
 */
int SerialGetchar(int comnbr);

/**
 * Setup UART with specified parameters
 * @param uart UART number
 * @param s2 Stop bits configuration
 * @param b9 9-bit mode flag
 * @param b7 7-bit mode flag
 * @param baud Baud rate
 * @param inv Invert signal flag
 */
void setupuart(int uart, int s2, int b9, int b7, int baud, int inv);

/* ==============================================================================================================
 * CONSOLE AND PORT CONTROL FUNCTIONS
 * ============================================================================================================== */
extern void start_console(void);
extern void stop_console(void);
extern void start_com1(void);
extern void stop_com1e(void);
extern void start_com2(void);
extern void stop_com2(void);
extern void MX_USART1_UART_Init1(void);

#endif // SERIAL_HEADER
#endif // !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
	   /* @endcond */