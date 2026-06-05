/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

Custom.h

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
/* Universal Custom.h stays preprocessor-clean and does not pull in
 * Pico WEB lwIP/CYW43 headers or declarations. */
#include "port_config.h"

/* ********************************************************************************
 the C language function associated with commands, functions or operators should be
 declared here
**********************************************************************************/
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
// format:
//      void cmd_???(void)
//      void fun_???(void)
//      void op_???(void)
extern uint8_t pioTXlast[4][3];
extern char * pioRXinterrupts[4][3];
extern char * pioTXinterrupts[4][3];
/* closeMQTT is the only WEB-ish symbol core code needs to reference
 * unconditionally (ClearExternalIO calls it during teardown). The
 * strong implementation lives in MMMqtt.c on WEB builds; MM_Misc.c
 * provides a no-op fallback on every other target. */
extern void closeMQTT(void);
extern int piointerrupt;
extern char * DMAinterruptRX;
extern char * DMAinterruptTX;
extern uint32_t dma_rx_chan;
extern uint32_t dma_tx_chan;
extern uint32_t dma_rx_chan2;
extern uint32_t dma_tx_chan2;
extern int dma_tx_pio;
extern int dma_tx_sm;
extern int dma_rx_pio;
extern int dma_rx_sm;
extern int dirOK;
extern bool PIO0;
extern bool PIO1;
extern bool PIO2;
extern uint64_t piomap[];

extern uint8_t nextline[4];

#define TCP_READ_BUFFER_SIZE 2048

#endif

/*  @endcond */
