/***********************************************************************************************************************
PicoMite MMBasic

custom.c

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
/**
* @file Custom.c
* @author Geoff Graham, Peter Mather
* @brief Source for PIO, JSON and WEB MMBasic commands and function
*/
/**
 * @cond
 * The following section will be excluded from the documentation.
 */
#include <stdio.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hardware/dma.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#ifdef PICOMITEWEB
#define CJSON_NESTING_LIMIT 100
#include "cJSON.h"
#endif
#define STATIC static

/*************************************************************************************************************************
**************************************************************************************************************************
IMPORTANT:
This module is empty and should be used for your special functions and commands.  In the standard distribution this file  
will never be changed, so your code should be safe here.  You should avoid placing commands and functions in other files as
they may be changed and you would then need to re insert your changes in a new release of the source.

**************************************************************************************************************************
**************************************************************************************************************************/


/********************************************************************************************************************************************
 custom commands and functions
 each function is responsible for decoding a command
 all function names are in the form cmd_xxxx() (for a basic command) or fun_xxxx() (for a basic function) so, if you want to search for the
 function responsible for the NAME command look for cmd_name

 There are 4 items of information that are setup before the command is run.
 All these are globals.

 int cmdtoken	This is the token number of the command (some commands can handle multiple
				statement types and this helps them differentiate)

 unsigned char *cmdline	This is the command line terminated with a zero unsigned char and trimmed of leading
				spaces.  It may exist anywhere in memory (or even ROM).

 unsigned char *nextstmt	This is a pointer to the next statement to be executed.  The only thing a
				command can do with it is save it or change it to some other location.

 unsigned char *CurrentLinePtr  This is read only and is set to NULL if the command is in immediate mode.

 The only actions a command can do to change the program flow is to change nextstmt or
 execute longjmp(mark, 1) if it wants to abort the program.

 ********************************************************************************************************************************************/
/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/claim.h"
#define PIO_NUM(pio) ((pio) == pio0 ? 0 : (pio1 ? 1 :2))
#define CLKMIN ((Option.CPU_Speed*125)>>13)
#define CLKMAX (Option.CPU_Speed *1000)

#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
char *pioRXinterrupts[4][3]={0};
char *pioTXinterrupts[4][3]={0};
uint8_t pioTXlast[4][3]={0};
char *DMAinterruptRX=NULL;
char *DMAinterruptTX=NULL;
uint32_t dma_rx_chan = PIO_RX_DMA;
uint32_t dma_tx_chan = PIO_TX_DMA;
uint32_t dma_rx_chan2 = PIO_RX_DMA2;
uint32_t dma_tx_chan2 = PIO_TX_DMA2;
int dma_tx_pio;
int dma_tx_sm;
int dma_rx_pio;
int dma_rx_sm;
#ifdef PICOMITE
#ifdef rp2350
bool PIO0=true;
bool PIO1=true;
bool PIO2=true;
#else
bool PIO0=true;
bool PIO1=true;
bool PIO2=false;
#endif
#endif
#ifdef PICOMITEVGA
#ifdef rp2350
#ifdef HDMI
bool PIO0=true;
#else
bool PIO0=false;
#endif
bool PIO1=true;
bool PIO2=true;
#else
bool PIO0=false;
bool PIO1=true;
bool PIO2=false;
#endif
#endif
#ifdef PICOMITEWEB
#ifdef rp2350
bool PIO0=true;
bool PIO1=false;
bool PIO2=true;
#else
bool PIO0=true;
bool PIO1=false;
bool PIO2=false;
#endif
extern void setwifi(unsigned char *tp);
volatile bool TCPreceived=false;
char *TCPreceiveInterrupt=NULL;
#endif
#ifdef rp2350
uint64_t piomap[2]={0};
#endif
#define MAXLABEL 16
static int sidepins=0,sideopt=0,sidepinsdir=0, PIOlinenumber=0, PIOstart=0, p_wrap=31, p_wrap_target=0;
static int delaypossible=5;
static int checksideanddelay=0;
int dirOK=2;
static int *instructions=NULL;
static char *labelsfound=NULL, *labelsneeded=NULL;
int piointerrupt=0;
uint8_t nextline[4]={0};
static int pioinuse=99;
extern bool PIO2, PIO1, PIO0;
extern void on_pwm_wrap(void);
PIO pioi2s;
uint8_t i2ssm;
//#if defined( PICOMITEVGA) && !defined(HDMI)
#include "PicoMiteI2S.pio.h"
//#endif
static inline uint32_t pio_sm_calc_wrap(uint wrap_target, uint wrap) {
    uint32_t calc=0;
//    valid_params_if(PIO, wrap < PIO_INSTRUCTION_COUNT);
//    valid_params_if(PIO, wrap_target < PIO_INSTRUCTION_COUNT);
        return  (calc & ~(PIO_SM0_EXECCTRL_WRAP_TOP_BITS | PIO_SM0_EXECCTRL_WRAP_BOTTOM_BITS)) |
                (wrap_target << PIO_SM0_EXECCTRL_WRAP_BOTTOM_LSB) |
                (wrap << PIO_SM0_EXECCTRL_WRAP_TOP_LSB);
}
int calcsideanddelay(char *p, int sidepins, int maxdelaybits){
        int data=0;
        char *pp;
        if((pp=fstrstr(p,"side "))){
                pp+=5;
                skipspace(pp);
                char *ss=pp;
                char save=0;
                if((*ss>='0' && *ss<='9') || *ss=='&' ){
                        char *ppp=ss;
                        if(*ss=='&'){
                                if(!(toupper(ss[1])=='B' || toupper(ss[1])=='H' || toupper(ss[1])=='O')) error("Syntax");
                                ppp+=2;
                        } 
                        while(*ppp>='0' && *ppp<='9' && *ppp){ppp++;}
                        if(*ppp){
                                save=*ppp;
                                *ppp=',';
                        }
                        data=(int)(getint((unsigned char *)ss,0,((1<<sidepins)-1))<<(8+maxdelaybits));
                        if(sideopt)data|=0x1000;
                        *ppp=save;
                } else error("Syntax");
        }
        if((pp=fstrstr(p,"["))){
                pp++;
                char *s=strstr(pp,"]");
                *s=' ';
                data|=(getint((unsigned char *)pp,0,(1<<maxdelaybits)-1))<<8;
        }
        return data;
}
int getirqnum(char *p){
        int data=0;
        char *pp=p;
        int rel=0;
#ifdef rp2350
        int prev=0,next=0;
#endif
        skipspace(pp);
        char *ppp=pp;
        char save=0;
        while(*ppp>='0' && *ppp<='9' && *ppp){ppp++;}
        if(*ppp){
                save=*ppp;
                *ppp=',';
        }
        data=(int)getint((unsigned char *)pp,0,7);
        if(*ppp==',')*ppp=save;
        if((pp=fstrstr(p," rel")) && (pp[4]==0 || pp[4]==' ' || pp[4]==';'))rel=1;
#ifdef rp2350
        if((pp=fstrstr(p," prev")) && (pp[5]==0 || pp[5]==' ' || pp[5]==';'))prev=1;
        if((pp=fstrstr(p," next")) && (pp[5]==0 || pp[5]==' ' || pp[5]==';'))next=1;
        if(next)data|=0b11;
        if(prev)data|=0b01;
#endif
        if(rel){
                data|=0x10;
        }
        return data;
}
void pio_init(int pior, int sm, uint32_t pinctrl, uint32_t execctrl, uint32_t shiftctrl, int start, float clock, bool sideout, bool setout, bool outout){
        pio_sm_config mypio=pio_get_default_sm_config();
#ifdef rp2350
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ? pio0: pio1);
#endif
        clock=(float)Option.CPU_Speed*1000.0/clock;
        int sidebase=(pinctrl & 0b111110000000000)>>10;
        int setbase=(pinctrl &       0b1111100000)>>5;
        int outbase=(pinctrl &            0b11111);
        int sidecount=(pinctrl & 0xE0000000)>>29;
        int setcount=(pinctrl & 0x1C000000)>>26;
        int outcount=(pinctrl & 0x3F00000)>>20;
//        int inbase=(pinctrl & 0xF8000)>>15;
        int opt=(execctrl>>30) & 1;
        mypio.clkdiv = (uint32_t) (clock * (1 << 16));
        mypio.execctrl=execctrl;
        mypio.shiftctrl=shiftctrl;
        mypio.pinctrl=pinctrl;
        #ifdef rp2350
        #ifdef PICOMITEWEB
                for(int i = 1; i < (NBRPINS) ; i++) {
        #else
                for(int i = 1; i < (rp2350a ? 44:NBRPINS) ; i++) {
        #endif
        #else
        for(int i = 1; i < (NBRPINS) ; i++) {
        #endif
                if(CheckPin(i, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) {    // don't reset invalid or boot reserved pins
                        gpio_set_input_enabled(PinDef[i].GPno, true);
                }
        }
        if(sidecount && sideout)pio_sm_set_consecutive_pindirs(pio, sm, sidebase, sidecount-opt, true);
        if(outcount && outout)pio_sm_set_consecutive_pindirs(pio, sm, outbase, outcount, true);
        if(setcount && setout)pio_sm_set_consecutive_pindirs(pio, sm, setbase, setcount, true);
        pio_sm_set_config(pio, sm, &mypio);
        pio_sm_init(pio, sm, start, &mypio);
        pio_sm_clear_fifos(pio,sm);
}
int checkblock(char *p){
        int data=0;
        char *pp;
        if((pp=fstrstr(p,"IFFULL"))){
                if(!(pp[6]==' ' || pp[6]==0))error("Syntax");
                data=0b1000000;
        }
        if((pp=fstrstr(p,"NOBLOCK"))){
                if(!(pp[7]==' ' || pp[7]==0))error("Syntax");
                return data;
        }
        if((pp=fstrstr(p,"BLOCK"))){
                if(!(pp[5]==' ' || pp[5]==0))error("Syntax");
                data=0b100000;
        }
        return data;
}
extern uint I2SOff;
extern void start_vga_i2s(void);
extern void start_i2s(int pio, int sm);

void start_i2s(int pior, int sm){
    if(!Option.audio_i2s_bclk)return;
    i2ssm=sm;
#ifdef rp2350
        pioi2s = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        pioi2s = (pior==0 ? pio0: pio1);
#endif
#if !defined(PICOMITEVGA) || defined(HDMI)
#ifdef rp2350
        if(PinDef[Option.audio_i2s_bclk].GPno+1>31 || PinDef[Option.audio_i2s_data].GPno>31)pio_set_gpio_base(pioi2s,16);
#endif
        I2SOff = pio_add_program(pioi2s, &i2s_program);

#endif
        gpio_set_input_enabled(PinDef[Option.audio_i2s_data].GPno, true);
	gpio_set_input_enabled(PinDef[Option.audio_i2s_bclk].GPno, true);
	gpio_set_input_enabled(PinDef[Option.audio_i2s_bclk].GPno+1, true);
#ifdef rp2350
        gpio_set_function(PinDef[Option.audio_i2s_bclk].GPno  , pior==0 ? GPIO_FUNC_PIO0 : (pior==1? GPIO_FUNC_PIO1 : GPIO_FUNC_PIO2));
        gpio_set_function(PinDef[Option.audio_i2s_data].GPno  , pior==0 ? GPIO_FUNC_PIO0 : (pior==1? GPIO_FUNC_PIO1 : GPIO_FUNC_PIO2));
        gpio_set_function(PinDef[Option.audio_i2s_bclk].GPno+1, pior==0 ? GPIO_FUNC_PIO0 : (pior==1? GPIO_FUNC_PIO1 : GPIO_FUNC_PIO2));
#else
        gpio_set_function(PinDef[Option.audio_i2s_bclk].GPno  , pior==0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
        gpio_set_function(PinDef[Option.audio_i2s_data].GPno  , pior==0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
        gpio_set_function(PinDef[Option.audio_i2s_bclk].GPno+1, pior==0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
#endif
        ExtCfg(Option.audio_i2s_bclk, EXT_BOOT_RESERVED, 0);
        ExtCfg(Option.audio_i2s_data, EXT_BOOT_RESERVED, 0);
        ExtCfg(PINMAP[PinDef[Option.audio_i2s_bclk].GPno+1], EXT_BOOT_RESERVED, 0);

	// prepare default PIO program config
	pio_sm_config cfg = i2s_program_get_default_config(I2SOff);

	// map state machine's OUT and MOV pins	
	sm_config_set_out_pins(&cfg, PinDef[Option.audio_i2s_data].GPno, 1);

	// set sideset pins (BCLK and LCLK)
	sm_config_set_sideset_pins(&cfg, PinDef[Option.audio_i2s_bclk].GPno);
        sm_config_set_sideset(&cfg,2, false, false);

        // join FIFO to send only
	sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);

	// PIO clock divider
        float clockdiv=(Option.CPU_Speed*1000.0f)/(float)(44100*128);
//        pio_sm_set_clkdiv(pioi2s,i2ssm,clockdiv);
        sm_config_set_clkdiv(&cfg, clockdiv);
        sm_config_set_out_shift(&cfg,false,false,false);
        sm_config_set_in_shift(&cfg,false,false,false);

// initialize state machine
	pio_sm_init(pioi2s, sm, I2SOff, &cfg);
	pio_sm_set_consecutive_pindirs(pioi2s, sm, PinDef[Option.audio_i2s_data].GPno, 1, true);
	pio_sm_set_consecutive_pindirs(pioi2s, sm, PinDef[Option.audio_i2s_bclk].GPno, 2, true);
        pio_sm_set_enabled(pioi2s, sm, true);

        AUDIO_SLICE=Option.AUDIO_SLICE;
        AUDIO_WRAP=(Option.CPU_Speed*10)/441  - 1 ;
        pwm_set_wrap(AUDIO_SLICE, AUDIO_WRAP);
        pwm_clear_irq(AUDIO_SLICE);
        irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
        irq_set_enabled(PWM_IRQ_WRAP, true);
        irq_set_priority(PWM_IRQ_WRAP,255);
	pwm_set_enabled(AUDIO_SLICE, true); 
	if(pior==2)PIO2=false;
        else if(pior==1)PIO1=false;
        else PIO0=false;
}
int getGPpin(unsigned char *pinarg, int pio, int base){
	char code;
        int pin;
	if(!(code=codecheck(pinarg)))pinarg+=2;
	pin = getinteger(pinarg);
	if(!code)pin=codemap(pin);
        if(IsInvalidPin(pin)) error("Invalid pin");
        if((ExtCurrentConfig[pin] == EXT_PIO0_OUT && pio!=0) ||
                (ExtCurrentConfig[pin] == EXT_PIO1_OUT && pio!=1)
#ifdef rp2350
                || (ExtCurrentConfig[pin] == EXT_PIO2_OUT && pio!=2)
#endif
                || ExtCurrentConfig[pin] == EXT_BOOT_RESERVED
        ) error("Pin in use");
        if(PinDef[pin].GPno<base || PinDef[pin].GPno>base+31)error("Pin out of range for base %");
        return PinDef[pin].GPno;

}

/*  @endcond */
void MIPS16 cmd_pio(void){
    unsigned char *tp;
    #ifdef rp2350
    int dims[MAXDIM]={0};
    #else
    short dims[MAXDIM]={0};
    #endif
    tp = checkstring(cmdline, (unsigned char *)"EXECUTE");
    if(tp){
        int i;
        getargs(&tp, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",");
        if((argc & 0x01) == 0) error("Syntax");
        if(argc<5)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        int sm=getint(argv[2],0,3);
        for(i = 4; i < argc; i += 2) {
            pio_sm_exec(pio, sm, getint(argv[i],0,65535));
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"WRITE");
    if(tp){
        int i=6;
        getargs(&tp, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",");
        if((argc & 0x01) == 0) error("Syntax");
        if(argc<5)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        int sm=getint(argv[2],0,3);
        int count=getint(argv[4],0,MAX_ARG_COUNT-3);
        while(count--) {
            pio_sm_put_blocking(pio, sm, getint(argv[i],0,(int64_t)0xFFFFFFFF));
            i+=2;
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"DMA RX");
    if(tp){
        getargs(&tp, 13, (unsigned char *)",");
        if(checkstring(argv[0],(unsigned char *)"OFF")){
                dma_hw->abort = ((1u << dma_rx_chan2) | (1u << dma_rx_chan));
                if(dma_channel_is_busy(dma_rx_chan))dma_channel_abort(dma_rx_chan);
                if(dma_channel_is_busy(dma_rx_chan2))dma_channel_abort(dma_rx_chan2);
                return;
        }
        if(DMAinterruptRX || dma_channel_is_busy(dma_rx_chan)|| dma_channel_is_busy(dma_rx_chan2)) {
                dma_hw->abort = ((1u << dma_rx_chan2) | (1u << dma_rx_chan));
                if(dma_channel_is_busy(dma_rx_chan))dma_channel_abort(dma_rx_chan);
                if(dma_channel_is_busy(dma_rx_chan2))dma_channel_abort(dma_rx_chan2);
        }
        if(argc<7)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        int sm=getint(argv[2],0,3);
        dma_rx_pio=pior;
        dma_rx_sm=sm;
        int nbr=getint(argv[4],0,0xFFFFFFFF);
        uint32_t s_nbr=nbr;
        static uint32_t *a1int=NULL;
        int64_t *aint=NULL;
        int toarraysize=parseintegerarray(argv[6],&aint,4,1,dims, true);
        a1int=(uint32_t *)aint;
        if(argc>=9 && *argv[8]){
                if(nbr==0)error("Interrupt incopmpatible with continuous running");
                DMAinterruptRX=(char *)GetIntAddress(argv[8]);
                InterruptUsed=true;
        }
        int dmasize=DMA_SIZE_32;
        if(argc>=11 && *argv[10]){
                dmasize=getinteger(argv[10]);
                if(!(dmasize==8 || dmasize==16 || dmasize==32))error("Invalid transfer size");
                if(dmasize==8)dmasize=DMA_SIZE_8;
                else if(dmasize==16)dmasize=DMA_SIZE_16;
                else if(dmasize==32)dmasize=DMA_SIZE_32;
        }
        dma_channel_config c = dma_channel_get_default_config(dma_rx_chan);
        channel_config_set_read_increment(&c, false);
        channel_config_set_transfer_data_size(&c, dmasize);
        if(dma_rx_pio==2)channel_config_set_dreq(&c, 20+sm);
        else channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
        channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
        if(argc==13){
                int size=getinteger(argv[12]);
                if(!(size==1 || size==2 || size==4 || size==8 || size==16 || size==32 || size==64 || size==128 || size==256 || size==512 || size==1024 || size== 2048 || size==4096 || size==8192 || size==16384 || size==32768))error("Not power of 2");
                if(size!=1){
                        int i=0,j=size;
                        if(((uint32_t)a1int & (j-1)) && nbr==0)error("Data alignment error");
                        while(j>>=1)i++;
                        i+=dmasize;
                        if((1<<i)>(toarraysize*8))error("Array size");
                         if(nbr==0){
                                nbr=size;
                                dma_channel_config c2 = dma_channel_get_default_config(dma_rx_chan2); //Get configurations for control channel
                                channel_config_set_transfer_data_size(&c2, DMA_SIZE_32); //Set control channel data transfer size to 32 bits
                                channel_config_set_read_increment(&c2, false); //Set control channel read increment to false
                                channel_config_set_write_increment(&c2, false); //Set control channel write increment to false
                                channel_config_set_dreq(&c2, 0x3F);
                                dma_channel_configure(dma_rx_chan2,
                                        &c2,
                                        &dma_hw->ch[dma_rx_chan].al2_write_addr_trig,
                                        &a1int,
                                        1,
                                        false); //Configure control channel  
                        }
                       if(s_nbr!=0)channel_config_set_ring(&c,true,i);
                        channel_config_set_write_increment(&c, true);
                } else channel_config_set_write_increment(&c, false);
        } else {
                if((nbr<<dmasize)>(toarraysize*8))error("Array size");
                channel_config_set_write_increment(&c, true);
        } 
        if(s_nbr==0) channel_config_set_chain_to(&c, dma_rx_chan2); //When this channel completes, it will trigger the channel indicated by chain_to
        dma_channel_configure(dma_rx_chan,
                &c,
                a1int,        // Destination pointer
                &pio->rxf[sm],      // Source pointer
                nbr, // Number of transfers
                (s_nbr==0 ? false :true)                // Start immediately
        );
        if(s_nbr==0) dma_start_channel_mask(1u << dma_rx_chan2);
        pio_sm_restart(pio, sm);
        pio_sm_set_enabled(pio, sm, true);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"DMA TX");
    if(tp){
        getargs(&tp, 13, (unsigned char *)",");
        if(checkstring(argv[0],(unsigned char *)"OFF")){
                dma_hw->abort = ((1u << dma_tx_chan2) | (1u << dma_tx_chan));
                if(dma_channel_is_busy(dma_tx_chan))dma_channel_abort(dma_tx_chan);
                if(dma_channel_is_busy(dma_tx_chan2))dma_channel_abort(dma_tx_chan2);
                return;
        }
        if(DMAinterruptTX || dma_channel_is_busy(dma_tx_chan) || dma_channel_is_busy(dma_tx_chan2)){
                dma_hw->abort = ((1u << dma_tx_chan2) | (1u << dma_tx_chan));
                if(dma_channel_is_busy(dma_tx_chan))dma_channel_abort(dma_tx_chan);
                if(dma_channel_is_busy(dma_tx_chan2))dma_channel_abort(dma_tx_chan2);
        }
        if(argc<7)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        int sm=getint(argv[2],0,3);
        dma_tx_pio=pior;
        dma_tx_sm=sm;
        uint32_t nbr=getint(argv[4],0,0xFFFFFFFF);
        uint32_t s_nbr=nbr;
        static uint32_t *a1int=NULL;
        int64_t *aint=NULL;
        int toarraysize=parseintegerarray(argv[6],&aint,4,1,dims, true);
        a1int=(uint32_t *)aint;
        if(argc>=9 && *argv[8]){
                if(nbr==0)error("Interrupt incopmpatible with continuous running");
                DMAinterruptTX=(char *)GetIntAddress(argv[8]);
                InterruptUsed=true;
        }
        int dmasize=DMA_SIZE_32;
        if(argc>=11 && *argv[10]){
                dmasize=getinteger(argv[10]);
                if(!(dmasize==8 || dmasize==16 || dmasize==32))error("Invalid transfer size");
                if(dmasize==8)dmasize=DMA_SIZE_8;
                else if(dmasize==16)dmasize=DMA_SIZE_16;
                else if(dmasize==32)dmasize=DMA_SIZE_32;
        }
        dma_channel_config c = dma_channel_get_default_config(dma_tx_chan);
        channel_config_set_write_increment(&c, false);
        if(dma_tx_pio==2)channel_config_set_dreq(&c, 16+sm);
        else channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));

        channel_config_set_transfer_data_size(&c, dmasize);
        if(argc==13){
                int size=getinteger(argv[12]);
                if(!(size==1 || size==2 || size==4 || size==8 || size==16 || size==32 || size==64 || size==128 || size==256 || size==512 || size==1024 || size== 2048 || size==4096 || size==8192 || size==16384 || size==32768))error("Not power of 2");
                if(size!=1){
                        int i=0,j=size;
                        if(((uint32_t)a1int & (j-1)) && nbr==0)error("Data alignment error");
                        while(j>>=1)i++;
                        i+=dmasize;
                        if((1<<i)>(toarraysize*8))error("Array size");
                        if(nbr==0){
                                nbr=size;
                                dma_channel_config c2 = dma_channel_get_default_config(dma_tx_chan2); //Get configurations for control channel
                                channel_config_set_transfer_data_size(&c2, DMA_SIZE_32); //Set control channel data transfer size to 32 bits
                                channel_config_set_read_increment(&c2, false); //Set control channel read increment to false
                                channel_config_set_write_increment(&c2, false); //Set control channel write increment to false
                                channel_config_set_dreq(&c2, 0x3F);
//                                channel_config_set_chain_to(&c2, dma_tx_chan); 
                                dma_channel_configure(dma_tx_chan2,
                                        &c2,
                                        &dma_hw->ch[dma_tx_chan].al3_read_addr_trig,
                                        &a1int,
                                        1,
                                        false); //Configure control channel  
                        }
                        channel_config_set_read_increment(&c, true);
                        if(s_nbr!=0)channel_config_set_ring(&c,false,i);
                } else channel_config_set_read_increment(&c, false);
        } else {
                if((nbr<<dmasize)>(toarraysize*8))error("Array size");
                channel_config_set_read_increment(&c, true);
        } 
        if(s_nbr==0) channel_config_set_chain_to(&c, dma_tx_chan2); //When this channel completes, it will trigger the channel indicated by chain_to

                
        dma_channel_configure(dma_tx_chan,
                &c,
                &pio->txf[sm],      // Destination pointer
                a1int,        // Source pointer
                nbr, // Number of transfers
                (s_nbr==0 ? false :true)                // Start immediately
        );
        if(s_nbr==0) dma_start_channel_mask(1u << dma_tx_chan2);
        pio_sm_restart(pio, sm);
        pio_sm_set_enabled(pio, sm, true);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"INTERRUPT");
    if(tp){
        getargs(&tp, 7, (unsigned char *)",");
        if((argc & 0x01) == 0) error("Syntax");
        if(argc<5)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
#endif
        int sm=getint(argv[2],0,3);
        if(*argv[4]){
                if(checkstring(argv[4],(unsigned char *)"0"))pioRXinterrupts[sm][pior]=NULL;
                else pioRXinterrupts[sm][pior]=(char *)GetIntAddress(argv[4]);
        }
        if(argc==7){
                if(checkstring(argv[6],(unsigned char *)"0"))pioTXinterrupts[sm][pior]=NULL;
                else pioTXinterrupts[sm][pior]=(char *)GetIntAddress(argv[6]);
        }
        piointerrupt=0;
        for(int i=0;i<4;i++){
                for(int j=0 ;j<PIOMAX;j++){
                        if(pioRXinterrupts[i][j] || pioTXinterrupts[i][j]){
                                piointerrupt=1;
                                InterruptUsed=1;
                        }
                }
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"READ");
    if(tp){
//        unsigned char *p;
        unsigned int nbr;
        long long int *dd;
        getargs(&tp, (MAX_ARG_COUNT * 2) - 1, (unsigned char *)",");
        if((argc & 0x01) == 0) error("Syntax");
        if(argc<5)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        int sm=getint(argv[2],0,3);
        nbr = getinteger(argv[4]);
        dd = findvar(argv[6], V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
        if(((g_vartbl[g_VarIndex].type & T_INT) && g_vartbl[g_VarIndex].dims[0] > 0 && g_vartbl[g_VarIndex].dims[1] == 0))
        {		// integer array
            if( (((long long int *)dd - g_vartbl[g_VarIndex].val.ia) + nbr) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase) )
                error("Insufficient array size");
        }  else  if ((g_vartbl[g_VarIndex].type & T_INT) && g_vartbl[g_VarIndex].dims[0] == 0 && nbr==1){
            // single variable
        }  else error("Invalid variable");
        
        while(nbr--) {
       	    *dd = pio_sm_get(pio, sm);
            if(pio->fdebug & (1<<(sm + 16)))*dd=-1;
       	    if(nbr)dd++;
        }
        return;
    }
#ifdef rp2350
    tp = checkstring(cmdline, (unsigned char *)"WRITEFIFO");
    if(tp){
        getargs(&tp,7,(unsigned char *)",");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
        int sm=getint(argv[2],0,3);
        int fifo=getint(argv[4],0,3);
        pio->rxf_putget[sm][fifo]=getint(argv[6],0,0xFFFFFFFF); // jmp pin
        targ=T_INT;
        return;
    }
#endif
    tp = checkstring(cmdline, (unsigned char *)"PROGRAM LINE");
    if(tp){
        getargs(&tp,5,(unsigned char *)",");
        if(argc!=5)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        int slot=getint(argv[2],0,31);
        int instruction=getint(argv[4],0,0xFFFF);
        pio->instr_mem[slot]=instruction;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"ASSEMBLE");
    if(tp){
        static int wrap_target_set=0, wrap_set=0;
        getargs(&tp,3,(unsigned char *)",");
        if(!argc)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
        if(PIO2==false && pior==2)error("PIO 2 not available");
        pioinuse=pior;
#ifdef rp2350
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        unsigned int ins=0;
        char *ss;
        if(argc==3){
                ss=(char *)getCstring(argv[2]);
                char *comment=strchr(ss,';');
                if(comment)*comment=0;
                skipspace(ss);
                if(*ss==0)return;
        } else return;
        if(!strncasecmp(ss,".PROGRAM ",9)){
                if(dirOK!=2)error("Program already started");
                sidepins=0,sideopt=0,sidepinsdir=0;
                delaypossible=5;
                checksideanddelay=0;
                dirOK=1;
                PIOlinenumber=0;
                instructions = (int *)GetMemory(sizeof(int)*32);
                for(int i=0;i<32;i++)instructions[i]=-1;
                labelsfound = GetMemory(32 * MAXLABEL);
                labelsneeded = GetMemory(32 * MAXLABEL);
                PIOstart=0;
                p_wrap=31;
                p_wrap_target=0;
                wrap_target_set=0;
                wrap_set=0;
                return;
        }
        if(dirOK!=2){
                char *p;
                if(strchr(ss,':') && !fstrstr(ss,"::")){
                        p=strchr(ss,':');
                        skipspace(ss);
                        *p=0;
                        if(((uint32_t)p-(uint32_t)ss) > (MAXLABEL-1))error("Label too long");
                        for(int j=PIOstart;j<PIOlinenumber;j++){
                                if(strcasecmp(ss,&labelsfound[j*MAXLABEL])==0) {
                                        error("Duplicate label");
                                }
                        }
                        strcpy(&labelsfound[MAXLABEL*PIOlinenumber],ss);
                        return;
                } else if(*ss!='.'){
                        if(PIOlinenumber>31)error("Program too large");
                        if(!strncasecmp(ss,"JMP ",4)){
                                int dup=0;checksideanddelay=1;
                                ss+=3;
                                dirOK=0;
                                skipspace(ss);
                                if(strncasecmp(ss,"!X",2)==0 && (ss[2]==' ' || ss[2]==',')) {
                                        ins|=0x20;
                                        dup=1;
                                        ss+=2;
                                } 
                                if(strncasecmp(ss,"X--",3)==0 && (ss[3]==' ' || ss[3]==',')) {
                                        if(dup)error("Syntax");
                                        ins|=0x40;
                                        dup=1;
                                        ss+=3;
                                }
                                if(strncasecmp(ss,"!Y",2)==0 && (ss[2]==' ' || ss[2]==',')) {
                                        if(dup)error("Syntax");
                                        ins|=0x60;
                                        dup=1;
                                        ss+=2;
                                }
                                if(strncasecmp(ss,"Y--",3)==0 && (ss[3]==' ' || ss[3]==',')) {
                                        if(dup)error("Syntax");
                                        ins|=0x80;
                                        dup=1;
                                        ss+=3;
                                }
                                if(strncasecmp(ss,"X!=Y",4)==0 && (ss[4]==' ' || ss[4]==',')) {
                                        if(dup)error("Syntax");
                                        ins|=0xA0;
                                        dup=1;
                                        ss+=4;
                                }
                                if(strncasecmp(ss,"PIN",3)==0 && (ss[3]==' ' || ss[3]==',')) {
                                        if(dup)error("Syntax");
                                        ins|=0xC0;
                                        dup=1;
                                        ss+=3;
                                }
                                if(strncasecmp(ss,"!OSRE",5)==0 && (ss[5]==' ' || ss[5]==',')) {
                                        if(dup)error("Syntax");
                                        ins|=0xC0;
                                        dup=1;
                                        ss+=5;
                                }
                                char save=0;
                                skipspace(ss);
                                if(*ss==','){
                                        ss++;
                                        skipspace(ss);
                                }
                                if((*ss>='0' && *ss<='9') || *ss=='&' ){
                                        char *ppp=ss;
                                        if(*ss=='&'){
                                                if(!(toupper(ss[1])=='B' || toupper(ss[1])=='H' || toupper(ss[1])=='O')) error("Syntax");
                                                ppp+=2;
                                        } 
                                        while(*ppp>='0' && *ppp<='9' && *ppp){ppp++;}
                                        if(*ppp){
                                                save=*ppp;
                                                *ppp=',';
                                        }

                                        ins|=getint((unsigned char *)ss,0,31);
                                        ins|=0x10000;
                                        if(*ppp==',')*ppp=save;
                                } else {
                                        char *ppp=ss;
                                        if(!isnamestart(*ppp))error("Syntax");
                                        while(isnamechar(*ppp)){ppp++;}
                                        if(*ppp){
                                                save=*ppp;
                                                *ppp=0;
                                        }
                                        strcpy(&labelsneeded[PIOlinenumber*MAXLABEL],ss);
                                        *ppp=save;
                                }
                        } else if(!strncasecmp(ss,"WAIT ",5)){
                                dirOK=0;
                                ss+=4;
                                bool PINCTRL_JMP_PIN=false;
                                ins=0x2000;checksideanddelay=1;
                                skipspace(ss);
                                if(!(*ss=='1' || *ss=='0'))error("Syntax");
                                if(*ss=='1')ins |=0x80;
                                ss++;
                                skipspace(ss);
                                if(*ss==','){
                                        ss++;
                                        skipspace(ss);
                                }
                                int rel=2;
                                if(strncasecmp(ss,"GPIO",4)==0 && (ss[4]==' ' || ss[4]==',')){
                                        ss+=4;
                                } else if(strncasecmp(ss,"PIN",3)==0 && (ss[3]==' ' || ss[3]==',')){
                                        ss+=3;
                                        ins |=0b0100000;
                                } else if(strncasecmp(ss,"IRQ",3)==0 && (ss[3]==' ' || ss[3]==',')){
                                        char *pp;
                                        ss+=3;
                                        rel=0;
                                        ins |=0b1000000;
                                        if((pp=fstrstr(ss," rel")) && (pp[4]==0 || pp[4]==' ' || pp[4]==';'))rel=1;
#ifdef rp2350
                                } else if(strncasecmp(ss,"JMPPIN",6)==0 && (ss[6]==' ' || ss[6]==',')){
                                        ss+=6;
                                        ins |=0b1100000;
                                        PINCTRL_JMP_PIN=true;
#endif
                                } else error("syntax");
                                skipspace(ss);
                                if(*ss==','){
                                        ss++;
                                        skipspace(ss);
                                }
                                char save=0;
                                char *ppp=ss;
                                if((*ss>='0' && *ss<='9') || *ss=='&' ){
                                        char *ppp=ss;
                                        if(*ss=='&'){
                                                if(!(toupper(ss[1])=='B' || toupper(ss[1])=='H' || toupper(ss[1])=='O')) error("Syntax");
                                                ppp+=2;
                                        } 
                                        while(*ppp>='0' && *ppp<='9' && *ppp){ppp++;}
                                        if(*ppp){
                                                save=*ppp;
                                                *ppp=',';
                                        }
                                } else error("Syntax");
                                int bits=getint((unsigned char *)ss,0,rel==2? 31 : PINCTRL_JMP_PIN ? 3: 7);
                                if(*ppp==',')*ppp=save;
                                if(rel==1) bits |=0x10;
                                ins |=bits;
                        } else if(!strncasecmp(ss,"IN ",3)){
                                dirOK=0;
                                ss+=3;
                                ins=0x4000;checksideanddelay=1;
                                skipspace(ss);
                                if(strncasecmp(ss,"PINS",4)==0 && (ss[4]==' ' || ss[4]==',') ){
                                        ss+=4;
                                } else if(strncasecmp(ss,"X",1)==0 && (ss[1]==' ' || ss[1]==',') ){
                                        ss++;
                                        ins|=0b00100000;
                                } else if(strncasecmp(ss,"Y",1)==0 && (ss[1]==' ' || ss[1]==',') ){
                                        ss++;
                                        ins|=0b01000000;
                                } else if(strncasecmp(ss,"NULL",4)==0 && (ss[4]==' ' || ss[4]==',') ){
                                        ss+=4;
                                        ins|=0b01100000;
                                } else if(strncasecmp(ss,"ISR",3)==0 && (ss[3]==' ' || ss[3]==',') ){
                                        ss+=3;
                                        ins|=0b11000000;
                                } else if(strncasecmp(ss,"OSR",3)==0 && (ss[3]==' ' || ss[3]==',') ){
                                        ss+=3;
                                        ins|=0b11100000;
                                } else error("Syntax");
                                skipspace(ss);
                                if(*ss!=',')error("Syntax");
                                ss++;
                                char save=0;
                                skipspace(ss);
                                char *ppp=ss;
                                if((*ss>='0' && *ss<='9') || *ss=='&' ){
                                        char *ppp=ss;
                                        if(*ss=='&'){
                                                if(!(toupper(ss[1])=='B' || toupper(ss[1])=='H' || toupper(ss[1])=='O')) error("Syntax");
                                                ppp+=2;
                                        } 
                                        while(*ppp>='0' && *ppp<='9' && *ppp){ppp++;}
                                        if(*ppp){
                                                save=*ppp;
                                                *ppp=',';
                                        }
                                } else error("Syntax");
                                int bits=getint((unsigned char *)ss,1,32);
                                if(bits==32)bits=0;
                                ins|=bits;
                                if(*ppp==',')*ppp=save;
                        } else if(!strncasecmp(ss,"OUT ",4)){
                                dirOK=0;
                                ss+=3;
                                ins=0x6000;checksideanddelay=1;
                                skipspace(ss);
                                if(strncasecmp(ss,"PINS",4)==0 && (ss[4]==' ' || ss[4]==',') ){
                                        ss+=4;
                                } else if(strncasecmp(ss,"X",1)==0 && (ss[1]==' ' || ss[1]==',') ){
                                        ss++;
                                        ins|=0b00100000;
                                } else if(strncasecmp(ss,"Y",1)==0 && (ss[1]==' ' || ss[1]==',') ){
                                        ss++;
                                        ins|=0b01000000;
                                } else if(strncasecmp(ss,"NULL",4)==0 && (ss[4]==' ' || ss[4]==',') ){
                                        ss+=4;
                                        ins|=0b01100000;
                                } else if(strncasecmp(ss,"PINDIRS",7)==0 && (ss[7]==' ' || ss[7]==',') ){
                                        ss+=7;
                                        ins|=0b10000000;
                                } else if(strncasecmp(ss,"PC",2)==0 && (ss[2]==' ' || ss[2]==',') ){
                                        ss+=2;
                                        ins|=0b10100000;
                                } else if(strncasecmp(ss,"ISR",3)==0 && (ss[3]==' ' || ss[3]==',') ){
                                        ss+=3;
                                        ins|=0b11000000;
                                } else if(strncasecmp(ss,"EXEC",4)==0 && (ss[4]==' ' || ss[4]==',') ){
                                        ss+=4;
                                        ins|=0b11100000;
                                } else error("Syntax");
                                skipspace(ss);
                                if(*ss!=',')error("Syntax");
                                ss++;
                                char save=0;
                                skipspace(ss);
                                char *ppp=ss;
                                if((*ss>='0' && *ss<='9') || *ss=='&' ){
                                        char *ppp=ss;
                                        if(*ss=='&'){
                                                if(!(toupper(ss[1])=='B' || toupper(ss[1])=='H' || toupper(ss[1])=='O')) error("Syntax");
                                                ppp+=2;
                                        } 
                                        while(*ppp>='0' && *ppp<='9' && *ppp){ppp++;}
                                        if(*ppp){
                                                save=*ppp;
                                                *ppp=',';
                                        }
                                } else error("Syntax");
                                int bits=getint((unsigned char *)ss,1,32);
                                if(bits==32)bits=0;
                                ins|=bits;
                                if(*ppp==',')*ppp=save;
                        } else if(!strncasecmp(ss,"PUSH",4) && (ss[4]==0 || ss[4]==' ')){
                                dirOK=0;
                                ss+=4;
                                ins=0x8000;checksideanddelay=1;
                                ins |= checkblock(ss);
                        } else if(!strncasecmp(ss,"PULL ",4) && (ss[4]==0 || ss[4]==' ')){
                                dirOK=0;
                                ss+=4;
                                ins=0x8080;checksideanddelay=1;
                                ins |= checkblock(ss);
                        } else if(!strncasecmp(ss,"MOV ",4)){
                                dirOK=0;
                                ss+=3;
                                ins=0xA000;checksideanddelay=1;
                                skipspace(ss);
                                if(strncasecmp(ss,"PINS",4)==0 && (ss[4]==' ' || ss[4]==',') ){
                                        ss+=4;
                                } else if(strncasecmp(ss,"X",1)==0 && (ss[1]==' ' || ss[1]==',') ){
                                        ss++;
                                        ins|=0b00100000;
                                } else if(strncasecmp(ss,"Y",1)==0 && (ss[1]==' ' || ss[1]==',') ){
                                        ss++;
                                        ins|=0b01000000;
#ifdef rp2350
                                } else if(strncasecmp(ss,"PINDIRS",7)==0 && (ss[7]==' ' || ss[7]==',') ){
                                        ss+=7;
                                        ins|=0b01100000;
#endif
                                } else if(strncasecmp(ss,"EXEC",4)==0 && (ss[4]==' ' || ss[4]==',') ){
                                        ss+=4;
                                        ins|=0b10000000;
                                } else if(strncasecmp(ss,"PC",2)==0 && (ss[2]==' ' || ss[2]==',') ){
                                        ss+=2;
                                        ins|=0b10100000;
                                } else if(strncasecmp(ss,"ISR",3)==0 && (ss[3]==' ' || ss[3]==',') ){
                                        ss+=3;
                                        ins|=0b11000000;
                                } else if(strncasecmp(ss,"OSR",3)==0 && (ss[3]==' ' || ss[3]==',') ){
                                        ss+=3;
                                        ins|=0b11100000;
#ifdef rp2350
                                } else if(strncasecmp(ss,"RXFIFO[",7)==0){ //this is really a push instruction
                                        ss+=7;
                                        ins=0x8010;
                                        if(strncasecmp(ss,"0]",2)==0 && (ss[2]==' ' || ss[2]==',')){
                                                ins|=0b1000;
                                        } else if(strncasecmp(ss,"1]",2)==0 && (ss[2]==' ' || ss[2]==',')){
                                                ins|=0b1001;
                                        } else if(strncasecmp(ss,"2]",2)==0 && (ss[2]==' ' || ss[2]==',')){
                                                ins|=0b1010;
                                        } else if(strncasecmp(ss,"3]",2)==0 && (ss[2]==' ' || ss[2]==',')){
                                                ins|=0b1011;
                                        } else if(!(strncasecmp(ss,"Y]",2)==0 && (ss[2]==' ' || ss[2]==',')))error("Syntax");
                                        ss+=2;
#endif
                                } else error("Syntax");
                                skipspace(ss);
                                if(*ss!=',')error("Syntax");
                                ss++;
                                skipspace(ss);
                                if(*ss=='~' || *ss=='!'){
                                        ins |=8;
                                        ss++;
                                }
                                if(*ss==':' && ss[1]==':'){
                                        ins |=0x10;
                                        ss+=2;
                                }
                                skipspace(ss);
                                if(strncasecmp(ss,"PINS",4)==0 && (ss[4]==0 || ss[4]==' ' || ss[4]==',') ){
                                        ss+=4;
                                        if(!(ins&0x2000))error("Syntax");
                                } else if(strncasecmp(ss,"X",1)==0 && (ss[1]==0 || ss[1]==' ' || ss[1]==',') ){
                                        ss++;
                                        ins|=0b001;
                                        if(!(ins&0x2000))error("Syntax");
                                } else if(strncasecmp(ss,"Y",1)==0 && (ss[1]==0 || ss[1]==' ' || ss[1]==',') ){
                                        ss++;
                                        ins|=0b010;
                                        if(!(ins&0x2000))error("Syntax");
                                } else if(strncasecmp(ss,"NULL",4)==0 && (ss[4]==0 || ss[4]==' ' || ss[4]==',') ){
                                        ss+=4;
                                        ins|=0b011;
                                        if(!(ins&0x2000))error("Syntax");
                                } else if(strncasecmp(ss,"STATUS",6)==0 && (ss[6]==0 || ss[6]==' ' || ss[6]==',') ){
                                        ss+=6;
                                        ins|=0b101;
                                        if(!(ins&0x2000))error("Syntax");
                                } else if(strncasecmp(ss,"ISR",3)==0 && (ss[3]==0 || ss[3]==' ' || ss[3]==',') ){
                                        ss+=3;
                                        if(ins&0x2000)ins|=0b110;
                                } else if(strncasecmp(ss,"OSR",3)==0 && (ss[3]==0 || ss[3]==' ' || ss[3]==',') ){
                                        ss+=3;
                                        ins|=0b111;
                                        if(!(ins&0x2000))error("Syntax");
#ifdef rp2350
                                } else if(strncasecmp(ss,"RXFIFO",6)==0 && (ss[6]=='[') && ins==(0xA000 | 0b11100000)){ //this is really a pull instruction
                                        ss+=7;
                                        ins=0x8090;
                                        if(strncasecmp(ss,"0]",2)==0 && (ss[2]==0 || ss[2]==' ' || ss[2]==',')){
                                                ins|=0b1000;
                                        } else if(strncasecmp(ss,"1]",2)==0 && (ss[2]==0 || ss[2]==' ' || ss[2]==',')){
                                                ins|=0b1001;
                                        } else if(strncasecmp(ss,"2]",2)==0 && (ss[2]==0 || ss[2]==' ' || ss[2]==',')){
                                                ins|=0b1010;
                                        } else if(strncasecmp(ss,"3]",2)==0 && (ss[2]==0 || ss[2]==' ' || ss[2]==',')){
                                                ins|=0b1011;
                                        } else if(!(strncasecmp(ss,"Y]",2)==0 && (ss[2]==0 || ss[2]==' ' || ss[2]==',')))error("Syntax");
                                        ss+=2;
#endif
                                } else error("Syntax");

                        } else if(!strncasecmp(ss,"NOP",3) && (ss[3]==0 || ss[3]==' ')){
                                dirOK=0;
                                ss+=3;
                                ins=0xA000;
                                ins |=0b01000010;checksideanddelay=1;
                        } else if(!strncasecmp(ss,"IRQ SET ",8)){
                                dirOK=0;
                                ss+=7;
                                ins=0xC000;checksideanddelay=1;
                                ins |= getirqnum(ss);
                        } else if(!strncasecmp(ss,"IRQ WAIT ",9)){
                                dirOK=0;
                                ss+=8;
                                ins=0xC020;checksideanddelay=1;
                                ins |= getirqnum(ss);
                        } else if(!strncasecmp(ss,"IRQ CLEAR ",10)){
                                dirOK=0;
                                ss+=9;
                                ins=0xC040;checksideanddelay=1;
                                ins |= getirqnum(ss);
                        } else if(!strncasecmp(ss,"IRQ NOWAIT ",11)){
                                dirOK=0;
                                ss+=10;
                                ins=0xC000;checksideanddelay=1;
                                ins |= getirqnum(ss);
                        } else if(!strncasecmp(ss,"IRQ ",4)){
                                dirOK=0;
                                ss+=3;
                                ins=0xC000;checksideanddelay=1;
                                ins |= getirqnum(ss);
                        } else if(!strncasecmp(ss,"SET ",4)){
                                dirOK=0;
                                ss+=3;
                                ins=0xE000;checksideanddelay=1;
                                skipspace(ss);
                                if(strncasecmp(ss,"PINS",4)==0 && (ss[4]==' ' || ss[4]==',') ){
                                        ss+=4;
                                } else if(strncasecmp(ss,"X",1)==0 && (ss[1]==' ' || ss[1]==',') ){
                                        ss++;
                                        ins|=0b100000;
                                } else if(strncasecmp(ss,"Y",1)==0 && (ss[1]==' ' || ss[1]==',') ){
                                        ss++;
                                        ins|=0b1000000;
                                } else if(strncasecmp(ss,"PINDIRS",7)==0 && (ss[7]==' ' || ss[7]==',') ){
                                        ss+=7;
                                        ins|=0b10000000;
                                } else error("Syntax");
                                skipspace(ss);
                                if(*ss!=',')error("Syntax");
                                ss++;
                                char save=0;
                                skipspace(ss);
                                char *ppp=ss;
                                if((*ss>='0' && *ss<='9') || *ss=='&' ){
                                        char *ppp=ss;
                                        if(*ss=='&'){
                                                if(!(toupper(ss[1])=='B' || toupper(ss[1])=='H' || toupper(ss[1])=='O')) error("Syntax");
                                                ppp+=2;
                                        } 
                                        while(*ppp>='0' && *ppp<='9' && *ppp){ppp++;}
                                        if(*ppp){
                                                save=*ppp;
                                                *ppp=',';
                                        }
                                } else error("Syntax");
                                ins|=getint((unsigned char *)ss,0,31);
                                if(*ppp==',')*ppp=save;
                        } else error("PIO instruction not found");
                        if(checksideanddelay==1)ins|=calcsideanddelay(ss, sidepins, delaypossible);
                        instructions[PIOlinenumber]=ins;
                        PIOlinenumber++;
                        return;
                } else {
                        if(!strncasecmp(ss,".LINE ",5)){
                                if(!dirOK)error("Directive must appear before instructions");
                                ss+=5;
                                skipspace(ss);
                                if(!strncasecmp(ss,"NEXT",4) && (ss[4]==0 || ss[4]==' '))PIOlinenumber=nextline[pioinuse];
                                else PIOlinenumber=getint((unsigned char *)ss,0,31);
                                PIOstart=PIOlinenumber;
                                return;
                        } else if(!strncasecmp(ss,".WRAP TARGET",12)){
                                if(wrap_target_set)error("Repeat directive");
                                p_wrap_target=PIOlinenumber;
                                wrap_target_set=1;
                                return;
                                 //sort out the jmps
                        } else if(!strncasecmp(ss,".WRAP",4)){
//                                if(!wrap_target_set)error("Wrap target not set");
                                if(wrap_set)error("Repeat directive");
                                p_wrap=PIOlinenumber-1;
                                if(PIOlinenumber==-1)PIOlinenumber=31;
                                return;
                                 //sort out the jmps
                        } else if(!strncasecmp(ss,".END PROGRAM",12)){ //sort out the jmps
                                if(dirOK==2)error("Program not started");
                                int totallines=0;
                                dirOK=2;
                                for(int i=PIOstart; i<32;i++){
                                        if(instructions[i]!=-1){
                                                totallines++;
                                        }
                                }
                                for(int i=PIOstart; i<PIOstart+totallines;i++){
                                        if(!(instructions[i] & 0x1E000)){ // jmp instructions needs resolving
                                                int notfound=1;
                                                for(int j=PIOstart;j<PIOstart+totallines;j++){
                                                        if(strcasecmp(&labelsneeded[i*MAXLABEL],&labelsfound[j*MAXLABEL])==0) {
                                                                instructions[i] |=j; 
                                                                notfound=0;
                                                        }
                                                }
                                                if(notfound)error("label not found at line %",i);
                                        }
                                        if(instructions[i]&0x10000)instructions[i]&=0xFFFF;
                                }
                                ss+=12;
                                skipspace(ss);
                                for(int i=PIOstart;i<PIOstart+totallines;i++){
                                        pio->instr_mem[i]=instructions[i];
                                                if(!strncasecmp(ss,"LIST",4) && (ss[4]==0 || ss[4]==' ')){
                                                char c[10]={0};
                                                PInt(i);
                                                MMPrintString(": ");
                                                IntToStr(c,instructions[i]+0x10000,16);
                                                MMPrintString(&c[1]);
                                                PRet();
                                        }
                                }
                                nextline[pioinuse]=PIOstart+totallines;
                                nextline[3]=pioinuse;
                                FreeMemorySafe((void **)&instructions);
                                FreeMemorySafe((void **)&labelsneeded);
                                FreeMemorySafe((void **)&labelsfound);
                                pioinuse=99;
                                return;
                        } else if(!strncasecmp(ss,".SIDE_SET ",10)){
                                if(!dirOK)error("Directive must appear before instructions");
                                ss+=10;
                                sidepins=*ss-'0';
                                if(sidepins<1 || sidepins>5)error("Invalid side_set pin count");
                                ss++;
                                delaypossible=5-sidepins;
                                ins=0xE000;
                                skipspace(ss);
                                if(!strncasecmp(ss,"opt",3) && (ss[3]==0 || ss[3]==' ')){
                                        sideopt=1;
                                        delaypossible=4-sidepins;
                                        if(sideopt && sidepins==5)error("Only 4 side set pins allowed with opt set");
                                        ss+=3;
                                        skipspace(ss);
                                }
                                if(!strncasecmp(ss,"pindirs",7) && (ss[7]==0 || ss[7]==' '))sidepinsdir=1;
                                return;
                        } else error("PIO directive not found");
                }
        } else error(".program must be first statement");
    }
    tp = checkstring(cmdline, (unsigned char *)"CLEAR");
    if(tp){
        getargs(&tp,1,(unsigned char *)",");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        for(int sm=0;sm<4;sm++){
            hw_clear_bits(&pio->ctrl, 1 << (PIO_CTRL_SM_ENABLE_LSB + sm));
            pio->sm[sm].pinctrl=(5<<26);
            pio->sm[sm].execctrl=(0x1f<<12);
            pio->sm[sm].shiftctrl=(3<<18);
            pio_sm_clear_fifos(pio,sm);
        }
        pio_clear_instruction_memory(pio);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"MAKE RING BUFFER");
    if(tp){
        getargs(&tp,3,(unsigned char *)",");
        if(argc<3)error("Syntax");
        int size=getinteger(argv[2]);
        if(!(size==256 || size==512 || size==1024 || size== 2048 || size==4096 || size==8192 || size==16384 || size==32768))error("Not power of 2");
        findvar(argv[0], V_FIND | V_NOFIND_ERR);
        if ((g_vartbl[g_VarIndex].type & T_INT) && g_vartbl[g_VarIndex].dims[0] == 0 && g_vartbl[g_VarIndex].level==0){
                g_vartbl[g_VarIndex].val.s =(unsigned char *)GetAlignedMemory(size);
                g_vartbl[g_VarIndex].size=255;
                g_vartbl[g_VarIndex].dims[0] = size/8-1+g_OptionBase;
        }  else error("Invalid variable");
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"PROGRAM");
    if(tp){
        struct pio_program program;
        getargs(&tp,3,(unsigned char *)",");
        if(argc!=3)error("Syntax");
//        void *prt1;
        program.length=32;
        program.origin=0;
        int64_t *a1int=NULL;
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        int toarraysize=parseintegerarray(argv[2],&a1int,2,1,dims, true);
        if(toarraysize!=8)error("Array size");
        program.instructions = (const uint16_t *)a1int;
        for(int sm=0;sm<4;sm++)hw_clear_bits(&pio->ctrl, 1 << (PIO_CTRL_SM_ENABLE_LSB + sm));
        pio_clear_instruction_memory(pio);
        pio_add_program(pio, &program);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"START");
    if(tp){
        getargs(&tp,3,(unsigned char *)",");
        if(argc!=3)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        int sm=getint(argv[2],0,3);
        pio_sm_clear_fifos(pio,sm);
        pio_sm_restart(pio, sm);
        pio_sm_set_enabled(pio, sm, true);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"STOP");
    if(tp){
        getargs(&tp,3,(unsigned char *)",");
        if(argc!=3)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        int sm=getint(argv[2],0,3);
        pio_sm_set_enabled(pio, sm, false);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"INIT MACHINE");
    if(tp){
        int start=0;
        bool setout=false,sideout=false,outout=false;
        getargs(&tp,19,(unsigned char *)",");
        if(argc<5)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
#endif
        int sm=getint(argv[2],0,3);
        float clock=getnumber(argv[4]);
        if(clock<CLKMIN || clock> CLKMAX)error("Clock must be in range % to %",CLKMIN,CLKMAX);
        int pinctrl=0, execctrl=0,shiftctrl=0;
        if(argc>5 && *argv[6])pinctrl = getint(argv[6],0,0xFFFFFFFF);
        if(argc>7 && *argv[8])execctrl= getint(argv[8],0,0xFFFFFFFF);
        if(argc>9 && *argv[10])shiftctrl= getint(argv[10],0,0xFFFFFFFF);
        if(argc>11 && *argv[12]) start=getint(argv[12],0,31);
        if(argc>13 && *argv[14]) sideout=getint(argv[14],0,1);
        if(argc>15 && *argv[16]) setout=getint(argv[16],0,1);
        if(argc>171 && *argv[18]) outout=getint(argv[18],0,1);
        pio_init(pior, sm, pinctrl, execctrl, shiftctrl, start, clock, sideout, setout, outout);
        return;
    }
    #ifdef rp2350
    tp = checkstring(cmdline, (unsigned char *)"SET BASE");
    if(tp){
        getargs(&tp,3,(unsigned char *)",");
        if(argc<3)error("Syntax");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        int i=getinteger(argv[2]);
        if(!(i==0 || i==16))error("Valid base values are 0 and 16");
        if(rp2350a && i==16)error("Invalid for 60-pin chip");
        pio_set_gpio_base(pio,i);
        return;
    }
#endif
    tp = checkstring(cmdline, (unsigned char *)"CONFIGURE");
/*PIO Configure pio, sm, clock, startaddress,
sidesetbase, sidesetno, sidesetout, setbase, setno, setout, outbase, outno, outout, inbase,
jmppin, wraptarget, wrap, sideenable,sidepindir,
pushthreshold, pullthreshold, autopush, autopull, inshiftdir, outshiftdir, joinrcfifo, jointxfifo, joinrxfifoget, joinrxfifoput
*/
    if(tp){
        int startaddress=0, base=0;
        int sidesetbase=0, sidesetno=0, sidesetout=0, setbase=0, setno=0, setout=0, outbase=0, outno=0, outout=0, inbase=0;
        int jmppin=-1, wraptarget=0, wrap=31, sideenable=0, sidepindir=0;
        int pushthreshold=0, pullthreshold=0, autopush=0, autopull=0, inshiftdir=1, outshiftdir=1, joinrxfifo=0, jointxfifo=0;
#ifdef rp2350
        int joinrxfifoget=0, joinrxfifoput=0;
        getargs(&tp,57,(unsigned char *)",");
#else
        getargs(&tp,53,(unsigned char *)",");
#endif
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
        base=pio_get_gpio_base(pio);
#else
        PIO pio = (pior==0 ? pio0: pio1);
#endif
        if(base==16){
                sidesetbase=16;
                setbase=16;
                outbase=16;  
                inbase=16;
        }
        int sm=getint(argv[2],0,3);
        float clock=getnumber(argv[4]);
        if(clock<CLKMIN || clock> CLKMAX)error("Clock must be in range % to %",CLKMIN,CLKMAX);
        if(argc>5 && *argv[6]) startaddress=getint(argv[6],0,31);
        if(argc>7 && *argv[8]) sidesetbase=getGPpin(argv[8],pior, base);
        if(argc>9 && *argv[10]) sidesetno=getint(argv[10],0,5);
        if(argc>11 && *argv[12]) sidesetout=getint(argv[12],0,1);
        if(argc>13 && *argv[14]) setbase=getGPpin(argv[14],pior, base);
        if(argc>15 && *argv[16]) setno=getint(argv[16],0,5);
        if(argc>17 && *argv[18]) setout=getint(argv[18],0,1);
        if(argc>19 && *argv[20]) outbase=getGPpin(argv[20],pior, base);
        if(argc>21 && *argv[22]) outno=getint(argv[22],0,5);
        if(argc>23 && *argv[24]) outout=getint(argv[24],0,1);
        if(argc>25 && *argv[26]) inbase=getGPpin(argv[26],pior, base);
        if(argc>27 && *argv[28]) jmppin=getGPpin(argv[28],pior, base);
        if(argc>29 && *argv[30]) wraptarget=getint(argv[30],0,31);
        if(argc>31 && *argv[32]) wrap=getint(argv[32],0,31);
        if(argc>33 && *argv[34]) sideenable=getint(argv[34],0,1);
        if(sideenable+sidesetno>5)error("Max 4 sideset pins with option set");
        if(argc>35 && *argv[36]) sidepindir=getint(argv[36],0,1); // push threshold
        if(argc>37 && *argv[38]) pushthreshold=getint(argv[38],0,31); // push threshold
        if(argc>39 && *argv[40]) pullthreshold=getint(argv[40],0,31); // pull threshold
        if(argc>41 && *argv[42]) autopush=getint(argv[42],0,1); // autopush
        if(argc>43 && *argv[44]) autopull=getint(argv[44],0,1); // autopull
        if(argc>45 && *argv[46]) inshiftdir=getint(argv[46],0,1); // IN_SHIFTDIR
        if(argc>47 && *argv[48]) outshiftdir=getint(argv[48],0,1); // OUT_SHIFTDIR
        if(argc>49 && *argv[50]) joinrxfifo=getint(argv[50],0,1); // FJOIN_RX
        if(argc>51 && *argv[52]) jointxfifo=getint(argv[52],0,1); // FJOIN_TX
        if(joinrxfifo && jointxfifo)error("Attempt to join both RX and TX FIFOs");
#ifdef rp2350
        if(argc>53 && *argv[54]) joinrxfifoget=getint(argv[54],0,1); // FJOIN_RX_GET
        if(argc>55 && *argv[56]) joinrxfifoput=getint(argv[56],0,1); // FJOIN_RX_PUT
#endif
        #ifdef rp2350
        #ifdef PICOMITEWEB
                for(int i = 1; i < (NBRPINS) ; i++) {
        #else
                for(int i = 1; i < (rp2350a ? 44:NBRPINS) ; i++) {
        #endif
        #else
        for(int i = 1; i < (NBRPINS) ; i++) {
        #endif
                if(CheckPin(i, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) {    // don't reset invalid or boot reserved pins
                        gpio_set_input_enabled(PinDef[i].GPno, true);
                }
        }
        pio_set_gpio_base(pio,base);
        pio_sm_config cfg = pio_get_default_sm_config();
        sm_config_set_in_pin_base(&cfg, inbase);
	if(jointxfifo)sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);
	if(joinrxfifo)sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_RX);
        if(outno)sm_config_set_out_pins(&cfg, outbase, outno);
        if(sidesetno){
                sm_config_set_sideset_pins(&cfg,sidesetbase);
                sm_config_set_sideset(&cfg,sidesetno+sideenable,sideenable,sidepindir);
        }
        if(setno){
                sm_config_set_set_pin_base(&cfg, setbase);
                sm_config_set_set_pin_count(&cfg, setno);
        }
        if(jmppin>=0)sm_config_set_jmp_pin(&cfg, jmppin);
        sm_config_set_wrap(&cfg, wraptarget, wrap);
        sm_config_set_in_shift(&cfg,inshiftdir,autopush,pushthreshold);
        sm_config_set_out_shift(&cfg,outshiftdir, autopull,pullthreshold);
#ifdef rp2350
        if(joinrxfifoget)cfg.shiftctrl|=(1<<14);
        if(joinrxfifoput)cfg.shiftctrl|=(1<<15);
#endif
        sm_config_set_clkdiv(&cfg, (Option.CPU_Speed*1000.0f)/clock);
        pio_sm_set_config(pio, sm, &cfg);
        pio_sm_init(pio, sm, startaddress, &cfg);
        if(sidesetout && sidesetno)pio_sm_set_consecutive_pindirs(pio, sm, sidesetbase, sidesetno, true);
        if(setout && setno)pio_sm_set_consecutive_pindirs(pio, sm, setbase, setno, true);
        if(outout && outno)pio_sm_set_consecutive_pindirs(pio, sm, outbase, outno, true);
        return;
}
    error("Syntax");
}
void fun_pio(void){
    unsigned char *tp;
    tp = checkstring(ep, (unsigned char *)"PINCTRL");
    if(tp){
        int64_t myret=0;
        getargs(&tp,13,(unsigned char *)",");
        if(argc<3)error("Syntax");
        myret=(getint(argv[0],0,5)<<29); // no of side set pins
        if(argc>1 && *argv[2])myret|=(getint(argv[2],0,5)<<26); // no of set pins
        if(argc>3 && *argv[4])myret|=(getint(argv[4],0,31)<<20); // no of OUT pins
        if(argc>5 && *argv[6]){
            if(!(toupper((char)*argv[6])=='G'))error("Syntax");
            argv[6]++;
            if(!(toupper((char)*argv[6])=='P'))error("Syntax");
            argv[6]++;
            myret|=(getint(argv[6],0,31)<<15); // IN base
        }
        if(argc>7 && *argv[8]){
            if(!(toupper((char)*argv[8])=='G'))error("Syntax");
            argv[8]++;
            if(!(toupper((char)*argv[8])=='P'))error("Syntax");
            argv[8]++;
            myret|=(getint(argv[8],0,31)<<10); // SIDE SET base
        }
        if(argc>9 && *argv[10]){
            if(!(toupper((char)*argv[10])=='G'))error("Syntax");
            argv[10]++;
            if(!(toupper((char)*argv[10])=='P'))error("Syntax");
            argv[10]++;
            myret|=(getint(argv[10],0,31)<<5); // SET base
        }
        if(argc==13){
            if(!(toupper((char)*argv[12])=='G'))error("Syntax");
            argv[12]++;
            if(!(toupper((char)*argv[12])=='P'))error("Syntax");
            argv[12]++;
            myret|=getint(argv[12],0,31); //OUT base
        }
        iret=myret;
        targ=T_INT;
        return;
    }
    tp = checkstring(ep, (unsigned char *)"EXECCTRL");
    if(tp){
        int64_t myret=0;
        getargs(&tp,9,(unsigned char *)",");
        if(!(argc==5 || argc==7 || argc==9))error("Syntax");
        if(!(toupper((char)*argv[0])=='G'))error("Syntax");
        argv[0]++;
        if(!(toupper((char)*argv[0])=='P'))error("Syntax");
        argv[0]++;
        myret=(getint(argv[0],0,31)<<24); // jmp pin
        myret |= pio_sm_calc_wrap(getint(argv[2],0,31), getint(argv[4],0,31));
        if(argc>=7 && *argv[6])myret|=(getint(argv[6],0,1)<<29); //SIDE_PINDIR
        if(argc==9)myret|=(getint(argv[8],0,1)<<30); // SIDE_EN
        iret=myret;
        targ=T_INT;
        return;
    }
    tp = checkstring(ep, (unsigned char *)".WRAP TARGET");
    if(tp){
        iret=p_wrap_target;
        targ=T_INT;
        return;
    }
    tp = checkstring(ep, (unsigned char *)".WRAP");
    if(tp){
        iret=p_wrap;
        targ=T_INT;
        return;
    }
    tp = checkstring(ep, (unsigned char *)"SHIFTCTRL");
    if(tp){
#ifdef rp2350
        getargs(&tp,19,(unsigned char *)",");
#else
        getargs(&tp,15,(unsigned char *)",");
#endif
        if(argc<1)error("Syntax");
        int64_t myret=0;
        myret=(getint(argv[0],0,31)<<20); // push threshold
        myret|=(getint(argv[2],0,31)<<25); // pull threshold
        if(argc>3 && *argv[4])myret|=(getint(argv[4],0,1)<<16); // autopush
        if(argc>5 && *argv[6])myret|=(getint(argv[6],0,1)<<17); // autopull
        if(argc>7 && *argv[8])myret|=(getint(argv[8],0,1)<<18); // IN_SHIFTDIR
        if(argc>9 && *argv[10])myret|=(getint(argv[10],0,1)<<19); // OUT_SHIFTDIR
        if(argc>11 && *argv[12])myret|=(getint(argv[12],0,1)<<30); // FJOIN_RX
        if(argc>13 && *argv[14])myret|=(getint(argv[14],0,1)<<31); // FJOIN_TX
#ifdef rp2350
        if(argc>15 && *argv[16])myret|=(getint(argv[16],0,1)<<14); // FJOIN_RX_GET
        if(argc>17 && *argv[18])myret|=(getint(argv[18],0,1)<<15); // FJOIN_RX_PUT
#endif
        iret=myret;
        targ=T_INT;
        return;
    }
    tp = checkstring(ep, (unsigned char *)"FSTAT");
    if(tp){
        getargs(&tp,1,(unsigned char *)",");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        iret=pio->fstat; // jmp pin
        targ=T_INT;
        return;
    }
#ifdef rp2350
    tp = checkstring(ep, (unsigned char *)"READFIFO");
    if(tp){
        getargs(&tp,5,(unsigned char *)",");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
        int sm=getint(argv[2],0,3);
        int fifo=getint(argv[4],0,3);
        iret=pio->rxf_putget[sm][fifo]; // jmp pin
        targ=T_INT;
        return;
    }
#endif
    tp = checkstring(ep, (unsigned char *)"FDEBUG");
    if(tp){
        getargs(&tp,1,(unsigned char *)",");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        iret=pio->fdebug; // jmp pin
        targ=T_INT;
        return;
    }
    tp = checkstring(ep, (unsigned char *)"NEXT LINE");
    if(tp){
        getargs(&tp,1,(unsigned char *)",");
        int pior=nextline[3];
        if(argc)pior=getint(argv[0],0,PIOMAX-1);
        if(pior==99)error("No current PIO in use");
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
#endif
        iret=nextline[pior];
        targ=T_INT;
        return;
}
    tp = checkstring(ep, (unsigned char *)"FLEVEL");
    if(tp){
        getargs(&tp,5,(unsigned char *)",");
        int pior=getint(argv[0],0,PIOMAX-1);
        if(PIO0==false && pior==0)error("PIO 0 not available");
        if(PIO1==false && pior==1)error("PIO 1 not available");
#ifdef rp2350
        if(PIO2==false && pior==2)error("PIO 2 not available");
        PIO pio = (pior==0 ? pio0: (pior==1 ? pio1: pio2));
#else
        PIO pio = (pior==0 ?  pio0: pio1);
#endif
        if(argc==1)iret=pio->flevel; // jmp pin
        else {
                if(argc!=5)error("Syntax");
                int sm=getint(argv[2],0,3)*8;
                if(checkstring(argv[4],(unsigned char *)"TX"))iret=((pio->flevel)>>sm) & 0xf;
                else if(checkstring(argv[4],(unsigned char *)"RX"))iret=((pio->flevel)>>(sm+4)) & 0xf;
                else error("Syntax");
        }
        targ=T_INT;
        return;
    }
    tp = checkstring(ep, (unsigned char *)"DMA RX POINTER");
    if(tp){
        iret=dma_hw->ch[dma_rx_chan].write_addr;
        targ=T_INT;
        return;
    }
    tp = checkstring(ep, (unsigned char *)"DMA TX POINTER");
    if(tp){
        iret=dma_hw->ch[dma_tx_chan].read_addr;
        targ=T_INT;
        return;
    }
    error("Syntax");


}
// mode determines functionality
// bit 0 sets wheter to add a space after the text
// bit 1 sets whether to omit the missing program check
// bit 2 sets whether to add a colon after the text
// bit 3 enables the diagnostic print
void call_pio(char * instruction, uint8_t mode){
       if((instructions==NULL || pioinuse==99) && !(mode & 2))error("Missing Program statement");
       skipspace(cmdline);
       char *buff=GetTempMemory(STRINGSIZE);
       sprintf(buff,"ASSEMBLE %d, \"",pioinuse);
       strcat(buff,instruction);
       if(mode & 1)strcat(buff," ");
       strcat(buff,(char *)cmdline);
       char *p=buff;
      while(*p){ //strip comments and loose spurrious tokens
               if(*p==GetTokenValue((unsigned char *)"-"))*p=(unsigned char)'-';
               if(*p==GetTokenValue((unsigned char *)"="))*p=(unsigned char)'=';
               if(*p=='\'' || *p==';'){
                       *p=0;
                       break;
               }
               p++;
       }
       // strip trailing spaces
       p=buff+strlen(buff)-1;
       while(*p==' ')*p--=0;
       if(mode & 4)strcat(buff,":");
       strcat(buff,"\"");
       cmdline=((unsigned char *)buff);
       if(mode & 8){MMPrintString((char *)cmdline);PRet();}
       cmd_pio();
}
void cmd_program(void){
       call_pio(".program",3);
}
void cmd_wrap(void){
       call_pio(".wrap",0);
}
void cmd_wraptarget(void){
       call_pio(".wrap target",0);
}
void cmd_PIOline(void){
       call_pio(".line",1);
}
void cmd_sideset(void){
       call_pio(".side_set",1);
}
void cmd_endprogram(void){
       call_pio(".end program",strlen((char *)cmdline) ? 1 : 0);
}
void cmd_jmp(void){
       call_pio("jmp",1);
}
void cmd_wait(void){
       call_pio("wait",1);
}
void cmd_in(void){
        call_pio("in",1);
}
void cmd_out(void){
        call_pio("out",strlen((char *)cmdline) ? 1 : 0);
}
void cmd_push(void){
        call_pio("push",strlen((char *)cmdline) ? 1 : 0);
}
void cmd_pull(void){
        call_pio("pull",1);
}
void cmd_mov(void){
        call_pio("mov",1);
}
void cmd_nop(void){
        call_pio("nop",strlen((char *)cmdline) ? 1 : 0);
}
void cmd_irqset(void){
        call_pio("irq set",1);
}
void cmd_irqwait(void){
        call_pio("irq wait",1);
}
void cmd_irqclear(void){
        call_pio("irq clear",1);
}
void cmd_irqnowait(void){
        call_pio("irq mov",1);
}
void cmd_irq(void){
        call_pio("irq",1);
}
void cmd_set(void){
       call_pio("set",1);
}
void cmd_label(void){
       call_pio("",4);
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

#ifdef PICOMITEWEB
char *scan_dest=NULL;
volatile char *scan_dups=NULL; 
volatile int scan_size;
static int scan_result(void *env, const cyw43_ev_scan_result_t *result) {
    if (result) {
        Timer4=5000;
        char buff[STRINGSIZE]={0};
//        int found=0;
        if(scan_dups==NULL)return 0;
        for(int i=0;i<scan_dups[32*100];i++){
                if(strcmp((char *)result->ssid,(char *)&scan_dups[32*i])==0)return 0;
        }
        for(int i=0;i<100;i++){
                if(scan_dups[32*i]==0){
                        strcpy((char *)&scan_dups[32*i],(char *)result->ssid);
                        scan_dups[32*100]++;
                        break;
                }
        }
        sprintf(buff,"ssid: %-32s rssi: %4d chan: %3d mac: %02x:%02x:%02x:%02x:%02x:%02x sec: %u\r\n",
            result->ssid, result->rssi, result->channel,
            result->bssid[0], result->bssid[1], result->bssid[2], result->bssid[3], result->bssid[4], result->bssid[5],
            result->auth_mode);
        if(scan_dest!=NULL){
                if(strlen(((char *)&scan_dest[8]) + strlen(buff)) > scan_size){
                        FreeMemorySafe((void **)&scan_dups);
                        scan_dest=NULL;
                        error("Array too small");
                }
                if(scan_dest[8]==0)strcpy(&scan_dest[8],buff);
                else strcat(&scan_dest[8],buff);
        } else MMPrintString(buff);
    } 
    return 0;
}
/*  @endcond */
void cmd_web(void){
        unsigned char *tp;
        tp=checkstring(cmdline, (unsigned char *)"CONNECT");
        if(tp){
            if(*tp){
            	setwifi(tp);
                WebConnect();
            } else {
	            if(cyw43_wifi_link_status(&cyw43_state,CYW43_ITF_STA)<0){
	                WebConnect();
	            }
	        }
           return;   
        }
        tp=checkstring(cmdline, (unsigned char *)"SCAN");
        if(tp){
                void *ptr1 = NULL;
                if(*tp){
                        ptr1 = findvar(tp, V_FIND | V_EMPTY_OK);
                        if(g_vartbl[g_VarIndex].type & T_INT) {
                                if(g_vartbl[g_VarIndex].dims[1] != 0) error("Invalid variable");
                                if(g_vartbl[g_VarIndex].dims[0] <= 0) {		// Not an array
                                        error("Argument 1 must be integer array");
                                }
                                scan_size=(g_vartbl[g_VarIndex].dims[0]-g_OptionBase)*8;
                                scan_dest = (char *)ptr1;
                                scan_dest[8]=0;
                        } else error("Argument 1 must be integer array");

                }
                scan_dups=GetMemory(32*100+1);
                cyw43_wifi_scan_options_t scan_options = {0};
                int err = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, scan_result);
                if (err == 0) {
                    MMPrintString("\nPerforming wifi scan\r\n");
                } else {
                    char buff[STRINGSIZE]={0};
                    sprintf(buff,"Failed to start scan: %d\r\n", err);
                    MMPrintString(buff);
                }
                Timer4=500;
                while (Timer4)if(startupcomplete)ProcessWeb(0);
                if(scan_dest){
                        uint64_t *p=(uint64_t *)scan_dest;
                        *p=strlen(&scan_dest[8]);
                }
                scan_dest=NULL;
                FreeMemorySafe((void **)&scan_dups);
                return;   
        }
        if(!(WIFIconnected &&  cyw43_tcpip_link_status(&cyw43_state,CYW43_ITF_STA)==CYW43_LINK_UP))error("WIFI not connected");
        tp=checkstring(cmdline, (unsigned char *)"NTP");
        if(tp){
            cmd_ntp(tp);
            return;   
        }
        tp=checkstring(cmdline, (unsigned char *)"UDP");
        if(tp){
            cmd_udp(tp);
            return;   
        }
        if(cmd_mqtt())return;
        if(cmd_tcpclient())return;
        if(cmd_tcpserver())return;
//        if(cmd_tls())return;
        error("Syntax");
}

void fun_json(void){
    char *json_string=NULL;
    const cJSON *root = NULL;
    void *ptr1 = NULL;
    char *p;
    sret=GetTempMemory(STRINGSIZE);
    int64_t *dest=NULL;
    MMFLOAT tempd;
    int i,j,k,mode,index;
    char field[32],num[6];
    getargs(&ep, 3, (unsigned char *)",");
    char *a=GetTempMemory(STRINGSIZE);
    ptr1 = findvar(argv[0], V_FIND | V_EMPTY_OK);
    if(g_vartbl[g_VarIndex].type & T_INT) {
    if(g_vartbl[g_VarIndex].dims[1] != 0) error("Invalid variable");
    if(g_vartbl[g_VarIndex].dims[0] <= 0) {		// Not an array
        error("Argument 1 must be integer array");
    }
    dest = (long long int *)ptr1;
    json_string=(char *)&dest[1];
    } else error("Argument 1 must be integer array");
    cJSON_InitHooks(NULL);
    cJSON * parse = cJSON_Parse(json_string);
    if(parse==NULL)error("Invalid JSON data");
    root=parse;
    p=(char *)getCstring((unsigned char *)argv[2]);
    int len = strlen(p);
    memset(field,0,32);
    memset(num,0,6);
    i=0;j=0;k=0;mode=0;
    while(i<len){
        if(p[i]=='['){ //start of index
            mode=1;
            field[j]=0;
            root = cJSON_GetObjectItemCaseSensitive(root, field);
            memset(field,0,32);
            j=0;
        }
        if(p[i]==']'){
            num[k]=0;
            index=atoi(num);
            root = cJSON_GetArrayItem(root, index);
            memset(num,0,6);
            k=0;
        }
        if(p[i]=='.'){ //new field separator
            if(mode==0){
                field[j]=0;
                root = cJSON_GetObjectItemCaseSensitive(root, field);
             memset(field,0,32);
                j=0;
            } else { //step past the dot after a close bracket
                mode=0;
            }
        } else  {
            if(mode==0)field[j++]=p[i];
            else if(p[i]!='[')num[k++]=p[i];
        }
        i++;
    }
    root = cJSON_GetObjectItem(root, field);

    if (cJSON_IsObject(root)){
        cJSON_Delete(parse);
        error("Not an item");
        return;
    }
    if (cJSON_IsInvalid(root)){
        cJSON_Delete(parse);
        error("Not an item");
        return;
    }
    if (cJSON_IsNumber(root))
    {
        tempd = root->valuedouble;

        if((MMFLOAT)((int64_t)tempd)==tempd) IntToStr(a,(int64_t)tempd,10);
        else FloatToStr(a, tempd, 0, STR_AUTO_PRECISION, ' ');   // set the string value to be saved
        cJSON_Delete(parse);
        sret=(unsigned char *)a;
        sret=CtoM(sret);
        targ=T_STR;
        return;
    }
    if (cJSON_IsBool(root)){
        int64_t tempint;
        tempint=root->valueint;
        cJSON_Delete(parse);
        if(tempint)strcpy((char *)sret,"true");
        else strcpy((char *)sret,"false");
        sret=CtoM(sret);
        targ=T_STR;
        return;
    }
    if (cJSON_IsString(root)){
        strcpy(a,root->valuestring);
        cJSON_Delete(parse);
        sret=(unsigned char *)a;
        sret=CtoM(sret);
        targ=T_STR;
        return;
    }
    cJSON_Delete(parse);
    targ=T_STR;
    sret=(unsigned char *)a;
}
#endif