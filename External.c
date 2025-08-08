/***********************************************************************************************************************
PicoMite MMBasic

External.c

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

************************************************************************************************************************/#include "MMBasic_Includes.h"
/**
* @file External.c
* @author Geoff Graham, Peter Mather
* @brief Source for I/O MMBasic commands and functions
*/
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
#include "Hardware_Includes.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
//#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/pwm.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/structs/adc.h"
#include "hardware/dma.h"
#include <hardware/structs/ioqspi.h>
#include <hardware/sync.h>
#define ANA_AVERAGE     10
#define ANA_DISCARD     2

extern MMFLOAT FDiv(MMFLOAT a, MMFLOAT b);
extern MMFLOAT FMul(MMFLOAT a, MMFLOAT b);
extern MMFLOAT FSub(MMFLOAT a, MMFLOAT b);
const char *PinFunction[] = {	
        "OFF",
		"AIN",
		"DIN",
		"FIN",
		"PER",
		"CIN",
		"INTH",
		"INTL",
		"DOUT",
        "HEARTBEAT",
		"INTB",
		"UART0TX",
		"UART0RX",
		"UART1TX",
		"UART1RX",
		"I2C0SDA",
		"I2C0SCL",
		"I2C1SDA",
		"I2C1SCL",
		"SPI0RX",
		"SPI0TX",
		"SPI0SCK",
		"SPI1RX",
		"SPI1TX",
		"SPI1SCK",
        "IR",
        "INT1",
        "INT2",
        "INT3",
        "INT4",
        "PWM0A",
        "PWM0B",
        "PWM1A",
        "PWM1B",
        "PWM2A",
        "PWM2B",
        "PWM3A",
        "PWM3B",
        "PWM4A",
        "PWM4B",
        "PWM5A",
        "PWM5B",
        "PWM6A",
        "PWM6B",
        "PWM7A",
        "PWM7B",
        "ADCRAW",
#ifdef rp2350
        "PWM8A",
        "PWM8B",
        "PWM9A",
        "PWM9B",
        "PWM10A",
        "PWM10B",
        "PWM11A",
        "PWM11B",
#endif
        "PIO0",
#ifdef rp2350
        "PIO1",
        "PIO2",
        "FFIN",
#ifdef PICONITE
        "KEYBOARD"
#endif
#else
        "PIO1"
#endif
};
;

volatile int ExtCurrentConfig[NBRPINS + 1];
volatile int INT1Value, INT1InitTimer, INT1Timer;
volatile int INT2Value, INT2InitTimer, INT2Timer;
volatile int INT3Value, INT3InitTimer, INT3Timer;
volatile int INT4Value, INT4InitTimer, INT4Timer;
volatile int64_t INT1Count,INT2Count,INT3Count, INT4Count;
uint64_t uSecoffset=0;
uint32_t pinmask=0;
volatile uint64_t IRoffset=0;
void *IrDev, *IrCmd;
volatile char IrVarType;
volatile char IrState, IrGotMsg;
int IrBits, IrCount;
unsigned char *IrInterrupt;
int last_adc=99;
volatile int CallBackEnabled=0;
uint8_t IRpin=99;
uint8_t PWM0Apin=99;
uint8_t PWM1Apin=99;
uint8_t PWM2Apin=99;
uint8_t PWM3Apin=99;
uint8_t PWM4Apin=99;
uint8_t PWM5Apin=99;
uint8_t PWM6Apin=99;
uint8_t PWM7Apin=99;
#ifdef rp2350
uint8_t PWM8Apin=99;
uint8_t PWM9Apin=99;
uint8_t PWM10Apin=99;
uint8_t PWM11Apin=99;
#endif
uint8_t PWM0Bpin=99;
uint8_t PWM1Bpin=99;
uint8_t PWM2Bpin=99;
uint8_t PWM3Bpin=99;
uint8_t PWM4Bpin=99;
uint8_t PWM5Bpin=99;
uint8_t PWM6Bpin=99;
uint8_t PWM7Bpin=99;
#ifdef rp2350
uint8_t PWM8Bpin=99;
uint8_t PWM9Bpin=99;
uint8_t PWM10Bpin=99;
uint8_t PWM11Bpin=99;
#endif
uint8_t UART1RXpin=99;
uint8_t UART1TXpin=99;
uint8_t UART0TXpin=99;
uint8_t UART0RXpin=99;
uint8_t SPI1TXpin=99;
uint8_t SPI1RXpin=99;
uint8_t SPI1SCKpin=99;
uint8_t SPI0TXpin=99;
uint8_t SPI0RXpin=99;
uint8_t SPI0SCKpin=99;
uint8_t I2C1SDApin=99;
uint8_t I2C1SCLpin=99;
uint8_t I2C0SDApin=99;
uint8_t I2C0SCLpin=99;
uint8_t slice0=0,slice1=0,slice2=0,slice3=0,slice4=0,slice5=0,slice6=0,slice7=0;
#ifdef rp2350
uint8_t slice8=0,slice9=0,slice10=0,slice11=0;
bool fast_timer_active=false;
volatile uint64_t INT5Count, INT5Value, INT5InitTimer, INT5Timer;
#ifdef PICOMITE
int LocalKeyDown[7];
#endif
#endif
bool dmarunning=false;
bool ADCDualBuffering=false;
uint32_t ADCmax=0;
int ADCopen=0;
char *ADCInterrupt;
volatile MMFLOAT * volatile a1float=NULL, * volatile a2float=NULL, * volatile a3float=NULL, * volatile a4float=NULL;
volatile int ADCpos=0;
float frequency;
uint32_t ADC_dma_chan=ADC_DMA;
uint32_t ADC_dma_chan2=ADC_DMA2;
short *ADCbuffer=NULL;
void PWMoff(int slice);
volatile uint8_t *adcint=NULL; 
uint8_t *adcint1=NULL; 
uint8_t *adcint2=NULL; 
MMFLOAT ADCscale[4], ADCbottom[4];
extern void mouse0close(void);
//Vector to CFunction routine called every command (ie, from the BASIC interrupt checker)

uint64_t readusclock(void){
    return time_us_64()-uSecoffset;
}
void writeusclock(uint64_t timeset){
  uSecoffset=time_us_64()-(uint64_t)timeset;
}
uint64_t readIRclock(void){
    return time_us_64()-IRoffset;
}
void writeIRclock(uint64_t timeset){
  IRoffset=time_us_64()-(uint64_t)timeset;
}
#ifdef rp2350
const uint8_t PINMAP[48]={1,2,4,5,6,7,9,10,11,12,14,15,16,17,19,20,21,22,24,25,26,27,29,41,42,43,31,32,34,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62};
int codemap(int pin){
#ifdef PICOMITEWEB
    if(pin>29 || pin<0 || pin==23 || pin==24 || pin==25 || pin==29) error("Invalid GPIO");
#else
	if(pin>(rp2350a? 29:47) || pin<0) error("Invalid GPIO");
#endif
	return (int)PINMAP[pin];
}
#else
const uint8_t PINMAP[30]={1,2,4,5,6,7,9,10,11,12,14,15,16,17,19,20,21,22,24,25,26,27,29,41,42,43,31,32,34,44};
int codemap(int pin){
#ifdef PICOMITEWEB
    if(pin>29 || pin<0 || pin==23 || pin==24 || pin==25 || pin==29) error("Invalid GPIO");
#else
	if(pin>29 || pin<0) error("Invalid GPIO");
#endif
	return (int)PINMAP[pin];
}
#endif
int codecheck(unsigned char *line){
	if((line[0]=='G' || line[0]=='g') && (line[1]=='P' || line[1]=='p')){
		line+=2;
		if(isnamestart(*line) || *line=='.') return 1;

		if(isdigit(*line) && !isnamechar(line[1])) {
			return 0;
		}
		line++;
		
		if(!(isdigit(*line))) return 2;
		line++;
		if(isnamechar(*line)) return 3;
	} else return 4;
	return 0;
}
#ifdef rp2350
void __not_in_flash_func(on_pwm_wrap_1)(void) {
    pwm_clear_irq(0);
    INT5Count++;
}

#endif

void SoftReset(void){
    _excep_code = SOFT_RESET;
#ifdef USBKEYBOARD
    USBenabled=false;
    uSec(50000); //wait for outstanding requests to complete
#endif
	watchdog_enable(1, 1);
	while(1);
}
#ifdef rp2350
void __not_in_flash_func(PinSetBit)(int pin, unsigned int offset) {
#else
#if defined(PICOMITEVGA) || defined(PICOMITEWEB)
void PinSetBit(int pin, unsigned int offset) {
#else
void __not_in_flash_func(PinSetBit)(int pin, unsigned int offset) {
#endif
#endif
	switch (offset){
	case LATCLR:
		gpio_set_pulls(PinDef[pin].GPno,false,false);
		gpio_pull_down(PinDef[pin].GPno);
		gpio_put(PinDef[pin].GPno,GPIO_PIN_RESET);
		return;
	case LATSET:
		gpio_set_pulls(PinDef[pin].GPno,false,false);
		gpio_pull_up(PinDef[pin].GPno);
		gpio_put(PinDef[pin].GPno,GPIO_PIN_SET);
		return;
	case LATINV:
        gpio_xor_mask64(1<<PinDef[pin].GPno);
		return;
	case TRISSET:
        gpio_set_dir(PinDef[pin].GPno, GPIO_IN);
	    gpio_set_input_enabled(PinDef[pin].GPno, true);
        uSec(2);
        return;
	case TRISCLR:
        gpio_set_dir(PinDef[pin].GPno, GPIO_OUT);
        gpio_set_input_enabled(PinDef[pin].GPno, false);
        gpio_set_drive_strength (PinDef[pin].GPno, GPIO_DRIVE_STRENGTH_8MA);
        uSec(2);
        return;
	case CNPUSET:
		gpio_set_pulls(PinDef[pin].GPno,true,false);
	    return;
	case CNPDSET:
		gpio_set_pulls(PinDef[pin].GPno,false,true);
	    return;
	case CNPUCLR:
	case CNPDCLR:
		gpio_set_pulls(PinDef[pin].GPno,false,false);
		return;
	case ODCCLR:
        gpio_set_dir(PinDef[pin].GPno, GPIO_OUT);
		gpio_put(PinDef[pin].GPno,GPIO_PIN_RESET);
        uSec(2);
		return;
	case ODCSET:
		gpio_set_pulls(PinDef[pin].GPno,true,false);
        gpio_set_dir(PinDef[pin].GPno, GPIO_IN);
        uSec(2);
		return;
    case ANSELCLR:
        gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_SIO);
        gpio_set_dir(PinDef[pin].GPno, GPIO_IN);
        return;
    case ANSELSET:
        gpio_set_dir(PinDef[pin].GPno, GPIO_IN);
        gpio_disable_pulls(PinDef[pin].GPno);
        gpio_set_input_enabled(PinDef[pin].GPno, false);
        adc_select_input(PinDef[pin].ADCpin);
        return;
	default: error("Unknown PinSetBit command");
	}
}

int IsInvalidPin(int pin) {
#ifdef rp2350
    #ifdef PICOMITEWEB
        if(pin < 1 || pin > NBRPINS) return true;
    #else
        if(pin < 1 || pin > (rp2350a ? 44: NBRPINS)) return true;
    #endif
#else
    if(pin < 1 || pin > NBRPINS) return true;
#endif
    if(PinDef[pin].mode & UNUSED) return true;
    return false;
}
#ifdef PICOMITEWEB
void ExtSet(int pin, int val){
#else
#ifndef rp2350
#ifdef PICOMITEVGA
void ExtSet(int pin, int val){
#else
void __not_in_flash_func(ExtSet)(int pin, int val){
#endif
#else
void __not_in_flash_func(ExtSet)(int pin, int val){
#endif
#endif

    if(ExtCurrentConfig[pin] == EXT_NOT_CONFIG || ExtCurrentConfig[pin] == EXT_DIG_OUT/* || ExtCurrentConfig[pin] == EXT_OC_OUT*/) {
        PinSetBit(pin, val ? LATSET : LATCLR);
        if(ExtCurrentConfig[pin] == EXT_NOT_CONFIG){
            pinmask|=(1<<PinDef[pin].GPno);
            if(val)pinmask|=(1<<PinDef[pin].GPno);
            else pinmask &= (~(1<<PinDef[pin].GPno));
            gpio_set_input_enabled(PinDef[pin].GPno,false);
            last_adc=99;
        }
//        INTEnableInterrupts();
    }
    else if(ExtCurrentConfig[pin] == EXT_CNT_IN){ //allow the user to zero the count
        if(pin == Option.INT1pin) INT1Count=val;
        if(pin == Option.INT2pin) INT2Count=val;
        if(pin == Option.INT3pin) INT3Count=val;
        if(pin == Option.INT4pin) INT4Count=val;
    }
    else
        error("Pin %/| is not an output",pin,pin);
}
/*  @endcond */
#if defined(PICOMITEVGA) && !defined(rp2350)
    void cmd_sync(void){
#else
void __not_in_flash_func(cmd_sync)(void){
#endif
	uint64_t i;
    static uint64_t synctime=0,endtime=0;
	getargs(&cmdline,3,(unsigned char *)",");
	if(synctime && argc==0){
        while(time_us_64()<endtime){
            if(synctime-time_us_64()> 2000)CheckAbort();
        }
        endtime+=synctime;
	} else {
		if(argc==0)error("sync not initialised");
		i=getint(argv[0],0,0x7FFFFFFFFFFFFFFF);
		if(i){
			if(argc==3){
				if(checkstring(argv[2],(unsigned char *)"U")){
					i *= 1;
				} else if(checkstring(argv[2],(unsigned char *)"M")){
					i *= 1000;
				} else if(checkstring(argv[2],(unsigned char *)"S")){
					i *= 1000000;
				}
            }
            synctime=i;
            endtime=time_us_64()+synctime;
		} else {
			synctime=endtime=0;
		}
	}
}


// this is invoked as a command (ie, pin(3) = 1)
// first get the argument then step over the closing bracket.  Search through the rest of the command line looking
// for the equals sign and step over it, evaluate the rest of the command and set the pin accordingly
void cmd_pin(void) {
	int pin, value;
	unsigned char code;
	if(!(code=codecheck(cmdline)))cmdline+=2;
	pin = getinteger(cmdline);
	if(!code)pin=codemap(pin);
	if(IsInvalidPin(pin)) error("Invalid pin");
	while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
	if(!*cmdline) error("Invalid syntax");
	++cmdline;
	if(!*cmdline) error("Invalid syntax");
	value = getinteger(cmdline);
	ExtSet(pin, value);
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
void ClearPin(int pin){
    if(pin==IRpin)IRpin=99;
    if(pin==PWM0Apin)PWM0Apin=99;
    if(pin==PWM1Apin)PWM1Apin=99;
    if(pin==PWM2Apin)PWM2Apin=99;
    if(pin==PWM3Apin)PWM3Apin=99;
    if(pin==PWM4Apin)PWM4Apin=99;
    if(pin==PWM5Apin)PWM5Apin=99;
    if(pin==PWM6Apin)PWM6Apin=99;
    if(pin==PWM7Apin)PWM7Apin=99;
#ifdef rp2350
    if(pin==PWM8Apin)PWM8Apin=99;
    if(pin==PWM9Apin)PWM9Apin=99;
    if(pin==PWM10Apin)PWM10Apin=99;
    if(pin==PWM11Apin)PWM11Apin=99;
#endif
    if(pin==PWM0Bpin)PWM0Bpin=99;
    if(pin==PWM1Bpin)PWM1Bpin=99;
    if(pin==PWM2Bpin)PWM2Bpin=99;
    if(pin==PWM3Bpin)PWM3Bpin=99;
    if(pin==PWM4Bpin)PWM4Bpin=99;
    if(pin==PWM5Bpin)PWM5Bpin=99;
    if(pin==PWM6Bpin)PWM6Bpin=99;
    if(pin==PWM7Bpin)PWM7Bpin=99;
#ifdef rp2350
    if(pin==PWM8Bpin)PWM8Bpin=99;
    if(pin==PWM9Bpin)PWM9Bpin=99;
    if(pin==PWM10Bpin)PWM10Bpin=99;
    if(pin==PWM11Bpin)PWM11Bpin=99;
#endif
    if(pin==UART0TXpin)UART0TXpin=99;
    if(pin==UART0RXpin)UART0RXpin=99;
    if(pin==UART1RXpin)UART1RXpin=99;
    if(pin==UART1TXpin)UART1TXpin=99;
    if(pin==SPI1TXpin)SPI1TXpin=99;
    if(pin==SPI1RXpin)SPI1RXpin=99;
    if(pin==SPI1SCKpin)SPI1SCKpin=99;
    if(pin==SPI0TXpin)SPI0TXpin=99;
    if(pin==SPI0RXpin)SPI0RXpin=99;
    if(pin==SPI0SCKpin)SPI0SCKpin=99;
    if(pin==I2C1SDApin)I2C1SDApin=99;
    if(pin==I2C1SCLpin)I2C1SCLpin=99;
    if(pin==I2C0SDApin)I2C0SDApin=99;
    if(pin==I2C0SCLpin)I2C0SCLpin=99;
}
/****************************************************************************************************************************
Configure an I/O pin
*****************************************************************************************************************************/
void MIPS16 ExtCfg(int pin, int cfg, int option) {
  int i, tris=0, ana=0, edge;

    CheckPin(pin, CP_IGNORE_INUSE | CP_IGNORE_RESERVED);

    if(cfg >= EXT_DS18B20_RESERVED) {
        ExtCurrentConfig[pin] |= cfg;                                // don't do anything except set the config type
        return;
    }
    ClearPin(pin);  //disable the link to any special functions
    if(pin == Option.INT1pin) {
        if(CallBackEnabled==2) gpio_set_irq_enabled_with_callback(PinDef[pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
        else gpio_set_irq_enabled(PinDef[pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        CallBackEnabled &= (~2);
    }
    if(pin == Option.INT2pin) {
        if(CallBackEnabled==4) gpio_set_irq_enabled_with_callback(PinDef[pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
        else gpio_set_irq_enabled(PinDef[pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        CallBackEnabled &= (~4);
    }
    if(pin == Option.INT3pin) {
        if(CallBackEnabled==8) gpio_set_irq_enabled_with_callback(PinDef[pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
        else gpio_set_irq_enabled(PinDef[pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        CallBackEnabled &= (~8);
    }
    if(pin == Option.INT4pin) {
        if(CallBackEnabled==16) gpio_set_irq_enabled_with_callback(PinDef[pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
        else gpio_set_irq_enabled(PinDef[pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        CallBackEnabled &= (~16);
    }
    #ifdef rp2350
    if(pin==FAST_TIMER_PIN && ExtCurrentConfig[pin]==EXT_FAST_TIMER){
        slice0=0;
        pwm_set_irq1_enabled(0, false);
    }
    #endif
 

    // make sure any pullups/pulldowns are removed in case we are changing from a digital input
    gpio_disable_pulls(PinDef[pin].GPno);
    // disable ADC if we are changing from a analogue input
    if(ExtCurrentConfig[pin]==EXT_ANA_IN || ExtCurrentConfig[pin]==EXT_ADCRAW  )PinSetBit(pin, ANSELCLR);

    for(i = 0; i < NBRINTERRUPTS; i++)
        if(inttbl[i].pin == pin)
            inttbl[i].pin = 0;                                      // start off by disable a software interrupt (if set) on this pin
    gpio_set_input_enabled(PinDef[pin].GPno,false);
    gpio_deinit(PinDef[pin].GPno); 
    gpio_set_input_hysteresis_enabled(PinDef[pin].GPno,true);
    if(cfg!=EXT_NOT_CONFIG)gpio_init(PinDef[pin].GPno); 
    switch(cfg) {
        case EXT_NOT_CONFIG:    tris = 1; ana = 1;
//                                gpio_init(PinDef[pin].GPno); 
//		                        gpio_set_input_hysteresis_enabled(PinDef[pin].GPno,true);
                                gpio_set_input_enabled(PinDef[pin].GPno,false);
                                gpio_deinit(PinDef[pin].GPno);
                                switch(ExtCurrentConfig[pin]){      //Disable the pin numbers used by the special function code
                                     case EXT_IR:
				                        IRpin=99;
					                    break;
                                     case EXT_PWM0A:
				                        PWM0Apin=99;
					                    break;
                                     case EXT_PWM1A:
				                        PWM1Apin=99;
					                    break;
                                     case EXT_PWM2A:
				                        PWM2Apin=99;
					                    break;
                                     case EXT_PWM3A:
				                        PWM3Apin=99;
					                    break;
                                     case EXT_PWM4A:
				                        PWM4Apin=99;
					                    break;
                                     case EXT_PWM5A:
				                        PWM5Apin=99;
					                    break;
                                     case EXT_PWM6A:
				                        PWM6Apin=99;
					                    break;
                                     case EXT_PWM7A:
				                        PWM7Apin=99;
					                    break;
#ifdef rp2350
                                     case EXT_PWM8A:
				                        PWM8Apin=99;
					                    break;
                                     case EXT_PWM9A:
				                        PWM9Apin=99;
					                    break;
                                     case EXT_PWM10A:
				                        PWM10Apin=99;
					                    break;
                                     case EXT_PWM11A:
				                        PWM11Apin=99;
					                    break;
#endif
                                     case EXT_PWM0B:
				                        PWM0Bpin=99;
					                    break;
                                     case EXT_PWM1B:
				                        PWM1Bpin=99;
					                    break;
                                     case EXT_PWM2B:
				                        PWM2Bpin=99;
					                    break;
                                     case EXT_PWM3B:
				                        PWM3Bpin=99;
					                    break;
                                     case EXT_PWM4B:
				                        PWM4Bpin=99;
					                    break;
                                     case EXT_PWM5B:
				                        PWM5Bpin=99;
					                    break;
                                     case EXT_PWM6B:
				                        PWM6Bpin=99;
					                    break;
                                     case EXT_PWM7B:
				                        PWM7Bpin=99;
					                    break;
#ifdef rp2350
                                     case EXT_PWM8B:
				                        PWM8Bpin=99;
					                    break;
                                     case EXT_PWM9B:
				                        PWM9Bpin=99;
					                    break;
                                     case EXT_PWM10B:
				                        PWM10Bpin=99;
					                    break;
                                     case EXT_PWM11B:
				                        PWM11Bpin=99;
					                    break;
#endif
                                     case EXT_UART0TX:
				                        UART0TXpin=99;
					                    break;
                                    case EXT_UART0RX:
				                        UART0RXpin=99;
					                    break;
                                    case EXT_UART1TX:
				                        UART1TXpin=99;
					                    break;
                                    case EXT_UART1RX:
				                        UART1RXpin=99;
					                    break;
                                    case EXT_I2C0SDA:
				                        I2C0SDApin=99;
					                    break;
                                    case EXT_I2C0SCL:
				                        I2C0SCLpin=99;
					                    break;
                                    case EXT_I2C1SDA:
				                        I2C1SDApin=99;
					                    break;
                                    case EXT_I2C1SCL:
				                        I2C1SCLpin=99;
					                    break;
                                    case EXT_SPI0RX:
				                        SPI0RXpin=99;
					                    break;
                                    case EXT_SPI0TX:
				                        SPI0TXpin=99;
					                    break;
                                    case EXT_SPI0SCK:
				                        SPI0SCKpin=99;
					                    break;
                                    case EXT_SPI1RX:
				                        SPI1RXpin=99;
					                    break;
                                    case EXT_SPI1TX:
				                        SPI1TXpin=99;
					                    break;
                                    case EXT_SPI1SCK:
				                        SPI1SCKpin=99;
					                    break;
                                }
                                break;

#ifdef rp2350
        case EXT_ADCRAW:
        case EXT_ANA_IN:        if(!(PinDef[pin].mode & ANALOG_IN)) error("Invalid configuration");
                                if(pin<=44 && rp2350a==0) error("Invalid configuration");
                                if(pin> 44 && rp2350a) error("Invalid configuration");
                                tris = 1; ana = 0; 
                                break;
#else
        case EXT_ADCRAW:
        case EXT_ANA_IN:        if(!(PinDef[pin].mode & ANALOG_IN)) error("Invalid configuration");
                                tris = 1; ana = 0; 
                                break;
#endif
        case EXT_CNT_IN:        
        case EXT_FREQ_IN:   // same as counting, so fall through
        case EXT_PER_IN:        // same as counting, so fall through
                                    edge = GPIO_IRQ_EDGE_RISE;
                                    if(cfg==EXT_CNT_IN && option==2)edge = GPIO_IRQ_EDGE_FALL;
                                    if(cfg==EXT_CNT_IN && option>=3)edge = GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE;
                                    if(option==1 || option==4)gpio_pull_down (PinDef[pin].GPno);
                                    if(option==2 || option==5)gpio_pull_up (PinDef[pin].GPno);
                                    irq_set_priority(IO_IRQ_BANK0,0);
                                    PinSetBit(pin,TRISSET);
                                    if(pin == Option.INT1pin) {
                                    if(!CallBackEnabled){
                                        gpio_set_irq_enabled_with_callback(PinDef[pin].GPno, edge , true, &gpio_callback);
                                        CallBackEnabled=2;
                                    } else {
                                        gpio_set_irq_enabled(PinDef[pin].GPno, edge, true);
                                        CallBackEnabled|=2;
                                    }
                                    INT1Count = INT1Value = 0;
                                    INT1Timer = INT1InitTimer = option;  // only used for frequency and period measurement
                                    tris = 1; ana = 1;
		                            gpio_set_input_hysteresis_enabled(PinDef[pin].GPno,true);
                                    break;
                                }
                                if(pin == Option.INT2pin) {
                                    if(!CallBackEnabled){
                                        gpio_set_irq_enabled_with_callback(PinDef[pin].GPno, edge , true, &gpio_callback);
                                        CallBackEnabled=4;
                                    } else {
                                        gpio_set_irq_enabled(PinDef[pin].GPno, edge, true);
                                        CallBackEnabled|=4;
                                    }
                                    INT2Count = INT2Value = 0;
                                    INT2Timer = INT2InitTimer = option;  // only used for frequency and period measurement
                                    tris = 1; ana = 1; 
		                            gpio_set_input_hysteresis_enabled(PinDef[pin].GPno,true);
                                    break;
                                }
                                if(pin == Option.INT3pin) {
                                    if(!CallBackEnabled){
                                        gpio_set_irq_enabled_with_callback(PinDef[pin].GPno, edge , true, &gpio_callback);
                                        CallBackEnabled=8;
                                    } else {
                                        gpio_set_irq_enabled(PinDef[pin].GPno, edge, true);
                                        CallBackEnabled|=8;
                                    }
                                    INT3Count = INT3Value = 0;
                                    INT3Timer = INT3InitTimer = option;  // only used for frequency and period measurement
                                    tris = 1; ana = 1; 
		                            gpio_set_input_hysteresis_enabled(PinDef[pin].GPno,true);
                                    break;
                                }
                                if(pin == Option.INT4pin) {
                                    if(!CallBackEnabled){
                                        gpio_set_irq_enabled_with_callback(PinDef[pin].GPno, edge , true, &gpio_callback);
                                        CallBackEnabled=16;
                                    } else {
                                        gpio_set_irq_enabled(PinDef[pin].GPno, edge, true);
                                        CallBackEnabled|=16;
                                    }
                                    INT4Count = INT4Value = 0;
                                    INT4Timer = INT4InitTimer = option;  // only used for frequency and period measurement
                                    tris = 1; ana = 1; 
		                            gpio_set_input_hysteresis_enabled(PinDef[pin].GPno,true);
                                    break;
                                }
                                error("Invalid configuration");       // not an interrupt enabled pin
                                return;

        case EXT_INT_LO:                                              // same as digital input, so fall through
        case EXT_INT_HI:                                              // same as digital input, so fall through
        case EXT_INT_BOTH:                                            // same as digital input, so fall through
        case EXT_DIG_IN:        if(!(PinDef[pin].mode & DIGITAL_IN)) error("Invalid configuration");
                                if(option) PinSetBit(pin, option);
                                tris = 1; ana = 1; 
		                        gpio_set_input_hysteresis_enabled(PinDef[pin].GPno,true);
                                break;

        case EXT_PIO0_OUT:       
        case EXT_PIO1_OUT:       
#ifdef rp2350
        case EXT_PIO2_OUT:       
#endif
        case EXT_DIG_OUT:       if(!(PinDef[pin].mode & DIGITAL_OUT)) error("Invalid configuration");
                                tris = 0; ana = 1; 
                                gpio_set_drive_strength(PinDef[pin].GPno,GPIO_DRIVE_STRENGTH_8MA);
                                break;
#ifndef PICOMITEWEB
        case EXT_HEARTBEAT:     if(!(pin=HEARTBEATpin)) error("Invalid configuration");
                                tris = 0; ana = 1; 
                                break;
#endif
        case EXT_UART0TX:       if(!(PinDef[pin].mode & UART0TX)) error("Invalid configuration");
                                if((UART0TXpin!=99)) error("Already Set to pin %",UART0TXpin);
                                UART0TXpin=pin;
                                break;
        case EXT_UART0RX:       if(!(PinDef[pin].mode & UART0RX)) error("Invalid configuration");
                                if((UART0RXpin!=99)) error("Already Set to pin %",UART0RXpin);
                                UART0RXpin=pin;
                                break;
        case EXT_UART1TX:       if(!(PinDef[pin].mode & UART1TX)) error("Invalid configuration");
                                if((UART1TXpin!=99)) error("Already Set to pin %",UART1TXpin);
                                UART1TXpin=pin;
                                break;
        case EXT_UART1RX:       if(!(PinDef[pin].mode & UART1RX)) error("Invalid configuration");
                                if((UART1RXpin!=99)) error("Already Set to pin %",UART1RXpin);
                                UART1RXpin=pin;
                                break;
        case EXT_SPI0TX:        if(!(PinDef[pin].mode & SPI0TX)) error("Invalid configuration");
                                if(SPI0locked)error("SPI in use for SYSTEM SPI");
                                if((SPI0TXpin!=99 && SPI0TXpin!=pin)) error("Already Set to pin %",SPI0TXpin);
                                SPI0TXpin=pin;
                                break;
        case EXT_SPI0RX:        if(!(PinDef[pin].mode & SPI0RX)) error("Invalid configuration");
                                if(SPI0locked)error("SPI in use for SYSTEM SPI");
                                if((SPI0RXpin!=99 && SPI0RXpin!=pin)) error("Already Set to pin %",SPI0RXpin);
                                SPI0RXpin=pin;
                                break;
        case EXT_SPI0SCK:       if(!(PinDef[pin].mode & SPI0SCK)) error("Invalid configuration");
                                if(SPI0locked)error("SPI in use for SYSTEM SPI");
                                if((SPI0SCKpin!=99 && SPI0SCKpin!=pin)) error("Already Set to pin %",SPI0SCKpin);
                                SPI0SCKpin=pin;
                                break;
        case EXT_SPI1TX:        if(!(PinDef[pin].mode & SPI1TX)) error("Invalid configuration");
                                if(SPI1locked)error("SPI2 in use for SYSTEM SPI");
                                if((SPI1TXpin!=99 && SPI1TXpin!=pin)) error("Already Set to pin %",SPI1TXpin);
                                SPI1TXpin=pin;
                                break;
        case EXT_SPI1RX:        if(!(PinDef[pin].mode & SPI1RX)) error("Invalid configuration");
                                if(SPI1locked)error("SPI2 in use for SYSTEM SPI");
                                if((SPI1RXpin!=99 && SPI1RXpin!=pin)) error("Already Set to pin %",SPI1RXpin);
                                SPI1RXpin=pin;
                                break;
        case EXT_SPI1SCK:       if(!(PinDef[pin].mode & SPI1SCK)) error("Invalid configuration");
                                if(SPI1locked)error("SPI2 in use for SYSTEM SPI");
                                if((SPI1SCKpin!=99 && SPI1SCKpin!=pin)) error("Already Set to pin %",SPI1SCKpin);
                                SPI1SCKpin=pin;
                                break;
        case EXT_IR:            if((IRpin!=99)) error("Already Set to pin %",IRpin);
                                IRpin=pin;
                                break;
        case EXT_PWM0A:         if(!(PinDef[pin].mode & PWM0A)) error("Invalid configuration");
                                if((PWM0Apin!=99 && PWM0Apin!=pin)) error("Already Set to pin %",PWM0Apin);
                                PWM0Apin=pin;
                                break;
        case EXT_PWM1A:         if(!(PinDef[pin].mode & PWM1A)) error("Invalid configuration");
                                if((PWM1Apin!=99 && PWM1Apin!=pin)) error("Already Set to pin %",PWM1Apin);
                                PWM1Apin=pin;
                                break;
        case EXT_PWM2A:         if(!(PinDef[pin].mode & PWM2A)) error("Invalid configuration");
                                if((PWM2Apin!=99 && PWM2Apin!=pin)) error("Already Set to pin %",PWM2Apin);
                                PWM2Apin=pin;
                                gpio_set_drive_strength (PinDef[pin].GPno, GPIO_DRIVE_STRENGTH_8MA);
                                gpio_set_slew_rate(PinDef[pin].GPno,GPIO_SLEW_RATE_FAST);
                                break;
        case EXT_PWM3A:         if(!(PinDef[pin].mode & PWM3A)) error("Invalid configuration");
                                if((PWM3Apin!=99 && PWM3Apin!=pin)) error("Already Set to pin %",PWM3Apin);
                                PWM3Apin=pin;
                                break;
        case EXT_PWM4A:         if(!(PinDef[pin].mode & PWM4A)) error("Invalid configuration");
                                if((PWM4Apin!=99 && PWM4Apin!=pin)) error("Already Set to pin %",PWM4Apin);
                                PWM4Apin=pin;
                                break;
        case EXT_PWM5A:         if(!(PinDef[pin].mode & PWM5A)) error("Invalid configuration");
                                if((PWM5Apin!=99 && PWM5Apin!=pin)) error("Already Set to pin %",PWM5Apin);
                                PWM5Apin=pin;
                                break;
        case EXT_PWM6A:         if(!(PinDef[pin].mode & PWM6A)) error("Invalid configuration");
                                if((PWM6Apin!=99 && PWM6Apin!=pin)) error("Already Set to pin %",PWM6Apin);
                                PWM6Apin=pin;
                                break;
        case EXT_PWM7A:         if(!(PinDef[pin].mode & PWM7A)) error("Invalid configuration");
                                if((PWM7Apin!=99 && PWM7Apin!=pin)) error("Already Set to pin %",PWM7Apin);
                                PWM7Apin=pin;
                                break;
#ifdef rp2350
        case EXT_PWM8A:         if(!(PinDef[pin].mode & PWM8A) || rp2350a) error("Invalid configuration");
                                if((PWM8Apin!=99 && PWM8Apin!=pin)) error("Already Set to pin %",PWM8Apin);
                                PWM8Apin=pin;
                                break;
        case EXT_PWM9A:         if(!(PinDef[pin].mode & PWM9A) || rp2350a) error("Invalid configuration");
                                if((PWM9Apin!=99 && PWM9Apin!=pin)) error("Already Set to pin %",PWM9Apin);
                                PWM9Apin=pin;
                                break;
        case EXT_PWM10A:         if(!(PinDef[pin].mode & PWM10A) || rp2350a) error("Invalid configuration");
                                if((PWM10Apin!=99 && PWM10Apin!=pin)) error("Already Set to pin %",PWM10Apin);
                                PWM10Apin=pin;
                                break;
        case EXT_PWM11A:         if(!(PinDef[pin].mode & PWM11A) || rp2350a) error("Invalid configuration");
                                if((PWM11Apin!=99 && PWM11Apin!=pin)) error("Already Set to pin %",PWM11Apin);
                                PWM11Apin=pin;
                                break;
#endif
        case EXT_PWM0B:         if(!(PinDef[pin].mode & PWM0B)) error("Invalid configuration");
                                if((PWM0Bpin!=99 && PWM0Bpin!=pin)) error("Already Set to pin %",PWM0Bpin);
                                PWM0Bpin=pin;
                                break;
        case EXT_PWM1B:         if(!(PinDef[pin].mode & PWM1B)) error("Invalid configuration");
                                if((PWM1Bpin!=99 && PWM1Bpin!=pin)) error("Already Set to pin %",PWM1Bpin);
                                PWM1Bpin=pin;
                                break;
        case EXT_PWM2B:         if(!(PinDef[pin].mode & PWM2B)) error("Invalid configuration");
                                if((PWM2Bpin!=99 && PWM2Bpin!=pin)) error("Already Set to pin %",PWM2Bpin);
                                PWM2Bpin=pin;
                                break;
        case EXT_PWM3B:         if(!(PinDef[pin].mode & PWM3B)) error("Invalid configuration");
                                if((PWM3Bpin!=99 && PWM3Bpin!=pin)) error("Already Set to pin %",PWM3Bpin);
                                PWM3Bpin=pin;
                                break;
        case EXT_PWM4B:         if(!(PinDef[pin].mode & PWM4B)) error("Invalid configuration");
                                if((PWM4Bpin!=99 && PWM4Bpin!=pin)) error("Already Set to pin %",PWM4Bpin);
                                PWM4Bpin=pin;
                                break;
        case EXT_PWM5B:         if(!(PinDef[pin].mode & PWM5B)) error("Invalid configuration");
                                if((PWM5Bpin!=99 && PWM5Bpin!=pin)) error("Already Set to pin %",PWM5Bpin);
                                PWM5Bpin=pin;
                                break;
        case EXT_PWM6B:         if(!(PinDef[pin].mode & PWM6B)) error("Invalid configuration");
                                if((PWM6Bpin!=99 && PWM6Bpin!=pin)) error("Already Set to pin %",PWM6Bpin);
                                PWM6Bpin=pin;
                                break;
        case EXT_PWM7B:         if(!(PinDef[pin].mode & PWM7B)) error("Invalid configuration");
                                if((PWM7Bpin!=99 && PWM7Bpin!=pin)) error("Already Set to pin %",PWM7Bpin);
                                PWM7Bpin=pin;
                                break;
#ifdef rp2350
#ifndef PICOMITEWEB
        case EXT_PWM8B:         if(!(PinDef[pin].mode & PWM8B) || rp2350a) error("Invalid configuration");
                                if((PWM8Bpin!=99 && PWM8Bpin!=pin)) error("Blready Set to pin %",PWM8Bpin);
                                PWM8Bpin=pin;
                                break;
        case EXT_PWM9B:         if(!(PinDef[pin].mode & PWM9B) || rp2350a) error("Invalid configuration");
                                if((PWM9Bpin!=99 && PWM9Bpin!=pin)) error("Blready Set to pin %",PWM9Bpin);
                                PWM9Bpin=pin;
                                break;
        case EXT_PWM10B:         if(!(PinDef[pin].mode & PWM10B) || rp2350a) error("Invalid configuration");
                                if((PWM10Bpin!=99 && PWM10Bpin!=pin)) error("Blready Set to pin %",PWM10Bpin);
                                PWM10Bpin=pin;
                                break;
        case EXT_PWM11B:         if(!(PinDef[pin].mode & PWM11B) || rp2350a) error("Invalid configuration");
                                if((PWM11Bpin!=99 && PWM11Bpin!=pin)) error("Blready Set to pin %",PWM11Bpin);
                                PWM11Bpin=pin;
                                break;
#endif
        case EXT_FAST_TIMER:    if(!(PinDef[pin].mode & PWM0B)) error("Invalid configuration");
                                if((PWM0Bpin!=99 && PWM0Bpin!=pin)) error("Already Set to pin %",PWM0Bpin);
                                PWM0Bpin=pin;
                                INT5Count = INT5Value = 0;
                                INT5Timer = INT5InitTimer = option;  // only used for frequency and period measurement
                                tris = 1; ana = 1;
//                                PinSetBit(pin,TRISSET);
                                gpio_set_input_hysteresis_enabled(PinDef[pin].GPno,true);
                                gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_PWM);
                                pwm_config cfg = pwm_get_default_config();
                                pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_RISING);
                                pwm_config_set_clkdiv(&cfg, 1);
                                pwm_init(0, &cfg, false);
                                pwm_set_wrap(0, 49999);
                                pwm_clear_irq(0);
                                irq_set_exclusive_handler(PWM_IRQ_WRAP_1, on_pwm_wrap_1);
                                irq_set_enabled(PWM_IRQ_WRAP_1, true);
                                irq_set_priority(PWM_IRQ_WRAP_1,0);
	                            pwm_set_irq1_enabled(0, true);
                                pwm_set_enabled(0, true); 
                                break;
#endif
        case EXT_I2C0SDA:       if(!(PinDef[pin].mode & I2C0SDA)) error("Invalid configuration");
                                if(I2C0locked)error("I2C in use for SYSTEM I2C");
                                if((I2C0SDApin!=99 && I2C0SDApin!=pin)) error("Already Set to pin %",I2C0SDApin);
                                I2C0SDApin=pin;
                                break;
        case EXT_I2C0SCL:       if(!(PinDef[pin].mode & I2C0SCL)) error("Invalid configuration");
                                if(I2C0locked)error("I2C in use for SYSTEM I2C");
                                 if((I2C0SCLpin!=99 && I2C0SCLpin!=pin)) error("Already Set to pin %",I2C0SCLpin);
                                I2C0SCLpin=pin;
                                break;
        case EXT_I2C1SDA:       if(!(PinDef[pin].mode & I2C1SDA)) error("Invalid configuration");
                                if(I2C1locked)error("I2C2 in use for SYSTEM I2C");
                                 if((I2C1SDApin!=99) && I2C1SDApin!=pin) error("Already Set to pin %",I2C1SDApin);
                                I2C1SDApin=pin;
                                break;
        case EXT_I2C1SCL:       if(!(PinDef[pin].mode & I2C1SCL)) error("Invalid configuration");
                                if(I2C1locked)error("I2C2 in use for SYSTEM I2C");
                                 if((I2C1SCLpin!=99 && I2C1SCLpin!=pin)) error("Already Set to pin %",I2C1SCLpin);
                                I2C1SCLpin=pin;
                                break;
        default:                error("Invalid configuration");
                              return;
  }
    ExtCurrentConfig[pin] = cfg;
    if(cfg<=EXT_INT_BOTH || cfg==EXT_ADCRAW){
        //    *GetPortAddr(pin, ana ? ANSELCLR : ANSELSET) = (1 << GetPinBit(pin));// if ana = 1 then it is a digital I/O
        PinSetBit(pin, tris ? TRISSET : TRISCLR);                         // if tris = 1 then it is an input
        if(!tris && (pinmask & (1<<PinDef[pin].GPno))){
            gpio_put(PinDef[pin].GPno,GPIO_PIN_SET);
        }
        pinmask &= (~(1<<PinDef[pin].GPno));
        if(cfg == EXT_NOT_CONFIG) ExtSet(pin, 0);                         // set the default output to low
        if(ana==0)PinSetBit(pin, ANSELSET);
    }
    else if(cfg>=EXT_UART0TX && cfg<=EXT_UART1RX){
        gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_UART);
        if(cfg==EXT_UART0RX || cfg==EXT_UART1RX)gpio_set_pulls(PinDef[pin].GPno,true,false);
    }
    else if(cfg>=EXT_I2C0SDA && cfg<=EXT_I2C1SCL)gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_I2C);
    else if(cfg>=EXT_SPI0RX && cfg<=EXT_SPI1SCK)gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_SPI);
#ifdef rp2350
    else if(cfg>=EXT_PWM0A && cfg<=(rp2350a ? EXT_PWM7B : EXT_PWM11B))gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_PWM);
#else
    else if(cfg>=EXT_PWM0A && cfg<=EXT_PWM7B)gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_PWM);
#endif
    else if(cfg==EXT_PIO0_OUT){
	    gpio_set_input_enabled(PinDef[pin].GPno, true);
        gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_PIO0);
    }
    else if(cfg==EXT_PIO1_OUT){
	    gpio_set_input_enabled(PinDef[pin].GPno, true);
        gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_PIO1);
    }
#ifdef rp2350
    else if(cfg==EXT_PIO2_OUT){
	    gpio_set_input_enabled(PinDef[pin].GPno, true);
        gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_PIO2);
    }
#endif
    uSec(2);
}
extern int adc_clk_div;
#ifndef rp2350
#ifdef PICOMITEVGA
int64_t ExtInp(int pin){
#else
int64_t __not_in_flash_func(ExtInp)(int pin){
#endif
#else
int64_t __not_in_flash_func(ExtInp)(int pin){
#endif
    if(ExtCurrentConfig[pin]==EXT_ANA_IN || ExtCurrentConfig[pin]==EXT_ADCRAW){
        if(adc_clk_div!=adc_hw->div){
            SetADCFreq(500000.0);
        }

        if(last_adc!=pin){
            last_adc=pin;
            adc_select_input(PinDef[pin].ADCpin);
        }
        int a= adc_read();   
        if(adc_hw->cs & (ADC_CS_ERR_STICKY_BITS | ADC_CS_ERR_BITS)) {
            hw_set_bits(&adc_hw->cs, ADC_CS_ERR_STICKY_BITS);
            a=-1;
        }
        return a;      
    } else if(ExtCurrentConfig[pin] == EXT_FREQ_IN || ExtCurrentConfig[pin] == EXT_PER_IN) {
      // select input channel
        if(pin == Option.INT1pin) return INT1Value;
        if(pin == Option.INT2pin) return INT2Value;
        if(pin == Option.INT3pin) return INT3Value;
        if(pin == Option.INT4pin) return INT4Value;
    }  else if(ExtCurrentConfig[pin] == EXT_CNT_IN) {
        // select input channel
            if(pin == Option.INT1pin) return INT1Count;
            if(pin == Option.INT2pin) return INT2Count;
            if(pin == Option.INT3pin) return INT3Count;
            if(pin == Option.INT4pin) return INT4Count;
    }  else if(ExtCurrentConfig[pin] == EXT_DIG_OUT) {
        return gpio_get_out_level(PinDef[pin].GPno);
    }  else {
        return  gpio_get(PinDef[pin].GPno);
    }
    return 0;
}
/*  @endcond */
void MIPS16 cmd_setpin(void) {
	int i, pin, pin2=0, pin3=0, value=-1, value2=0, value3=0, option = 0;
	getargs(&cmdline, 7, (unsigned char *)",");
	if(argc%2 == 0 || argc < 3) error("Argument count");
	char code;
	if(!(code=codecheck(argv[0])))argv[0]+=2;
	pin = getinteger(argv[0]);
	if(!code)pin=codemap(pin);

    if(checkstring(argv[2], (unsigned char *)"OFF") || checkstring(argv[2], (unsigned char *)"0"))
        value = EXT_NOT_CONFIG;
    else if(checkstring(argv[2], (unsigned char *)"AIN"))
        value = EXT_ANA_IN;
    else if(checkstring(argv[2], (unsigned char *)"ARAW"))
        value = EXT_ADCRAW;
    else if(checkstring(argv[2], (unsigned char *)"DIN"))
        value = EXT_DIG_IN;
    else if(checkstring(argv[2], (unsigned char *)"FIN"))
        value = EXT_FREQ_IN;
    else if(checkstring(argv[2], (unsigned char *)"PIN"))
        value = EXT_PER_IN;
    else if(checkstring(argv[2], (unsigned char *)"CIN"))
        value = EXT_CNT_IN;
    else if(checkstring(argv[2], (unsigned char *)"INTH"))
        value = EXT_INT_HI;
    else if(checkstring(argv[2], (unsigned char *)"INTL"))
        value = EXT_INT_LO;
    else if(checkstring(argv[2], (unsigned char *)"DOUT"))
        value = EXT_DIG_OUT;
    else if(checkstring(argv[2], (unsigned char *)"HEARTBEAT"))
        value = EXT_HEARTBEAT;
    else if(checkstring(argv[2], (unsigned char *)"INTB"))
        value = EXT_INT_BOTH;
    else if(checkstring(argv[2], (unsigned char *)"IR"))
        value = EXT_IR;
    else if(checkstring(argv[2], (unsigned char *)"PWM0A"))
        value = EXT_PWM0A;
    else if(checkstring(argv[2], (unsigned char *)"PWM1A"))
        value = EXT_PWM1A;
    else if(checkstring(argv[2], (unsigned char *)"PWM2A"))
        value = EXT_PWM2A;
    else if(checkstring(argv[2], (unsigned char *)"PWM3A"))
        value = EXT_PWM3A;
    else if(checkstring(argv[2], (unsigned char *)"PWM4A"))
        value = EXT_PWM4A;
    else if(checkstring(argv[2], (unsigned char *)"PWM5A"))
        value = EXT_PWM5A;
    else if(checkstring(argv[2], (unsigned char *)"PWM6A"))
        value = EXT_PWM6A;
    else if(checkstring(argv[2], (unsigned char *)"PWM7A"))
        value = EXT_PWM7A;
#ifdef rp2350
    else if(checkstring(argv[2], (unsigned char *)"FFIN"))
        value = EXT_FAST_TIMER;
#endif
#ifdef rp2350
    else if(checkstring(argv[2], (unsigned char *)"PWM8A"))
        value = EXT_PWM8A;
    else if(checkstring(argv[2], (unsigned char *)"PWM9A"))
        value = EXT_PWM9A;
    else if(checkstring(argv[2], (unsigned char *)"PWM10A"))
        value = EXT_PWM10A;
    else if(checkstring(argv[2], (unsigned char *)"PWM711"))
        value = EXT_PWM11A;
#endif
    else if(checkstring(argv[2], (unsigned char *)"PWM0B"))
        value = EXT_PWM0B;
    else if(checkstring(argv[2], (unsigned char *)"PWM1B"))
        value = EXT_PWM1B;
    else if(checkstring(argv[2], (unsigned char *)"PWM2B"))
        value = EXT_PWM2B;
    else if(checkstring(argv[2], (unsigned char *)"PWM3B"))
        value = EXT_PWM3B;
    else if(checkstring(argv[2], (unsigned char *)"PWM4B"))
        value = EXT_PWM4B;
    else if(checkstring(argv[2], (unsigned char *)"PWM5B"))
        value = EXT_PWM5B;
    else if(checkstring(argv[2], (unsigned char *)"PWM6B"))
        value = EXT_PWM6B;
    else if(checkstring(argv[2], (unsigned char *)"PWM7B"))
        value = EXT_PWM7B;
#ifdef rp2350
    else if(checkstring(argv[2], (unsigned char *)"PWM8B"))
        value = EXT_PWM8B;
    else if(checkstring(argv[2], (unsigned char *)"PWM9B"))
        value = EXT_PWM9B;
    else if(checkstring(argv[2], (unsigned char *)"PWM10B"))
        value = EXT_PWM10B;
    else if(checkstring(argv[2], (unsigned char *)"PWM11B"))
        value = EXT_PWM11B;
#endif
    else if(checkstring(argv[2], (unsigned char *)"PIO0"))
        value = EXT_PIO0_OUT;
    else if(checkstring(argv[2], (unsigned char *)"PIO1"))
        value = EXT_PIO1_OUT;
#ifdef rp2350
    else if(checkstring(argv[2], (unsigned char *)"PIO2"))
        value = EXT_PIO2_OUT;
#endif
    else if(checkstring(argv[2],(unsigned char *)"PWM")){
        if(PinDef[pin].mode & PWM0A)value = EXT_PWM0A;
        else if(PinDef[pin].mode & PWM0B)value = EXT_PWM0B;
        else if(PinDef[pin].mode & PWM1A)value = EXT_PWM1A;
        else if(PinDef[pin].mode & PWM1B)value = EXT_PWM1B;
        else if(PinDef[pin].mode & PWM2A)value = EXT_PWM2A;
        else if(PinDef[pin].mode & PWM2B)value = EXT_PWM2B;
        else if(PinDef[pin].mode & PWM3A)value = EXT_PWM3A;
        else if(PinDef[pin].mode & PWM3B)value = EXT_PWM3B;
        else if(PinDef[pin].mode & PWM4A)value = EXT_PWM4A;
        else if(PinDef[pin].mode & PWM4B)value = EXT_PWM4B;
        else if(PinDef[pin].mode & PWM5A)value = EXT_PWM5A;
        else if(PinDef[pin].mode & PWM5B)value = EXT_PWM5B;
        else if(PinDef[pin].mode & PWM6A)value = EXT_PWM6A;
        else if(PinDef[pin].mode & PWM6B)value = EXT_PWM6B;
        else if(PinDef[pin].mode & PWM7A)value = EXT_PWM7A;
        else if(PinDef[pin].mode & PWM7B)value = EXT_PWM7B;
#ifdef rp2350
        else if(PinDef[pin].mode & PWM8A)value = EXT_PWM8A;
        else if(PinDef[pin].mode & PWM8B)value = EXT_PWM8B;
        else if(PinDef[pin].mode & PWM9A)value = EXT_PWM9A;
        else if(PinDef[pin].mode & PWM9B)value = EXT_PWM9B;
        else if(PinDef[pin].mode & PWM10A)value = EXT_PWM10A;
        else if(PinDef[pin].mode & PWM10B)value = EXT_PWM10B;
        else if(PinDef[pin].mode & PWM11A)value = EXT_PWM11A;
        else if(PinDef[pin].mode & PWM11B)value = EXT_PWM11B;
#endif
        else error("Invalid configuration");
    }  else if(checkstring(argv[2],(unsigned char *)"INT")){
        if(pin==Option.INT1pin)value = EXT_INT1;
        else if(pin==Option.INT2pin)value = EXT_INT2;
        else if(pin==Option.INT3pin)value = EXT_INT3;
        else if(pin==Option.INT4pin)value = EXT_INT4;
        else error("Invalid configuration");
    }
    if(value!=-1)goto process;
    if(argc<5)error("Syntax"); 
    if(checkstring(argv[4],(unsigned char *)"COM1")){
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin2 = getinteger(argv[2]);
        if(!code)pin2=codemap(pin2);
        if(PinDef[pin].mode & UART0TX)value = EXT_UART0TX;
        else if(PinDef[pin].mode & UART0RX)value = EXT_UART0RX;
        else error("Invalid configuration");
        if(PinDef[pin2].mode & UART0TX)value2 = EXT_UART0TX;
        else if(PinDef[pin2].mode & UART0RX)value2 = EXT_UART0RX;
        else error("Invalid configuration");
        if(value==value2)error("Invalid configuration");
    } else if(checkstring(argv[4],(unsigned char *)"COM2")){
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin2 = getinteger(argv[2]);
        if(!code)pin2=codemap(pin2);
        if(PinDef[pin].mode & UART1TX)value = EXT_UART1TX;
        else if(PinDef[pin].mode & UART1RX)value = EXT_UART1RX;
        else error("Invalid configuration");
        if(PinDef[pin2].mode & UART1TX)value2 = EXT_UART1TX;
        else if(PinDef[pin2].mode & UART1RX)value2 = EXT_UART1RX;
        else error("Invalid configuration");
        if(value==value2)error("Invalid configuration");
    }  else if(checkstring(argv[4],(unsigned char *)"I2C")){
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin2 = getinteger(argv[2]);
        if(!code)pin2=codemap(pin2);
        if(PinDef[pin].mode & I2C0SCL)value = EXT_I2C0SCL;
        else if(PinDef[pin].mode & I2C0SDA)value = EXT_I2C0SDA;
        else error("Invalid configuration");
        if(PinDef[pin2].mode & I2C0SCL)value2 = EXT_I2C0SCL;
        else if(PinDef[pin2].mode & I2C0SDA)value2 = EXT_I2C0SDA;
        else error("Invalid configuration");
        if(value==value2)error("Invalid configuration");
    }  else if(checkstring(argv[4],(unsigned char *)"I2C2")){
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin2 = getinteger(argv[2]);
        if(!code)pin2=codemap(pin2);
        if(PinDef[pin].mode & I2C1SCL)value = EXT_I2C1SCL;
        else if(PinDef[pin].mode & I2C1SDA)value = EXT_I2C1SDA;
        else error("Invalid configuration");
        if(PinDef[pin2].mode & I2C1SCL)value2 = EXT_I2C1SCL;
        else if(PinDef[pin2].mode & I2C1SDA)value2 = EXT_I2C1SDA;
        else error("Invalid configuration");
        if(value==value2)error("Invalid configuration");
    }  
    if(value!=-1)goto process;
    if(argc<7)error("Syntax"); 
    if(checkstring(argv[6],(unsigned char *)"SPI")){
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin2 = getinteger(argv[2]);
        if(!code)pin2=codemap(pin2);
        if(!(code=codecheck(argv[4])))argv[4]+=2;
        pin3 = getinteger(argv[4]);
        if(!code)pin3=codemap(pin3);
        if(PinDef[pin].mode & SPI0RX)value = EXT_SPI0RX;
        else if(PinDef[pin].mode & SPI0TX)value = EXT_SPI0TX;
        else if(PinDef[pin].mode & SPI0SCK)value = EXT_SPI0SCK;
        else error("Invalid configuration");
        if(PinDef[pin2].mode & SPI0RX)value2 = EXT_SPI0RX;
        else if(PinDef[pin2].mode & SPI0TX)value2 = EXT_SPI0TX;
        else if(PinDef[pin2].mode & SPI0SCK)value2 = EXT_SPI0SCK;
        else error("Invalid configuration");
        if(PinDef[pin3].mode & SPI0RX)value3 = EXT_SPI0RX;
        else if(PinDef[pin3].mode & SPI0TX)value3 = EXT_SPI0TX;
        else if(PinDef[pin3].mode & SPI0SCK)value3 = EXT_SPI0SCK;
        else error("Invalid configuration");
        if(value==value2 || value==value3 || value2==value3)error("Invalid configuration");
    }  else if(checkstring(argv[6],(unsigned char *)"SPI2")){
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin2 = getinteger(argv[2]);
        if(!code)pin2=codemap(pin2);
        if(!(code=codecheck(argv[4])))argv[4]+=2;
        pin3 = getinteger(argv[4]);
        if(!code)pin3=codemap(pin3);
        if(PinDef[pin].mode & SPI1RX)value = EXT_SPI1RX;
        else if(PinDef[pin].mode & SPI1TX)value = EXT_SPI1TX;
        else if(PinDef[pin].mode & SPI1SCK)value = EXT_SPI1SCK;
        else error("Invalid configuration");
        if(PinDef[pin2].mode & SPI1RX)value2 = EXT_SPI1RX;
        else if(PinDef[pin2].mode & SPI1TX)value2 = EXT_SPI1TX;
        else if(PinDef[pin2].mode & SPI1SCK)value2 = EXT_SPI1SCK;
        else error("Invalid configuration");
        if(PinDef[pin3].mode & SPI1RX)value3 = EXT_SPI1RX;
        else if(PinDef[pin3].mode & SPI1TX)value3 = EXT_SPI1TX;
        else if(PinDef[pin3].mode & SPI1SCK)value3 = EXT_SPI1SCK;
        else error("Invalid configuration");
        if(value==value2 || value==value3 || value2==value3)error("Invalid configuration");
    } else error("Syntax");
//        value = getint(argv[2], 1, 9);
process:
    // check for any options
    switch(value) {
    	case EXT_ANA_IN:
        case EXT_ADCRAW: if(argc == 5) {
    						option = getint((argv[4]), 8, 12);
    						if(option & 1)error("Invalid bit count");
        					} else
        					option = 12;
        break;

        case EXT_DIG_IN:    if(argc == 5) {
                                if(checkstring(argv[4], (unsigned char *)"PULLUP")) option = CNPUSET;
                                else if(checkstring(argv[4], (unsigned char *)"PULLDOWN")) {
#ifdef rp2350
                                    PinSetBit(pin,TRISCLR);
                                    PinSetBit(pin,LATCLR);
                                    PinSetBit(pin,TRISSET);
#endif
                                    option = CNPDSET;
                                }
                                else error("Invalid configuration");
                            } else
                                option = 0;
                            break;
        case EXT_INT_HI:
        case EXT_INT_LO:
        case EXT_INT_BOTH:  if(argc == 7) {
                                if(checkstring(argv[6], (unsigned char *)"PULLUP")) option = CNPUSET;
                                else if(checkstring(argv[6], (unsigned char *)"PULLDOWN")) {
#ifdef rp2350
                                    PinSetBit(pin,TRISCLR);
                                    PinSetBit(pin,LATCLR);
                                    PinSetBit(pin,TRISSET);
#endif
                                    option = CNPDSET;
                                }
                                else error("Invalid configuration");
                            } else
                                option = 0;
                            break;
        case EXT_FREQ_IN:   if(argc == 5)
                                option = getint((argv[4]), 1, 100000);
                            else
                                option = 1000;
                            break;
        case EXT_PER_IN:   if(argc == 5)
                                option = getint((argv[4]), 1, 10000);
                            else
                                option = 1;
                            break;
        case EXT_CNT_IN:   if(argc == 5)
                                option = getint((argv[4]), 1, 10);
                            else
                                option = 1;
                            break;
#ifdef rp2350
        case EXT_FAST_TIMER:    if(pin!=FAST_TIMER_PIN)error("Use pin2/GP1 for fast counter");
                            if(BacklightSlice==0)error("Channel in use for LCD backlight");
#ifdef PICOMITE
                            if(KeyboardlightSlice==0)error("Channel in use for keyboard backlight");
#endif
                            if(Option.AUDIO_SLICE==0)error("Channel in use for Audio");
                            if(CameraSlice==0)error("Channel in use for Camera");
                            if(argc == 5)
                                option = getint((argv[4]), 1, 100000);
                            else
                                option = 1000;
                            break;
#endif
        case EXT_DIG_OUT:
        case EXT_HEARTBEAT:   
                            option=0;
        default:            if(argc > 3 && !value2) error("Unexpected text");
    }
    // this allows the user to set a software interrupt on the touch IRQ pin if the GUI environment is not enabled
    if(pin == Option.TOUCH_IRQ)
    #ifdef GUICONTROLS
    if(Option.MaxCtrls==0) 
    #endif
    {
        if(value == EXT_INT_HI || value == EXT_INT_LO || value == EXT_INT_BOTH)
            ExtCurrentConfig[pin] = value;
        else if(value == EXT_NOT_CONFIG) {
            ExtCurrentConfig[pin] = EXT_BOOT_RESERVED;
            for(i = 0; i < NBRINTERRUPTS; i++)
                if(inttbl[i].pin == pin)
                    inttbl[i].pin = 0;                              // disable the software interrupt on this pin
        }
        else
            error("Pin %/| is reserved on startup", pin,pin);
    } 

    CheckPin(pin, CP_IGNORE_INUSE);
    ExtCfg(pin, value, option);

    if(value2)    {
        CheckPin(pin2, CP_IGNORE_INUSE);
        ExtCfg(pin2, value2, option);
    }
    if(value3)    {
        CheckPin(pin3, CP_IGNORE_INUSE);
        ExtCfg(pin3, value3, option);
    }


	if(value == EXT_INT_HI || value == EXT_INT_LO || value == EXT_INT_BOTH) {
		// we need to set up a software interrupt
		if(argc < 5) error("Argument count");
        for(i = 0; i < NBRINTERRUPTS; i++) if(inttbl[i].pin == 0) break;
        if(i >= NBRINTERRUPTS) error("Too many interrupts");
        inttbl[i].pin = pin;
		inttbl[i].intp = (char *)GetIntAddress(argv[4]);					// get the interrupt routine's location
		inttbl[i].last = ExtInp(pin);								// save the current pin value for the first test
        switch(value) {                                             // and set trigger polarity
            case EXT_INT_HI:    inttbl[i].lohi = T_LOHI; break;
            case EXT_INT_LO:    inttbl[i].lohi = T_HILO; break;
            case EXT_INT_BOTH:  inttbl[i].lohi = T_BOTH; break;
        }
		InterruptUsed = true;
	}
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
bool __no_inline_not_in_flash_func(bb_get_bootsel_button)() {
    const uint CS_PIN_INDEX = 1;
    disable_interrupts_pico();
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 100; ++i);
    bool button_state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    enable_interrupts_pico();

    return button_state;
}
/*  @endcond */

void fun_pin(void) {
	char code;
    int pin, i, j, b[ANA_AVERAGE];
    MMFLOAT t;
	if(checkstring(ep, (unsigned char *)"TEMP")){
        if(ADCDualBuffering || dmarunning)error("ADC in use");
        adc_init();
        adc_set_temp_sensor_enabled(true);
#ifdef rp2350
        adc_select_input((rp2350a ? 4 : 8));
#else
        adc_select_input(4);
#endif
        last_adc=4;
        t=(MMFLOAT)adc_read()/4095.0*VCC;
        fret=(27.0-(t-0.706)/0.001721);
        targ=T_NBR;
        return;
    }
    if(checkstring(ep, (unsigned char *)"BOOTSEL")){
        iret=bb_get_bootsel_button();
        targ=T_INT;
        return;
    }

	if(!(code=codecheck(ep)))ep+=2;
	pin = getinteger(ep);
	if(!code)pin=codemap(pin);
    if(IsInvalidPin(pin)) error("Invalid pin");
    switch(ExtCurrentConfig[pin]) {
        case EXT_DIG_IN:
        case EXT_CNT_IN:
        case EXT_INT_HI:
        case EXT_INT_LO:
        case EXT_INT_BOTH:
        case EXT_DIG_OUT:
        case EXT_PIO0_OUT:
        case EXT_PIO1_OUT:
#ifdef rp2350
        case EXT_PIO2_OUT:
#endif
                            iret = ExtInp(pin);
                            targ = T_INT;
                            return;
#ifdef rp2350
        case EXT_FAST_TIMER:
                            fret = (MMFLOAT)INT5Value  * (MMFLOAT)1000.0 / (MMFLOAT)INT5InitTimer;
                            targ = T_NBR;
                            return;
#endif
        case EXT_PER_IN:	// if period measurement get the count and average it over the number of cycles
                            if(pin == Option.INT1pin) fret = (MMFLOAT)ExtInp(pin) / (MMFLOAT)INT1InitTimer;
                            else if(pin == Option.INT2pin)  fret = (MMFLOAT)ExtInp(pin) / (MMFLOAT)INT2InitTimer;
                            else if(pin == Option.INT3pin)  fret = (MMFLOAT)ExtInp(pin) / (MMFLOAT)INT3InitTimer;
                            else if(pin == Option.INT4pin)  fret = (MMFLOAT)ExtInp(pin) / (MMFLOAT)INT4InitTimer;
                            targ = T_NBR;
                            return;
        case EXT_FREQ_IN:	// if frequency measurement get the count and scale the reading
                            if(pin == Option.INT1pin) fret = (MMFLOAT)(ExtInp(pin)) * (MMFLOAT)1000.0 / (MMFLOAT)INT1InitTimer;
                            else if(pin == Option.INT2pin)  fret = (MMFLOAT)(ExtInp(pin)) * (MMFLOAT)1000.0 / (MMFLOAT)INT2InitTimer;
                            else if(pin == Option.INT3pin)  fret = (MMFLOAT)(ExtInp(pin)) * (MMFLOAT)1000.0 / (MMFLOAT)INT3InitTimer;
                            else if(pin == Option.INT4pin)  fret = (MMFLOAT)(ExtInp(pin)) * (MMFLOAT)1000.0 / (MMFLOAT)INT4InitTimer;
                            targ = T_NBR;
                            return;
        case EXT_ADCRAW:
                            if(ADCDualBuffering || dmarunning)error("ADC in use");
                            iret=ExtInp(pin); 
                            targ=T_INT;
                            return;  
        case EXT_ANA_IN:    
                            if(ADCDualBuffering || dmarunning)error("ADC in use");
                            for(i = 0; i < ANA_AVERAGE; i++) {
                                b[i] = ExtInp(pin);                 // get the value
                                for(j = i; j > 0; j--) {            // and sort into position
                                    if(b[j - 1] < b[j]) {
                                        t = b[j - 1];
                                        b[j - 1] = b[j];
                                        b[j] = t;
                                    }
                                    else
                                        break;
                                }
                            }
                            // we then discard the top ANA_DISCARD samples and the bottom ANA_DISCARD samples and add up the remainder
                            for(j = 0, i = ANA_DISCARD; i < ANA_AVERAGE - ANA_DISCARD; i++) j += b[i];

                            // the total is averaged and scaled
                            fret = FMul((MMFLOAT)j , VCC) / (MMFLOAT)(4095 * (ANA_AVERAGE - ANA_DISCARD*2));
                            targ = T_NBR;
                            return;

        default:            error("Pin %/| is not an input",pin,pin);
    }
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

int CheckPin(int pin, int action) {

#ifdef rp2350
    if(rp2350a && pin>44)error("Pin | is invalid", pin);
#endif

    if(pin < 1 || pin > NBRPINS || (PinDef[pin].mode & UNUSED)) {
        if(!(action & CP_NOABORT)) error("Pin %/| is invalid", pin,pin);
        return false;
    }

    if(!(action & CP_IGNORE_INUSE) && ExtCurrentConfig[pin] >= EXT_DS18B20_RESERVED && ExtCurrentConfig[pin] < EXT_COM_RESERVED) {
        if(!(action & CP_NOABORT)) error("Pin %/| is in use", pin,pin);
        return false;
    }

    if(!(action & CP_IGNORE_BOOTRES) && ExtCurrentConfig[pin] >= EXT_BOOT_RESERVED) {
        if(!(action & CP_NOABORT)) {
            error("Pin %/| is reserved on startup", pin,pin);
            uSec(1000000);
        }
        return false;
    }

    if(!(action & CP_IGNORE_RESERVED) && ExtCurrentConfig[pin] >= EXT_DS18B20_RESERVED) {
        if(!(action & CP_NOABORT)) error("Pin %/| is in use", pin,pin);
        return false;
    }

    return true;
}
/*  @endcond */
// this is invoked as a command (ie, port(3, 8) = Value)
// first get the arguments then step over the closing bracket.  Search through the rest of the command line looking
// for the equals sign and step over it, evaluate the rest of the command and set the pins accordingly
void cmd_port(void) {
	int pin, nbr, value, code, pincode;
    int i;
	getargs(&cmdline, NBRPINS * 4, (unsigned char *)",");

	if((argc & 0b11) != 0b11) error("Invalid syntax");
    if(!strchr((char *)cmdline,')'))error ("Syntax");
    // step over the equals sign and get the value for the assignment
	while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
	if(!*cmdline) error("Invalid syntax");
	++cmdline;
	if(!*cmdline) error("Invalid syntax");
	value = getinteger(cmdline);
    uint64_t mask=0,setmask=0, readmask;

    for(i = 0; i < argc; i += 4) {
    	code=0;
    	if(!(code=codecheck(argv[i])))argv[i]+=2;
    	pincode = getinteger(argv[i]);
        nbr = getinteger(argv[i + 2]);
        if(nbr < 0 || (pincode == 0 && code!=0) || (pincode<0)) error("Invalid argument");

        while(nbr) {
        	if(!code)pin=codemap(pincode);
        	else pin=pincode;
            if(IsInvalidPin(pin) || !(ExtCurrentConfig[pin] == EXT_DIG_OUT )) error("Invalid output pin");
            mask |=(1<<PinDef[pin].GPno);
            if(value & 1)setmask |= (1<<PinDef[pin].GPno);
            value >>= 1;
            nbr--;
            pincode++;
        }
    } 
    readmask=gpio_get_out_level_all64();
    readmask &=mask;
    gpio_xor_mask(setmask ^ readmask);
}


void fun_distance(void) {
    int trig, echo,techo;

	getargs(&ep, 3, (unsigned char *)",");
	if((argc &1) != 1) error("Invalid syntax");
	char code;
	if(!(code=codecheck(argv[0])))argv[0]+=2;
	trig = getinteger(argv[0]);
	if(!code)trig=codemap(trig);
    if(argc == 3){
    	if(!(code=codecheck(argv[2])))argv[2]+=2;
    	echo = getinteger(argv[2]);
    	if(!code)echo=codemap(echo);
    }
    else
        echo = trig;                                                // they are the same if it is a 3-pin device
    if(IsInvalidPin(trig) || IsInvalidPin(echo)) error("Invalid pin |",echo);
    if(ExtCurrentConfig[trig] >= EXT_COM_RESERVED || ExtCurrentConfig[echo] >= EXT_COM_RESERVED)  error("Pin %/| is in use",trig,trig);
    ExtCfg(echo, EXT_DIG_IN, CNPUSET);                              // setup the echo input
    PinSetBit(trig, LATCLR);                                        // trigger output must start low
    ExtCfg(trig, EXT_DIG_OUT, 0);                                   // setup the trigger output
    PinSetBit(trig, LATSET); uSec(20); PinSetBit(trig, LATCLR);     // pulse the trigger
    uSec(50);
    ExtCfg(echo, EXT_DIG_IN, CNPUSET);                              // this is in case the sensor is a 3-pin type
    uSec(50);
    PauseTimer = 0;                                                 // this is our timeout
    while(PinRead(echo)) if(PauseTimer > 50) { fret = -2; return; } // wait for the acknowledgement pulse start
    while(!PinRead(echo)) if(PauseTimer > 100) { fret = -2; return;}// then its end
    PauseTimer = 0;
    writeusclock(0);
    while(PinRead(echo)) {                                          // now wait for the echo pulse
        if(PauseTimer > 38) {                                       // timeout is 38mS
            fret = -1;
            return;
        }
    }
    techo=readusclock();
    // we have the echo, convert the time to centimeters
    fret = FDiv((MMFLOAT)techo,58.0);  //200 ticks per us, 58 us per cm
    targ = T_NBR;
}



// this is invoked as a function (ie, x = port(10,8) )
void fun_port(void) {
	int pin, nbr, i, value = 0, code, pincode;

	getargs(&ep, NBRPINS * 4, (unsigned char *)",");
	if((argc & 0b11) != 0b11) error("Invalid syntax");
    uint64_t pinstate=gpio_get_all64();
    uint64_t outpinstate=gpio_get_out_level_all64();
    for(i = argc - 3; i >= 0; i -= 4) {
    	code=0;
    	if(!(code=codecheck(argv[i])))argv[i]+=2;
        pincode = getinteger(argv[i]);
        nbr = getinteger(argv[i + 2]);
        if(nbr < 0 || (pincode == 0 && code!=0) || (pincode<0)) error("Invalid argument");
        pincode += nbr - 1;                                             // we start by reading the most significant bit

        while(nbr) {
        	if(!code)pin=codemap(pincode);
        	else pin=pincode;
            if(IsInvalidPin(pin) || !(ExtCurrentConfig[pin] == EXT_DIG_IN || ExtCurrentConfig[pin] == EXT_DIG_OUT || ExtCurrentConfig[pin] == EXT_INT_HI || ExtCurrentConfig[pin] == EXT_INT_LO || ExtCurrentConfig[pin] == EXT_INT_BOTH)) error("Invalid input pin");
            value <<= 1;
            if(ExtCurrentConfig[pin] == EXT_DIG_OUT)value |= (outpinstate & (1<<PinDef[pin].GPno)? 1:0);
            else value |= (pinstate & (1<<PinDef[pin].GPno)? 1:0);
            nbr--;
            pincode--;
        }
    }

    iret = value;
    targ = T_INT;
}


void cmd_pulse(void) {
    int pin, i, x, y;
    MMFLOAT f;

	getargs(&cmdline, 3, (unsigned char *)",");
	if(argc != 3) error("Invalid syntax");
	char code;
	if(!(code=codecheck(argv[0])))argv[0]+=2;
	pin = getinteger(argv[0]);
	if(!code)pin=codemap(pin);
	if(!(ExtCurrentConfig[pin] == EXT_DIG_OUT)) error("Pin %/| is not an output", pin,pin);

    f = getnumber(argv[2]);                                         // get the pulse width
    if(f < 0) error("Number out of bounds");
    x = f;                                                          // get the integer portion (in mSec)
    y = (int)(FSub(f , (MMFLOAT)x) * 1000.0);                             // get the fractional portion (in uSec)

    for(i = 0; i < NBR_PULSE_SLOTS; i++)                            // search looking to see if the pin is in use
        if(PulseCnt[i] != 0 && PulsePin[i] == pin) {
            mT4IntEnable(0);       									// disable the timer interrupt to prevent any conflicts while updating
            PulseCnt[i] = x;                                        // and if the pin is in use, set its time to the new setting or reset if the user wants to terminate
            mT4IntEnable(1);
            if(x == 0) PinSetBit(PulsePin[i], LATINV);
            return;
        }

    if(x == 0 && y == 0) return;                                    // silently ignore a zero pulse width

    if(x < 3) {                                                     // if this is under 3 milliseconds just do it now
        PinSetBit(pin, LATINV);                    // starting edge of the pulse
        uSec(x * 1000 + y);
        PinSetBit(pin, LATINV);                    // finishing edge
        return;
    }

    for(i = 0; i < NBR_PULSE_SLOTS; i++)
        if(PulseCnt[i] == 0) break;                                 // find a spare slot

    if(i >= NBR_PULSE_SLOTS) error("Too many concurrent PULSE commands");

    PinSetBit(pin, LATINV);                        // starting edge of the pulse
    if(x == 1) uSec(500);                                           // prevent too narrow a pulse if there is just one count
    PulsePin[i] = pin;                                              // save the details
    PulseCnt[i] = x;
    PulseActive = true;
}


void fun_pulsin(void) { //allowas timeouts up to 10 seconds
    int pin, polarity;
    unsigned int t1, t2;

	getargs(&ep, 7, (unsigned char *)",");
	if((argc &1) != 1 || argc < 3) error("Invalid syntax");
	char code;
	if(!(code=codecheck(argv[0])))argv[0]+=2;
	pin = getinteger(argv[0]);
	if(!code)pin=codemap(pin);
    if(IsInvalidPin(pin)) error("Invalid pin");
	if(ExtCurrentConfig[pin] != EXT_DIG_IN) error("Pin %/| is not an input",pin,pin);
    polarity = getinteger(argv[2]);

    t1 = t2 = 100000;                                               // default timeout is 100mS
    if(argc >= 5) t1 = t2 = getint(argv[4], 5, 10000000);
    if(argc == 7) t2 = getint(argv[6], 5, 10000000);
    iret = -1;                                                      // in anticipation of a timeout
    writeusclock(0);
    if(polarity) {
        while(PinRead(pin)) if(readusclock() > t1) return;
        while(!PinRead(pin)) if(readusclock() > t1) return;
        writeusclock(0);
        while(PinRead(pin)) if(readusclock() > t2) return;
    } else {
        while(!PinRead(pin)) if(readusclock() > t1) return;
        while(PinRead(pin)) if(readusclock() > t1) return;
        writeusclock(0);
        while(!PinRead(pin)) if(readusclock() > t2) return;
    }
    t1 = readusclock();
    iret = t1;
    targ = T_INT;
}
/****************************************************************************************************************************
IR routines
*****************************************************************************************************************************/

void cmd_ir(void) {
    unsigned char *p;
    int i, pin, dev, cmd;
    if(checkstring(cmdline, (unsigned char *)"CLOSE")) {
        if(IrState == IR_CLOSED) error("Not Open");
        if(CallBackEnabled==1) gpio_set_irq_enabled_with_callback(PinDef[IRpin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
        else gpio_set_irq_enabled(PinDef[IRpin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        IrInterrupt = NULL;
        CallBackEnabled &= (~1);
        ExtCfg(IRpin, EXT_NOT_CONFIG, 0);
    } else if((p = checkstring(cmdline, (unsigned char *)"SEND"))) {
        getargs(&p, 5, (unsigned char *)",");
        char code;
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        pin = getinteger(argv[0]);
        if(!code)pin=codemap(pin);
        if(IsInvalidPin(pin)) error("Invalid pin");
        dev = getint(argv[2], 0, 0b11111);
        cmd = getint(argv[4], 0, 0b1111111);
        if(ExtCurrentConfig[pin] >= EXT_COM_RESERVED)  error("Pin %/| is in use",pin,pin);
        ExtCfg(pin, EXT_DIG_OUT, 0);
        cmd = (dev << 7) | cmd;
        IRSendSignal(pin, 186);
        for(i = 0; i < 12; i++) {
            uSec(600);
            if(cmd & 1)
                IRSendSignal(pin, 92);
            else
                IRSendSignal(pin, 46);
            cmd >>= 1;
        }
    } else {
        getargs(&cmdline, 5, (unsigned char *)",");
        if(IRpin==99)error("Pin not configured for IR");
        if(IrState != IR_CLOSED) error("Already open");
        if(argc%2 == 0 || argc == 0) error("Invalid syntax");
        IrVarType = 0;
        IrDev = findvar(argv[0], V_FIND);
        if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
        if(g_vartbl[g_VarIndex].type & T_STR)  error("Invalid variable");
        if(g_vartbl[g_VarIndex].type & T_NBR) IrVarType |= 0b01;
        IrCmd = findvar(argv[2], V_FIND);
        if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
        if(g_vartbl[g_VarIndex].type & T_STR)  error("Invalid variable");
        if(g_vartbl[g_VarIndex].type & T_NBR) IrVarType |= 0b10;
        InterruptUsed = true;
        IrInterrupt = GetIntAddress(argv[4]);							// get the interrupt location
        IrInit();
    }
}

/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

void IrInit(void) {
    writeusclock(0);
    if(ExtCurrentConfig[IRpin] >= EXT_COM_RESERVED)  error("Pin %/% is in use",IRpin,IRpin);
    ExtCfg(IRpin, EXT_IR, 0);
    ExtCfg(IRpin, EXT_COM_RESERVED, 0);
    if(!CallBackEnabled){
        gpio_set_irq_enabled_with_callback(PinDef[IRpin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
        CallBackEnabled=1;
    } else {
        gpio_set_irq_enabled(PinDef[IRpin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
        CallBackEnabled|=1;
    }
    IrReset();
}


void IrReset(void) {
	IrState = IR_WAIT_START;
    IrCount = 0;
    writeIRclock(0);
}


// this modulates (at about 38KHz) the IR beam for transmit
// half_cycles is the number of half cycles to send.  ie, 186 is about 2.4mSec
void IRSendSignal(int pin, int half_cycles) {
    while(half_cycles--) {
        PinSetBit(pin, LATINV);
        uSec(13);
    }
}
void MIPS16 set_PWM(int slice, MMFLOAT duty1, MMFLOAT duty2, int high1, int high2, int delaystart){
    if(slice==0 && PWM0Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==0 && PWM0Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
#ifdef rp2350
    if(slice==0 && fast_timer_active)error("Channel 0 in use for fast timer");
#endif
    if(slice==1 && PWM1Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==1 && PWM1Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
    if(slice==2 && PWM2Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==2 && PWM2Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
    if(slice==3 && PWM3Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==3 && PWM3Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
    if(slice==4 && PWM4Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==4 && PWM4Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
    if(slice==5 && PWM5Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==5 && PWM5Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
    if(slice==6 && PWM6Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==6 && PWM6Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
    if(slice==7 && PWM7Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==7 && PWM7Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
#ifdef rp2350
    if(slice==8 && PWM8Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==8 && PWM8Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
    if(slice==9 && PWM9Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==9 && PWM9Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
    if(slice==10 && PWM10Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==10 && PWM10Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
    if(slice==11 && PWM11Apin==99 && duty1>=0.0)error("Pin not set for PWM");
    if(slice==11 && PWM11Bpin==99 && duty2>=0.0)error("Pin not set for PWM");
#endif
    if(slice==0 && PWM0Apin!=99 && duty1>=0.0){
        ExtCfg(PWM0Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==0 && PWM0Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM0Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if(slice==1 && PWM1Apin!=99 && duty1>=0.0){
        ExtCfg(PWM1Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==1 && PWM1Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM1Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if(slice==2 && PWM2Apin!=99 && duty1>=0.0){
        ExtCfg(PWM2Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==2 && PWM2Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM2Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if(slice==3 && PWM3Apin!=99 && duty1>=0.0){
        ExtCfg(PWM3Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==3 && PWM3Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM3Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if(slice==4 && PWM4Apin!=99 && duty1>=0.0){
        ExtCfg(PWM4Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==4 && PWM4Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM4Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if(slice==5 && PWM5Apin!=99 && duty1>=0.0){
        ExtCfg(PWM5Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==5 && PWM5Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM5Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if(slice==6 && PWM6Apin!=99 && duty1>=0.0){
        ExtCfg(PWM6Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==6 && PWM6Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM6Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if(slice==7 && PWM7Apin!=99 && duty1>=0.0){
        ExtCfg(PWM7Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==7 && PWM7Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM7Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
#ifdef rp2350
    if(slice==8 && PWM8Apin!=99 && duty1>=0.0){
        ExtCfg(PWM8Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==8 && PWM8Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM8Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if(slice==9 && PWM9Apin!=99 && duty1>=0.0){
        ExtCfg(PWM9Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==9 && PWM9Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM9Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if(slice==10 && PWM10Apin!=99 && duty1>=0.0){
        ExtCfg(PWM10Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==10 && PWM10Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM10Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if(slice==11 && PWM11Apin!=99 && duty1>=0.0){
        ExtCfg(PWM11Apin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if(slice==11 && PWM11Bpin!=99 && duty2>=0.0){
        ExtCfg(PWM11Bpin,EXT_COM_RESERVED,0);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
#endif
        if(slice==0 && slice0==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice0=1;
    }
        if(slice==1 && slice1==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice1=1;
    }
        if(slice==2 && slice2==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice2=1;
    }
        if(slice==3 && slice3==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice3=1;
    }
        if(slice==4 && slice4==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice4=1;
    }
        if(slice==5 && slice5==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice5=1;
    }
        if(slice==6 && slice6==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice6=1;
    }
        if(slice==7 && slice7==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice7=1;
    }
#ifdef rp2350
        if(slice==8 && slice8==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice8=1;
    }
        if(slice==9 && slice9==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice9=1;
    }
        if(slice==10 && slice10==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice10=1;
    }
        if(slice==11 && slice11==0){
        if(!delaystart)pwm_set_enabled(slice, true);
        slice11=1;
    }
#endif
}

void PWMoff(int slice){
    if(slice==0 && PWM0Apin!=99 && ExtCurrentConfig[PWM0Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM0Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==0 && PWM0Bpin!=99 && ExtCurrentConfig[PWM0Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM0Bpin,EXT_NOT_CONFIG,0);
    }
    if(slice==1 && PWM1Apin!=99 && ExtCurrentConfig[PWM1Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM1Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==1 && PWM1Bpin!=99 && ExtCurrentConfig[PWM1Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM1Bpin,EXT_NOT_CONFIG,0);
    }
    if(slice==2 && PWM2Apin!=99 && ExtCurrentConfig[PWM2Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM2Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==2 && PWM2Bpin!=99 && ExtCurrentConfig[PWM2Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM2Bpin,EXT_NOT_CONFIG,0);
    }
    if(slice==3 && PWM3Apin!=99 && ExtCurrentConfig[PWM3Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM3Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==3 && PWM3Bpin!=99 && ExtCurrentConfig[PWM3Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM3Bpin,EXT_NOT_CONFIG,0);
    }
    if(slice==4 && PWM4Apin!=99 && ExtCurrentConfig[PWM4Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM4Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==4 && PWM4Bpin!=99 && ExtCurrentConfig[PWM4Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM4Bpin,EXT_NOT_CONFIG,0);
    }
    if(slice==5 && PWM5Apin!=99 && ExtCurrentConfig[PWM5Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM5Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==5 && PWM5Bpin!=99 && ExtCurrentConfig[PWM5Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM5Bpin,EXT_NOT_CONFIG,0);
    }
    if(slice==6 && PWM6Apin!=99 && ExtCurrentConfig[PWM6Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM6Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==6 && PWM6Bpin!=99 && ExtCurrentConfig[PWM6Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM6Bpin,EXT_NOT_CONFIG,0);
    }
    if(slice==7 && PWM7Apin!=99 && ExtCurrentConfig[PWM7Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM7Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==7 && PWM7Bpin!=99 && ExtCurrentConfig[PWM7Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM7Bpin,EXT_NOT_CONFIG,0);
    }
#ifdef rp2350
    if(slice==8 && PWM8Apin!=99 && ExtCurrentConfig[PWM8Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM8Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==8 && PWM8Bpin!=99 && ExtCurrentConfig[PWM8Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM8Bpin,EXT_NOT_CONFIG,0);
    }
    if(slice==9 && PWM9Apin!=99 && ExtCurrentConfig[PWM9Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM9Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==9 && PWM9Bpin!=99 && ExtCurrentConfig[PWM9Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM9Bpin,EXT_NOT_CONFIG,0);
    }
    if(slice==10 && PWM10Apin!=99 && ExtCurrentConfig[PWM10Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM10Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==10 && PWM10Bpin!=99 && ExtCurrentConfig[PWM10Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM10Bpin,EXT_NOT_CONFIG,0);
    }
    if(slice==11 && PWM11Apin!=99 && ExtCurrentConfig[PWM11Apin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM11Apin,EXT_NOT_CONFIG,0);
    }
    if(slice==11 && PWM11Bpin!=99 && ExtCurrentConfig[PWM11Bpin] < EXT_BOOT_RESERVED){
        ExtCfg(PWM11Bpin,EXT_NOT_CONFIG,0);
    }
#endif
    pwm_set_enabled(slice, false);
}
#ifndef PICOMITEVGA
void setBacklight(int level, int setfrequency){
#if defined(PICOMITE) && defined(rp2350)
    if(((Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<BufferedPanel ) || Option .DISPLAY_TYPE>=NEXTGEN || (Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL)) && Option.DISPLAY_BL){
#else
    if(((Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<BufferedPanel ) || (Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL)) && Option.DISPLAY_BL){
#endif
        MMFLOAT frequency=setfrequency ? (MMFLOAT)setfrequency : (Option.DISPLAY_TYPE==ILI9488W ? 1000.0 : 50000.0);
        int wrap=(Option.CPU_Speed*1000)/frequency;
        int high=(int)((MMFLOAT)Option.CPU_Speed/frequency*level*10.0);
        int div=1;
        while(wrap>65535){
            wrap>>=1;
            if(level>=0.0)high>>=1;
            div<<=1;
        }
        wrap--;
        if(div!=1)pwm_set_clkdiv(BacklightSlice,(float)div);
        pwm_set_wrap(BacklightSlice, wrap);
        pwm_set_chan_level(BacklightSlice, BacklightChannel, high);
    } else if(Option.DISPLAY_TYPE<=I2C_PANEL){
        level*=255;
        level/=100;
        I2C_Send_Command(0x81);//SETCONTRAST
        I2C_Send_Command((uint8_t)level);
    } else if(Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL){
        SetBacklightSSD1963(level);
    } else if(Option.DISPLAY_TYPE==SSD1306SPI){
        level*=255;
        level/=100;
        spi_write_command(0x81);//SETCONTRAST
        spi_write_command((uint8_t)level);
    } 
}
/*  @endcond */
void MIPS16 cmd_backlight(void){
    getargs(&cmdline,3,(unsigned char *)",");
    int level=getint(argv[0],0,100);
    int frequency=50000;
#if defined(PICOMITE) && defined(rp2350)
    if(((Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<BufferedPanel ) || (Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL) || Option .DISPLAY_TYPE>=NEXTGEN) && Option.DISPLAY_BL){
#else
    if(((Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<BufferedPanel ) || (Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL)) && Option.DISPLAY_BL){
#endif
    } else if(Option.DISPLAY_TYPE<=I2C_PANEL){
    } else if(Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL){
    } else if(Option.DISPLAY_TYPE==SSD1306SPI){
    } else error("Backlight not set up");
    if(argc==3){
        if(checkstring(argv[2],(unsigned char *)"DEFAULT")){
            Option.BackLightLevel=level;
            SaveOptions();
        } else {
            frequency=getint(argv[2],100,100000);
        }
    }
    setBacklight(level, frequency);
}
#endif
void MIPS16 cmd_Servo(void){
    unsigned char *tp;
    int div=1, high1=0, high2=0;
    MMFLOAT duty1=-1.0, duty2=-1.0;
    getargs(&cmdline,5,(unsigned char *)",");
    if(!(argc>=3))error("Syntax");
    int CPU_Speed=Option.CPU_Speed;
#ifdef rp2350
    int slice=getint(argv[0],0,rp2350a ? 7:11);
    if(slice==0 && ExtCurrentConfig[FAST_TIMER_PIN]==EXT_FAST_TIMER)error("Channel in use for fast frequency");
#else
    int slice=getint(argv[0],0,7);
#endif
    if(slice==BacklightSlice)error("Channel in use for backlight");
    if(slice==Option.AUDIO_SLICE)error("Channel in use for Audio");
    if(slice==CameraSlice)error("Channel in use for Camera");
    if((tp=checkstring(argv[2],(unsigned char *)"OFF"))){
        PWMoff(slice);
        if(slice==0)slice0=0;
        if(slice==1)slice1=0;
        if(slice==2)slice2=0;
        if(slice==3)slice3=0;
        if(slice==4)slice4=0;
        if(slice==5)slice5=0;
        if(slice==6)slice6=0;
        if(slice==7)slice7=0;
#ifdef rp2350
        if(slice==8)slice8=0;
        if(slice==9)slice9=0;
        if(slice==10)slice10=0;
        if(slice==11)slice11=0;
#endif
        return;
    }
    MMFLOAT frequency=50.0;
    if(*argv[2]){
        duty1=getnumber(argv[2]);
        if(duty1>120.0 || duty1<-20.0)error("Syntax");
        duty1=5.0+duty1*0.05;
    }
    if(argc>=5 && *argv[4]){
        duty2=getnumber(argv[4]);
        if(duty2>120.0 || duty2<-20.0)error("Syntax");
        duty2=5.0+duty2*0.05;
    }
    int wrap=(CPU_Speed*1000)/frequency;
    if(duty1>=0.0)high1=(int)((MMFLOAT)CPU_Speed/frequency*duty1*10.0);
    if(duty2>=0.0)high2=(int)((MMFLOAT)CPU_Speed/frequency*duty2*10.0);
    while(wrap>65535){
        wrap>>=1;
        if(duty1>=0.0)high1>>=1;
        if(duty2>=0.0)high2>>=1;
        div<<=1;
    }
    if(div>256)error("Invalid frequency");
    wrap--;
    if(high1)high1--;
    if(high2)high2--;
    pwm_set_clkdiv(slice,(float)div);
    pwm_set_wrap(slice, wrap);
    pwm_set_output_polarity(slice,false,false);
    pwm_set_phase_correct(slice,false);
    set_PWM(slice,duty1,duty2,high1,high2, 0);
}
void MIPS16 cmd_pwm(void){
    unsigned char *tp;
    if((tp=checkstring(cmdline, (unsigned char *)"SYNC"))) {
        MMFLOAT count0=-1.0,count1=-1.0,count2=-1.0,count3=-1.0,count4=-1.0,count5=-1.0,count6=-1.0,count7=-1.0;
#ifdef rp2350
        MMFLOAT count8=-1.0,count9=-1.0,count10=-1.0,count11=-1.0;
        getargs(&tp,23,(unsigned char *)",");
#else
        getargs(&tp,15,(unsigned char *)",");
#endif
        if(argc>=1){count0=getnumber(argv[0]);
        if((count0<0.0 || count0>100.0) && count0!=-1.0)error("Syntax");}
        if(argc>=3 && *argv[2]){count1=getnumber(argv[2]);
        if((count1<0.0 || count1>100.0) && count1!=-1.0)error("Syntax");}
        if(argc>=5 && *argv[4]){count2=getnumber(argv[4]);
        if((count2<0.0 || count2>100.0) && count2!=-1.0)error("Syntax");}
        if(argc>=7 && *argv[6]){count3=getnumber(argv[6]);
        if((count3<0.0 || count3>100.0) && count3!=-1.0)error("Syntax");}
        if(argc>=9 && *argv[8]){count4=getnumber(argv[8]);
        if((count4<0.0 || count4>100.0) && count4!=-1.0)error("Syntax");}
        if(argc>=11 && *argv[10]){count5=getnumber(argv[10]);
        if((count5<0.0 || count5>100.0) && count5!=-1.0)error("Syntax");}
        if(argc>=13 && *argv[12]){count6=getnumber(argv[12]);
        if((count6<0.0 || count6>100.0) && count6!=-1.0)error("Syntax");}
#ifdef rp2350
        if(argc>=15 && *argv[14]){count7=getnumber(argv[14]);
        if((count7<0.0 || count7>100.0) && count7!=-1.0)error("Syntax");}
        if(argc>=17 && *argv[16]){count8=getnumber(argv[16]);
        if((count8<0.0 || count8>100.0) && count8!=-1.0)error("Syntax");}
        if(argc>=19 && *argv[18]){count9=getnumber(argv[18]);
        if((count9<0.0 || count9>100.0) && count9!=-1.0)error("Syntax");}
        if(argc>=21 && *argv[20]){count10=getnumber(argv[20]);
        if((count10<0.0 || count10>100.0) && count10!=-1.0)error("Syntax");}
        if(argc==23 && *argv[22]){count11=getnumber(argv[22]);
        if((count11<0.0 || count11>100.0) && count11!=-1.0)error("Syntax");}
#else
        if(argc==15 && *argv[14]){count7=getnumber(argv[14]);
        if((count7<0.0 || count7>100.0) && count7!=-1.0)error("Syntax");}
#endif
        
        int enabled=0;
        if(slice0 || Option.AUDIO_SLICE ==0 || BacklightSlice==0 || CameraSlice==0
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==0
#endif
        ){
            enabled |=1;
            if(!(Option.AUDIO_SLICE ==0 || BacklightSlice==0 || CameraSlice==0 || count0<0.0
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==0
#endif
        )){
                pwm_set_enabled(0,false);
                count0=(MMFLOAT)pwm_hw->slice[0].top * (100.0-count0) / 100.0;
                pwm_set_counter(0,(int)count0);
            }
        }
        if(slice1 || Option.AUDIO_SLICE ==1 || BacklightSlice==1 || CameraSlice==1
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==1
#endif
        ){
            enabled |=2;
            if(!(Option.AUDIO_SLICE ==1 || BacklightSlice==1 || CameraSlice==1 || count1<0.0
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==1
#endif
        )){
                pwm_set_enabled(1,false);
                count1=(MMFLOAT)pwm_hw->slice[1].top * (100.0-count1) / 100.0;
                pwm_set_counter(1,count1);
            }
        }
        if(slice2 || Option.AUDIO_SLICE ==2 || BacklightSlice==2 || CameraSlice==2
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==2
#endif
        ){
            enabled |=4;
            if(!(Option.AUDIO_SLICE ==2 || BacklightSlice==2 || CameraSlice==2 || count2<0.0
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==2
#endif
        )){
                pwm_set_enabled(2,false);
                count2=(MMFLOAT)pwm_hw->slice[2].top * (100.0-count2) / 100.0;
                pwm_set_counter(2,count2);
            }
        }
        if(slice3 || Option.AUDIO_SLICE ==3 || BacklightSlice==3 || CameraSlice==3
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==3
#endif
        ){
            enabled |=8;
            if(!(Option.AUDIO_SLICE ==3 || BacklightSlice==3 || CameraSlice==3 || count3<0.0
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==3
#endif
        )){
                pwm_set_enabled(3,false);
                count3=(MMFLOAT)pwm_hw->slice[3].top * (100.0-count3) / 100.0;
                pwm_set_counter(3,count3);
            }
        }
        if(slice4 || Option.AUDIO_SLICE ==4 || BacklightSlice==4 || CameraSlice==4
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==4
#endif
        ){
            enabled |=16;
            if(!(Option.AUDIO_SLICE ==4 || BacklightSlice==4 || CameraSlice==4 || count4<0.0
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==4
#endif
        )){
                pwm_set_enabled(4,false);
                count4=(MMFLOAT)pwm_hw->slice[4].top * (100.0-count4) / 100.0;
                pwm_set_counter(4,count4);
            }
        }
        if(slice5 || Option.AUDIO_SLICE ==5 || BacklightSlice==5 || CameraSlice==5
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==5
#endif
        ){
            enabled |=32;
            if(!(Option.AUDIO_SLICE ==5 || BacklightSlice==5 || CameraSlice==5 || count5<0.0
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==5
#endif
        )){
                pwm_set_enabled(5,false);
                count5=(MMFLOAT)pwm_hw->slice[5].top * (100.0-count5) / 100.0;
                pwm_set_counter(5,count5);
            }
        }
        if(slice6 || Option.AUDIO_SLICE ==6 || BacklightSlice==6 || CameraSlice==6
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==6
#endif
        ){
            enabled |=64;
            if(!(Option.AUDIO_SLICE ==6 || BacklightSlice==6 || CameraSlice==6 || count6<0.0
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==6
#endif
        )){
                pwm_set_enabled(6,false);
                count6=(MMFLOAT)pwm_hw->slice[6].top * (100.0-count6) / 100.0;
                pwm_set_counter(6,count6);
            }
        }
        if(slice7 || Option.AUDIO_SLICE ==7 || BacklightSlice==7 || CameraSlice==7
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==7
#endif
        ){
            enabled |=128;
            if(!(Option.AUDIO_SLICE ==7 || BacklightSlice==7 || CameraSlice==7 || count7<0.0
#if defined(PICOMITE) && defined(rp2350)
            || KeyboardlightSlice==7
#endif
            )){
                pwm_set_enabled(7,false);
                count7=(MMFLOAT)pwm_hw->slice[7].top * (100.0-count7) / 100.0;
                pwm_set_counter(7,count7);
            }
        }
#ifdef rp2350
#ifdef PICOMITE
        if(slice8 || Option.AUDIO_SLICE ==8 || BacklightSlice==8 || CameraSlice==8 || KeyboardlightSlice==8){
#else
        if(slice8 || Option.AUDIO_SLICE ==8 || BacklightSlice==8 || CameraSlice==8){
#endif
            enabled |=256;
#ifdef PICOMITE
            if(!(Option.AUDIO_SLICE ==8 || BacklightSlice==8 || CameraSlice==8 || KeyboardlightSlice==8 || count8<0.0)){
#else
            if(!(Option.AUDIO_SLICE ==8 || BacklightSlice==8 || CameraSlice==8 || count8<0.0)){
#endif
                pwm_set_enabled(8,false);
                count8=(MMFLOAT)pwm_hw->slice[8].top * (100.0-count8) / 100.0;
                pwm_set_counter(8,count8);
            }
        }
#ifdef PICOMITE
        if(slice9 || Option.AUDIO_SLICE ==9 || BacklightSlice==9 || CameraSlice==9 || KeyboardlightSlice==9){
#else
        if(slice9 || Option.AUDIO_SLICE ==9 || BacklightSlice==9 || CameraSlice==9){
#endif
            enabled |=512;
#ifdef PICOMITE
            if(!(Option.AUDIO_SLICE ==9 || BacklightSlice==9 || CameraSlice==9 || KeyboardlightSlice==9 || count9<0.0)){
#else
            if(!(Option.AUDIO_SLICE ==9 || BacklightSlice==9 || CameraSlice==9 || count9<0.0)){
#endif
                pwm_set_enabled(9,false);
                count9=(MMFLOAT)pwm_hw->slice[9].top * (100.0-count9) / 100.0;
                pwm_set_counter(9,count9);
            }
        }
#ifdef PICOMITE
        if(slice10 || Option.AUDIO_SLICE ==10 || BacklightSlice==10 || CameraSlice==10 || KeyboardlightSlice==10){
#else
        if(slice10 || Option.AUDIO_SLICE ==10 || BacklightSlice==10 || CameraSlice==10){
#endif
            enabled |=1024;
#ifdef PICOMITE
            if(!(Option.AUDIO_SLICE ==10 || BacklightSlice==10 || CameraSlice==10 || KeyboardlightSlice==10 || count10<0.0)){
#else
            if(!(Option.AUDIO_SLICE ==10 || BacklightSlice==10 || CameraSlice==10 || count10<0.0)){
#endif
                pwm_set_enabled(10,false);
                count10=(MMFLOAT)pwm_hw->slice[10].top * (100.0-count10) / 100.0;
                pwm_set_counter(10,count10);
            }
        }
#ifdef PICOMITE
        if(slice11 || Option.AUDIO_SLICE ==11 || BacklightSlice==11 || CameraSlice==11 || KeyboardlightSlice==11){
#else
        if(slice11 || Option.AUDIO_SLICE ==11 || BacklightSlice==11 || CameraSlice==11){
#endif
            enabled |=2048;
#ifdef PICOMITE
            if(!(Option.AUDIO_SLICE ==11 || BacklightSlice==11 || CameraSlice==11  || KeyboardlightSlice==11 || count11<0.0)){
#else
            if(!(Option.AUDIO_SLICE ==11 || BacklightSlice==11 || CameraSlice==11 || count11<0.0)){
#endif
                pwm_set_enabled(11,false);
                count11=(MMFLOAT)pwm_hw->slice[11].top * (100.0-count11) / 100.0;
                pwm_set_counter(11,count11);
            }
        }
#endif
        pwm_hw->en=enabled;
        return;
    }
    int div=1, high1=0, high2=0;
    int phase1=0,phase2=0;
    MMFLOAT duty1=-1.0, duty2=-1.0;
    getargs(&cmdline,11,(unsigned char *)",");
    if(!(argc>=3))error("Syntax");
    int CPU_Speed=Option.CPU_Speed;
#ifdef rp2350
    int slice=getint(argv[0],0,rp2350a ? 7:11);
    if(slice==0 && ExtCurrentConfig[FAST_TIMER_PIN]==EXT_FAST_TIMER)error("Channel in use for fast frequency");
#else
    int slice=getint(argv[0],0,7);
#endif
    if(slice==BacklightSlice)error("Channel in use for backlight");
    if(slice==Option.AUDIO_SLICE)error("Channel in use for Audio");
    if(slice==CameraSlice)error("Channel in use for Camera");
    if((tp=checkstring(argv[2],(unsigned char *)"OFF"))){
        PWMoff(slice);
        if(slice==0)slice0=0;
        if(slice==1)slice1=0;
        if(slice==2)slice2=0;
        if(slice==3)slice3=0;
        if(slice==4)slice4=0;
        if(slice==5)slice5=0;
        if(slice==6)slice6=0;
        if(slice==7)slice7=0;
#ifdef rp2350
        if(slice==8)slice8=0;
        if(slice==9)slice9=0;
        if(slice==10)slice10=0;
        if(slice==11)slice11=0;
#endif
        return;
    }
    if(!(argc>=5))error("Syntax");
    int delaystart=0;
    int phase=1;
    if(argc>=9 && *argv[8])phase+=getint(argv[8],0,1);
    if(argc==11)delaystart=getint(argv[10],0,1);
    MMFLOAT frequency=getnumber(argv[2])*phase;
    if(frequency>(MMFLOAT)(CPU_Speed>>2)*1000.0)error("Invalid frequency");
    if(*argv[4]){
        duty1=getnumber(argv[4]);
        if(duty1>100.0 || duty1<-100.0)error("Syntax");
        if(duty1<0){
            duty1=-duty1;
            phase1=1;
        }
    }
    if(argc>=7 && *argv[6]){
        duty2=getnumber(argv[6]);
        if(duty2>100.0 || duty2<-100.0)error("Syntax");
        if(duty2<0){
            duty2=-duty2;
            phase2=1;
        }
    }
    int wrap=(CPU_Speed*1000)/frequency;
    if(duty1>=0.0)high1=(int)((MMFLOAT)CPU_Speed/frequency*duty1*10.0);
    if(duty2>=0.0)high2=(int)((MMFLOAT)CPU_Speed/frequency*duty2*10.0);
    while(wrap>65535){
        wrap>>=1;
        if(duty1>=0.0)high1>>=1;
        if(duty2>=0.0)high2>>=1;
        div<<=1;
    }
    if(div>256)error("Invalid frequency");
    wrap--;
    if(high1)high1--;
    if(high2)high2--;
    pwm_set_clkdiv(slice,(float)div);
    pwm_set_wrap(slice, wrap);
    pwm_set_output_polarity(slice,phase1,phase2);
    pwm_set_phase_correct(slice,(phase==2? true : false));
    set_PWM(slice,duty1,duty2,high1,high2, delaystart);
}


/****************************************************************************************************************************
 The KEYPAD command
*****************************************************************************************************************************/
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
#ifdef rp2350
static unsigned char keypad_pins[64]={0};
int keypadcols=0;
int keypadrows=0;
MMFLOAT *PadLookup=NULL;
MMFLOAT *KeypadVar;
const MMFLOAT PadLookupDefault[16] = { 1.0, 2.0, 3.0, 20.0, 4.0, 5.0, 6.0, 21.0, 7.0, 8.0, 9.0, 22.0, 10.0, 0.0, 11.0, 23.0 };
#else
static char keypad_pins[8]={0};
MMFLOAT *KeypadVar;
#define keypadcols 4
#define keypadrows 4
#endif
unsigned char *KeypadInterrupt = NULL;
void KeypadClose(void);
/*  @endcond */

void cmd_keypad(void) {
    int i, j, code;

    if(checkstring(cmdline, (unsigned char *)"CLOSE"))
        KeypadClose();
    else {
        getargs(&cmdline, 19, (unsigned char *)",");
#ifdef rp2350
        if(argc==13){ // new format map%(c,r),variable,interrupt, startcolpin, nocols, startrowpin, norows
            MMFLOAT *a1float=NULL;
            #ifdef rp2350
            int dims[MAXDIM]={0};
            #else
            short dims[MAXDIM]={0};
            #endif
            KeypadInterrupt = GetIntAddress(argv[4]);					// get the interrupt location
            keypadrows=getint(argv[8],1,31);
            keypadcols=getint(argv[12],1,31);
			parsefloatrarray(argv[0], &a1float, 1, 2, dims, false);
			if(dims[0] - g_OptionBase + 1 !=keypadrows){keypadcols=keypadrows=0;error("Array row count mismatch");}
			if(dims[1] - g_OptionBase + 1 !=keypadcols){keypadcols=keypadrows=0;error("Array column count mismatch");}
            KeypadVar = findvar(argv[2], V_FIND);
            if(g_vartbl[g_VarIndex].type & T_CONST){keypadcols=keypadrows=0;error("Cannot change a constant");}
            if(!(g_vartbl[g_VarIndex].type & T_NBR)){keypadcols=keypadrows=0;error("Integer variable required");}
            code=0;
            if(!(code=codecheck(argv[6])))argv[6]+=2;
            j = getinteger(argv[6]);
            if(!code)j=codemap(j);
            int k=PinDef[j].GPno;
            for(i=0;i<keypadrows;i++){
                j=PINMAP[k+i];
                if(ExtCurrentConfig[j] >= EXT_COM_RESERVED)  error("Pin %/| is in use",j,j);
                ExtCfg(j, EXT_DIG_IN, ODCSET);
                ExtCfg(j, EXT_COM_RESERVED, 0);
                keypad_pins[i] = j;
            }
            code=0;
            if(!(code=codecheck(argv[10])))argv[10]+=2;
            j = getinteger(argv[10]);
            if(!code)j=codemap(j);
            k=PinDef[j].GPno;
            for(i=0;i<keypadcols;i++){
                j=PINMAP[k+i];
                if(ExtCurrentConfig[j] >= EXT_COM_RESERVED)  error("Pin %/| is in use",j,j);
                ExtCfg(j, EXT_DIG_IN, ODCSET);
                ExtCfg(j, EXT_COM_RESERVED, 0);
                keypad_pins[i+keypadrows] = j;
            }
            PadLookup=a1float;
            InterruptUsed = true;
        } else {
            PadLookup=(MMFLOAT *)PadLookupDefault;
            keypadcols=4;
            keypadrows=4;
#endif
            if(argc%2 == 0 || argc < 17) error("Invalid syntax");
            if(KeypadInterrupt != NULL) error("Already open");
            KeypadVar = findvar(argv[0], V_FIND);
            if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
            if(!(g_vartbl[g_VarIndex].type & T_NBR)) error("Floating point variable required");
            InterruptUsed = true;
            KeypadInterrupt = GetIntAddress(argv[2]);					// get the interrupt location
            for(i = 0; i < 8; i++) {
                if(i == 7 && argc < 19) {
                    keypad_pins[i] = 0;
                    break;
                }
                code=0;
                if(!(code=codecheck(argv[(i + 2) * 2])))argv[(i + 2) * 2]+=2;
                j = getinteger(argv[(i + 2) * 2]);
                if(!code)j=codemap(j);
                if(ExtCurrentConfig[j] >= EXT_COM_RESERVED)  error("Pin %/| is in use",j,j);
    //            if(i < 4) {
                ExtCfg(j, EXT_DIG_IN, ODCSET);
                ExtCfg(j, EXT_COM_RESERVED, 0);
                keypad_pins[i] = j;
            }
#ifdef rp2350
        }
#endif
    }
}

/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

void KeypadClose(void) {
    int i;
    if(KeypadInterrupt == NULL) return;
#ifdef rp2350
    keypadcols=0;
    keypadrows=0;
//    PadLookup=NULL;
    for(i = 0; i < 64; i++) {
#else
    for(i = 0; i < 8; i++) {
#endif
        if(keypad_pins[i]) {
            ExtCfg(keypad_pins[i], EXT_NOT_CONFIG, 0);				// all set to unconfigured
        }
    }
    KeypadInterrupt = NULL;
}


int KeypadCheck(void) {
    static unsigned char count = 0, keydown = false;
    int rows, cols;
#ifndef rp2350
    const char PadLookup[16] = { 1, 2, 3, 20, 4, 5, 6, 21, 7, 8, 9, 22, 10, 0, 11, 23 };
#endif
    if(count++ % 64) return false;                                  // only check every 64 loops through the interrupt processor

    for(cols = keypadrows; cols < keypadrows+keypadcols; cols++) {   
       if(keypad_pins[cols]) {                                        // we might just have 3 pull down pins
            PinSetBit(keypad_pins[cols], ODCCLR);                      // pull it low
            for(rows = 0; rows < keypadrows; rows++) {   
                if(PinRead((unsigned char)keypad_pins[rows]) == 0) {                  // if it is low we have found a keypress
                    if(keydown) goto exitcheck;                     // we have already reported this, so just exit
                    uSec(40 * 1000);                                // wait 40mS and check again
                    if(PinRead((unsigned char)keypad_pins[rows]) != 0) goto exitcheck;// must be contact bounce if it is now high
                    *KeypadVar = PadLookup[(rows*keypadcols)+(cols-keypadrows)];     // lookup the key value and set the variable
                    PinSetBit(keypad_pins[cols], ODCSET);
                    keydown = true;                                 // record that we know that the key is down
                    return true;                                    // and tell the interrupt processor that we are good to go
                }
            }
            PinSetBit(keypad_pins[cols], ODCSET);                      // wasn't this pin, clear the pulldown
        }
    }
    keydown = false;                                                // no key down, record the fact
    return false;

exitcheck:
    PinSetBit(keypad_pins[cols], ODCSET);
    return false;
}
#if defined(PICOMITE) && defined(rp2350)
const unsigned char localkeymap[13][6]={
{1,2,3,4,5,6},
{7,8,9,10,11,12},
{13,14,15,16,17,18},
{19,20,21,22,23,24},
{25,26,27,28,29,30},
{31,32,33,34,35,36},
{37,38,39,40,41,42},
{43,44,45,46,47,48},
{49,50,51,52,53,54},
{55,56,57,58,59,60},
{61,62,63,64,65,66},
{67,68,69,70,71,72},
{73,74,75,76,77,78}
};
const unsigned char asciimapl[79]={
    255,
    255,27,11,255,255,255,
    255,255,255,255,255,255,
    255,49,113,97,122,255,
    145,50,119,115,120,255,
    146,51,101,100,99,255,
    147,52,114,102,118,45,
    148,53,116,103,98,61,
    149,54,121,104,110,255,
    150,55,117,106,109,32,
    151,56,105,107,47,255,
    152,57,111,108,128,129,
    8,48,112,59,13,130,
    255,96,92,39,255,131
};
const unsigned char asciimapu[79]={
    255,
    255,27,11,255,255,255,
    255,255,255,255,255,255,
    255,33,81,65,90,255,
    177,64,87,83,88,255,
    178,35,69,68,67,255,
    179,36,82,70,86,95,
    180,37,84,71,66,43,
    181,94,89,72,78,255,
    182,38,85,74,77,32,
    183,42,73,75,63,255,
    184,40,79,76,136,137,
    127,41,80,58,13,134,
    255,126,124,34,255,135
};
const unsigned char asciimapfl[79]={
    255,
    255,27,11,255,255,255,
    255,255,255,255,255,255,
    255,49,113,97,122,255,
    145,50,119,115,120,255,
    146,51,101,100,99,255,
    147,52,114,102,118,45,
    148,53,116,103,98,61,
    149,54,121,104,110,255,
    150,55,117,106,44,32,
    151,153,105,60,46,255,
    152,154,132,62,136,137,
    8,155,91,123,13,134,
    255,156,93,125,255,135
};
const unsigned char asciimapfu[79]={
    255,
    255,27,11,255,255,255,
    255,255,255,255,255,255,
    255,33,81,65,90,255,
    177,64,87,83,88,255,
    178,35,69,68,67,255,
    179,36,82,70,86,95,
    180,37,84,71,66,43,
    181,94,89,72,78,255,
    182,38,85,74,44,32,
    183,185,73,60,46,255,
    184,186,132,62,136,137,
    127,187,91,123,13,134,
    255,188,93,125,255,135
};
bool checkpressedtime(int count){
    if(!count)return false;
    if(count==1)return true;
    if(count==Option.RepeatStart/LOCALKEYSCANRATE)return true;
    if(count >= (Option.RepeatStart+Option.RepeatRate)/LOCALKEYSCANRATE && 
    (count-Option.RepeatStart/LOCALKEYSCANRATE) % (Option.RepeatRate/LOCALKEYSCANRATE)==0)return true;
    return false;
}
void cmd_keyscan(void){
    static bool shift=false, function=false, s_lock=false, ctrl=false, alt=false, light=true;
    int key=0;
    static unsigned short pressed[79]={0};
    for(int cols = 24; cols < 37; cols++) {   
            if(cols==25)continue;
            PinSetBit(PINMAP[cols], ODCCLR);                      // pull it low
            for(int rows = 37; rows < 43; rows++) {   
                int index=localkeymap[cols-24][rows-37];
                if(PinRead((unsigned char)PINMAP[rows]) == 0) {                  // if it is low we have found a keypress
                    pressed[index]++;
                } else pressed[index]=0;
            }
            PinSetBit(PINMAP[cols], ODCSET);                      // wasn't this pin, clear the pulldown
    }
    function=pressed[18] ? true : false;
    shift=pressed[5] ? true : false;
    alt=pressed[30] ? true : false;
    ctrl=pressed[24] ? true : false;
    if(pressed[4]==1){
        gpio_xor_mask64((uint64_t)1<<44);
        s_lock^=1;

    }
    if(pressed[13]==1){
        light^=1;
        setpwm(PINMAP[43], &KeyboardlightChannel, &KeyboardlightSlice, 50000.0, light ? Option.KeyboardBrightness: 0);
    }
    LocalKeyDown[6]=(alt ? 1: 0) |
    (ctrl ? 2: 0) |
    (function ? 4: 0) |
    (shift ? 8: 0);

    for(int i=1;i<79;i++){
        if(checkpressedtime(pressed[i])){
            if(function)key=(s_lock ^ shift) ? asciimapfu[i]: asciimapfl[i];
            else key=(s_lock ^ shift) ? asciimapu[i]: asciimapl[i];
            if(ctrl && (key>='a' && key<='z'))key-=('a'-1);
            if(ctrl && key>='A' && key<='Z')key-=('A'-1);
            if (key == BreakKey) { // if the user wants to stop the progran
                MMAbort = true; // set the flag for the interpreter to see
                ConsoleRxBufHead = ConsoleRxBufTail; // empty the buffer
                // break;
            } else {
                ConsoleRxBuf[ConsoleRxBufHead] = key; // store the byte in the ring buffer
                if (ConsoleRxBuf[ConsoleRxBufHead] == keyselect && KeyInterrupt != NULL) {
                    Keycomplete = true;
                } else {
                    ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE; // advance the head of the queue
                    if (ConsoleRxBufHead == ConsoleRxBufTail) { // if the buffer has overflowed
                        ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
                }
                }
            }
        }
    }
}
#endif


/****************************************************************************************************************************
 The LCD command
*****************************************************************************************************************************/

void LCD_Nibble(int Data, int Flag, int Wait_uSec);
void LCD_Byte(int Data, int Flag, int Wait_uSec);
void LcdPinSet(int pin, int val);
static char lcd_pins[6];
/*  @endcond */

void cmd_lcd(void)
 {
    unsigned char *p;
    int i, j, code;

    if((p = checkstring(cmdline, (unsigned char *)"INIT"))) {
        getargs(&p, 11, (unsigned char *)",");
        if(argc != 11) error("Invalid syntax");
        if(*lcd_pins) error("Already open");
        for(i = 0; i < 6; i++) {
        	code=0;
        	if(!(code=codecheck(argv[i * 2])))argv[i * 2]+=2;
            lcd_pins[i] = getinteger(argv[i * 2]);
        	if(!code)lcd_pins[i]=codemap(lcd_pins[i]);
            if(ExtCurrentConfig[(int)lcd_pins[i]] >= EXT_COM_RESERVED)  error("Pin %/| is in use",lcd_pins[i],lcd_pins[i]);
            ExtCfg(lcd_pins[i], EXT_DIG_OUT, 0);
            ExtCfg(lcd_pins[i], EXT_COM_RESERVED, 0);
        }
        LCD_Nibble(0b0011, 0, 5000);                                // reset
        LCD_Nibble(0b0011, 0, 5000);                                // reset
        LCD_Nibble(0b0011, 0, 5000);                                // reset
        LCD_Nibble(0b0010, 0, 2000);                                // 4 bit mode
        LCD_Byte(0b00101100, 0, 600);                               // 4 bits, 2 lines
        LCD_Byte(0b00001100, 0, 600);                               // display on, no cursor
        LCD_Byte(0b00000110, 0, 600);                               // increment on write
        LCD_Byte(0b00000001, 0, 3000);                              // clear the display
        return;
    }

    if(!*lcd_pins) error("Not open");
    if(checkstring(cmdline, (unsigned char *)"CLOSE")) {
        for(i = 0; i < 6; i++) {
			ExtCfg(lcd_pins[i], EXT_NOT_CONFIG, 0);					// all set to unconfigured
			ExtSet(lcd_pins[i], 0);									// all outputs (when set) default to low
            *lcd_pins = 0;
        }
    } else if(checkstring(cmdline, (unsigned char *)"CLEAR")) {                // clear the display
        LCD_Byte(0b00000001, 0, 3000);
    } else if((p = checkstring(cmdline, (unsigned char *)"CMD")) || (p = checkstring(cmdline, (unsigned char *)"DATA"))) { // send a command or data
        getargs(&p, MAX_ARG_COUNT * 2, (unsigned char *)",");
        for(i = 0; i < argc; i += 2) {
            j = getint(argv[i], 0, 255);
            LCD_Byte(j, toupper(*cmdline) == 'D', 0);
        }
    } else {
        const char linestart[4] = {0, 64, 20, 84};
        int center, pos;

        getargs(&cmdline, 5, (unsigned char *)",");
        if(argc != 5) error("Invalid syntax");
        i = getint(argv[0], 1, 4);
        pos = 1;
        if(checkstring(argv[2], (unsigned char *)"C8"))
            center = 8;
        else if(checkstring(argv[2], (unsigned char *)"C16"))
            center = 16;
        else if(checkstring(argv[2], (unsigned char *)"C20"))
            center = 20;
        else if(checkstring(argv[2], (unsigned char *)"C40"))
            center = 40;
        else {
            center = 0;
            pos = getint(argv[2], 1, 256);
        }
        p = getstring(argv[4]);                                     // returns an MMBasic string
        i = 128 + linestart[i - 1] + (pos - 1);
        LCD_Byte(i, 0, 600);
        for(j = 0; j < (center - *p) / 2; j++) {
            LCD_Byte(' ', 1, 0);
        }
        for(i = 1; i <= *p; i++) {
            LCD_Byte(p[i], 1, 0);
            j++;
        }
        for(; j < center; j++) {
            LCD_Byte(' ', 1, 0);
        }
    }
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

void LCD_Nibble(int Data, int Flag, int Wait_uSec) {
    int i;
    LcdPinSet(lcd_pins[4], Flag);
    for(i = 0; i < 4; i++)
        LcdPinSet(lcd_pins[i], (Data >> i) & 1);
    LcdPinSet(lcd_pins[5], 1); uSec(250); LcdPinSet(lcd_pins[5], 0);
    if(Wait_uSec)
        uSec(Wait_uSec);
    else
        uSec(250);
}


void LCD_Byte(int Data, int Flag, int Wait_uSec) {
    LCD_Nibble(Data/16, Flag, 0);
    LCD_Nibble(Data, Flag, Wait_uSec);
}


void LcdPinSet(int pin, int val) {
    PinSetBit(pin, val ? LATSET : LATCLR);
}
int64_t DHmem(int pin){
    int timeout = 400;
    long long int r;
    int i;
    writeusclock(0);
    PinSetBit(pin, CNPUSET);
    PinSetBit(pin, TRISSET);
    uSec(5);
    // wait for the DHT22 to pull the pin low and return it high then take it low again
    while(PinRead(pin)) if(readusclock() > timeout) goto error_exit;
    while(!PinRead(pin)) if(readusclock() > timeout) goto error_exit;
    while(PinRead(pin)) if(readusclock() > timeout) goto error_exit;
//    PInt(readusclock());PRet();

    // now we wait for the pin to go high and measure how long it stays high (> 50uS is a one bit, < 50uS is a zero bit)
    for(r = i = 0; i < 40; i++) {
    	timeout=400;
        while(!PinRead(pin)) if(readusclock() > timeout) goto error_exit;
        timeout=400;writeusclock(0);
        while(PinRead(pin)) if(readusclock() > timeout) goto error_exit;
        r <<= 1;
        r |= (readusclock() > 50);
    }
    return r;
    error_exit:
    return -1;

}
/*  @endcond */

void cmd_DHT22(void) {
     int pin;
    union colourmap {
    unsigned char dhtbytes[8];
    long long int r;
    } dht;
//    long long int r;
    int dht11=0;
    MMFLOAT *temp, *humid;

    getargs(&cmdline, 7, (unsigned char *)",");
    if(!(argc == 5 || argc == 7)) error("Incorrect number of arguments");

    // get the two variables
	temp = findvar(argv[2], V_FIND);
	if(!(g_vartbl[g_VarIndex].type & T_NBR)) error("Invalid variable");
	humid = findvar(argv[4], V_FIND);
	if(!(g_vartbl[g_VarIndex].type & T_NBR)) error("Invalid variable");

    // get the pin number and set it up
    // get the pin number and set it up
	char code;
	if(!(code=codecheck(argv[0])))argv[0]+=2;
	pin = getinteger(argv[0]);
	if(!code)pin=codemap(pin);
    if(IsInvalidPin(pin)) error("Invalid pin ");
    if(ExtCurrentConfig[pin] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin,pin);
    if(argc==7){
    	dht11=getint(argv[6],0,1);
    }
    ExtCfg(pin, EXT_DIG_OUT, 0);
    
    // pulse the pin low for 1.5mS
    uSec(1500+dht11*18000);
    // we have all 40 bits
    // first validate against the checksum
    if((dht.r=DHmem(pin))==-1) goto error_exit;
    if((uint8_t)(dht.dhtbytes[4]+dht.dhtbytes[3]+dht.dhtbytes[2]+dht.dhtbytes[1])!=dht.dhtbytes[0])goto error_exit; 
//    if( ( (uint8_t)( ((r >> 8) & 0xff) + ((r >> 16) & 0xff) + ((r >> 24) & 0xff) + ((r >> 32) & 0xff) ) & 0xff) != (r & 0xff)) goto error_exit;                                           // returning temperature
    if(dht11==0){
		*temp = (MMFLOAT)((dht.r >> 8) &0x7fff) / 10.0;                       // get the temperature
		if((dht.r >> 8) &0x8000) *temp = -*temp;                            // the top bit is the sign
		*humid = (MMFLOAT)(dht.r >> 24) / 10.0;                               // get the humidity
    } else {
		*temp = (MMFLOAT)(dht.dhtbytes[2])+(MMFLOAT)(dht.dhtbytes[1])/10.0;                       // get the temperature
		*humid =(MMFLOAT)(dht.dhtbytes[4])+(MMFLOAT)(dht.dhtbytes[3])/10.0;                               // get the humidity
    }
    goto normal_exit;

error_exit:
    *temp = *humid = 1000.0;                                        // an obviously incorrect reading

normal_exit:
    ExtCfg(pin, EXT_NOT_CONFIG, 0);
    PinSetBit(pin, LATCLR);
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
void __not_in_flash_func(WS2812e)(int gppin, int T1H, int T1L, int T0H, int T0L, int nbr, char *p){
    for(int i=0;i<nbr;i++){
        for(int j=0;j<8;j++){
            if(*p & 1){
                gpio_put(gppin,true);
                shortpause(T1H);
                gpio_put(gppin,false);
                shortpause(T1L);
            } else {
                gpio_put(gppin,true);
                shortpause(T0H);
                gpio_put(gppin,false);
                shortpause(T0L);
            }
            *p>>=1;
        }
        p++;
    }
}
/*  @endcond */
void fun_dev(void){
    unsigned char *tp=NULL;
    tp = checkstring(ep, (unsigned char *)"WII");
    if(tp==NULL)tp = checkstring(ep, (unsigned char *)"CLASSIC");
    if(tp){
       //	int ax; //classic left x
        //	int ay; //classic left y
        //	int az; //classic centre
        //	int Z;  //classic right x
        //	int C;  //classic right y
        //	int L;  //classic left analog
        //	int R;  //classic right analog
        //	unsigned short x0; //classic buttons
        getargs(&tp,1,(unsigned char *)",");
		if(!classic1)error("Not open");
         if(checkstring(argv[0], (unsigned char *)"LX"))iret=nunstruct[0].ax;
        else if(checkstring(argv[0], (unsigned char *)"LY"))iret=nunstruct[0].ay;
        else if(checkstring(argv[0], (unsigned char *)"RX"))iret=nunstruct[0].Z;
        else if(checkstring(argv[0], (unsigned char *)"RY"))iret=nunstruct[0].C;
        else if(checkstring(argv[0], (unsigned char *)"L"))iret=nunstruct[0].L;
        else if(checkstring(argv[0], (unsigned char *)"R"))iret=nunstruct[0].R;
        else if(checkstring(argv[0], (unsigned char *)"B"))iret=nunstruct[0].x0;
        else if(checkstring(argv[0], (unsigned char *)"T"))iret=nunstruct[0].type;
        else iret=0;
        targ=T_INT;
    } else if((tp=checkstring(ep,(unsigned char *)"NUNCHUCK"))){
        unsigned char *p;
        getargs(&tp,1,(unsigned char *)",");
        p=argv[0];
        if(toupper(*p)=='A'){
            p++;
            if(p[1]==0){
                if(toupper(*p)=='X')iret=nunstruct[5].ax;
                else if(toupper(*p)=='Y')iret=nunstruct[5].ay;
                else if(toupper(*p)=='Z')iret=nunstruct[5].az;
                else error("Syntax");
            } else {
                if(p[1]=='0'){
                    if(toupper(*p)=='X')iret=nunstruct[5].x0;
                    else if(toupper(*p)=='Y')iret=nunstruct[5].y0;
                    else if(toupper(*p)=='Z')iret=nunstruct[5].z0;
                    else error("Syntax");
                } else if(p[1]=='1'){
                    if(toupper(*p)=='X')iret=nunstruct[5].x1;
                    else if(toupper(*p)=='Y')iret=nunstruct[5].y1;
                    else if(toupper(*p)=='Z')iret=nunstruct[5].z1;
                    else error("Syntax");
                } else error("Syntax");
            }
        } else if(toupper(*p)=='J'){
            p++;
            if(p[1]==0){
                if(toupper(*p)=='X')iret=nunstruct[5].x;
                else if(toupper(*p)=='Y')iret=nunstruct[5].y;
            } else {
                if(toupper(*p)=='X'){
                    p++;
                    if(toupper(*p)=='L'){
                        iret=nunstruct[5].calib[9];
                    } else if(toupper(*p)=='C'){
                        iret=nunstruct[5].calib[10];
                    } else if(toupper(*p)=='R'){
                        iret=nunstruct[5].calib[8];
                    } else error("Syntax");
                } else if(toupper(*p)=='Y'){
                    p++;
                    if(toupper(*p)=='T'){
                        iret=nunstruct[5].calib[11];
                    } else if(toupper(*p)=='C'){
                        iret=nunstruct[5].calib[13];
                    } else if(toupper(*p)=='B'){
                        iret=nunstruct[5].calib[12];
                    } else error("Syntax");
                } else error("Syntax");
            }
        } else {
            if(toupper(*p)=='Z')iret=nunstruct[5].Z;
            else if(toupper(*p)=='C')iret=nunstruct[5].C;
            else if(toupper(*p)=='T')iret=nunstruct[5].type;
            else error("Syntax");
        }
	    targ=T_INT;
    } else if((tp=checkstring(ep,(unsigned char *)"GAMEPAD"))){
      //	int ax; //classic left x
        //	int ay; //classic left y
        //	int az; //classic centre
        //	int Z;  //classic right x
        //	int C;  //classic right y
        //	int L;  //classic left analog
        //	int R;  //classic right analog
        //	unsigned short x0; //classic buttons
         getargs(&tp,3,(unsigned char *)",");
         int n=getint(argv[0],1,4);
         if(checkstring(argv[2], (unsigned char *)"LX"))iret=nunstruct[n].ax;
        else if(checkstring(argv[2], (unsigned char *)"LY"))iret=nunstruct[n].ay;
        else if(checkstring(argv[2], (unsigned char *)"RX"))iret=nunstruct[n].Z;
        else if(checkstring(argv[2], (unsigned char *)"RY"))iret=nunstruct[n].C;
        else if(checkstring(argv[2], (unsigned char *)"L"))iret=nunstruct[n].L;
        else if(checkstring(argv[2], (unsigned char *)"R"))iret=nunstruct[n].R;
        else if(checkstring(argv[2], (unsigned char *)"B"))iret=nunstruct[n].x0;
        else if(checkstring(argv[2], (unsigned char *)"GX"))iret=nunstruct[n].gyro[0];
        else if(checkstring(argv[2], (unsigned char *)"GY"))iret=nunstruct[n].gyro[1];
        else if(checkstring(argv[2], (unsigned char *)"GZ"))iret=nunstruct[n].gyro[2];
        else if(checkstring(argv[2], (unsigned char *)"AX"))iret=nunstruct[n].accs[0];
        else if(checkstring(argv[2], (unsigned char *)"AY"))iret=nunstruct[n].accs[1];
        else if(checkstring(argv[2], (unsigned char *)"AZ"))iret=nunstruct[n].accs[2];
        else if(checkstring(argv[2], (unsigned char *)"T"))iret=nunstruct[n].type;
#ifdef USBKEYBOARD
        else if(checkstring(argv[2], (unsigned char *)"RAW")){
            sret=GetTempMemory(STRINGSIZE);
            targ=T_STR;
            Mstrcpy(sret,(unsigned char *)HID[n-1].report);
            return;
        }
#endif
        else iret=0;
        targ=T_INT;
    } else if((tp=checkstring(ep,(unsigned char *)"MOUSE"))){
/*        Returns data from a PS2 mouse
        'funct' is a 1 letter code indicating the information to return as follows:
        X returns the value of the mouse X-position
        Y returns the value of the mouse Y-position
        L returns the value of the left mouse button (1 if pressed)
        R returns the value of the right mouse button (1 if pressed)
        W returns the value of the scroll wheel mouse button (1 if pressed)
        D This allows you to detect a double click of the left mouse button. The
        algorithm requires that the two clicks must occur between 100 and
        500 milliseconds apart. The report via MOUSE(D) is then valid for
        500mSec before it times out or until it is read.
        T This returns 3 if a PS2 mouse has a scroll wheel or 0 if not.*/

         getargs(&tp,3,(unsigned char *)",");
         int n=getint(argv[0],1,4);
         if(checkstring(argv[2], (unsigned char *)"X"))iret=nunstruct[n].ax;
        else if(checkstring(argv[2], (unsigned char *)"Y"))iret=nunstruct[n].ay;
        else if(checkstring(argv[2], (unsigned char *)"L"))iret=nunstruct[n].L;
        else if(checkstring(argv[2], (unsigned char *)"R"))iret=nunstruct[n].R;
        else if(checkstring(argv[2], (unsigned char *)"M"))iret=nunstruct[n].C;
        else if(checkstring(argv[2], (unsigned char *)"W"))iret=nunstruct[n].az;
        else if(checkstring(argv[2], (unsigned char *)"D")){iret=nunstruct[n].Z;nunstruct[n].Z=0;}
        else if(checkstring(argv[2], (unsigned char *)"T"))iret=nunstruct[n].classic[0];
        else error("Syntax");
        targ=T_INT;
    } else error("Syntax");

}

void cmd_WS2812(void){
        int64_t *dest=NULL;
        uint32_t pin, red , green, blue, white, colour;
        int T0H=0,T0L=0,T1H=0,T1L=0,TRST=0;
        unsigned char *p;
        int i, j, bit, nbr=0, colours=3;
        int ticks_per_millisecond=ticks_per_second/1000; 
    	getargs(&cmdline, 7, (unsigned char *)",");
        if(argc != 7)error("Argument count");
    	p=argv[0];
    	if(toupper(*p)=='O'){
    		T0H=16777215 + setuptime-((7*ticks_per_millisecond)/20000) ;
    		T1H=16777215 + setuptime-((14*ticks_per_millisecond)/20000) ;
    		T0L=16777215 + setuptime-((13*ticks_per_millisecond)/20000) ;
    		T1L=16777215 + setuptime-((9.5*ticks_per_millisecond)/20000) ;
            TRST=50;
    	} else if(toupper(*p)=='B'){
    		T0H=16777215 + setuptime-((8*ticks_per_millisecond)/20000) ;
    		T1H=16777215 + setuptime-((16*ticks_per_millisecond)/20000) ;
    		T0L=16777215 + setuptime-((14*ticks_per_millisecond)/20000) ;
    		T1L=16777215 + setuptime-((6.5*ticks_per_millisecond)/20000) ;
            TRST=280;
    	} else if(toupper(*p)=='S' || toupper(*p)=='W' ){
            if(toupper(*p)=='W')colours=4;
    		T0H=16777215 + setuptime-((6*ticks_per_millisecond)/20000) ;
    		T0L=16777215 + setuptime-((15*ticks_per_millisecond)/20000) ;
    		T1H=16777215 + setuptime-((12*ticks_per_millisecond)/20000) ;
    		T1L=16777215 + setuptime-((9*ticks_per_millisecond)/20000) ;
            TRST=80;
    	} else error("Syntax");
        nbr=getint(argv[4],1,256);
        if(nbr>1){
            parseintegerarray(argv[6], &dest, 4, 1, NULL, false);
        } else {
            colour=getinteger(argv[6]);
            dest = (long long int *)&colour;
        }
        unsigned char code;
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin = getinteger(argv[2]);
        if(!code)pin=codemap(pin);
        if(IsInvalidPin(pin)) error("Invalid pin");
        int gppin=PinDef[pin].GPno;
        if(!(ExtCurrentConfig[pin] == EXT_DIG_OUT || ExtCurrentConfig[pin] == EXT_NOT_CONFIG)) error("Pin %/| is not off or an output",pin,pin);
        if(ExtCurrentConfig[pin] == EXT_NOT_CONFIG)ExtCfg(pin, EXT_DIG_OUT, 0);
		p=GetTempMemory((nbr+1)*colours);
		uint64_t endreset=time_us_64()+TRST;
    	for(i=0;i<nbr;i++){
    		green=(dest[i]>>8) & 0xFF;
    		red=(dest[i]>>16) & 0xFF;
    		blue=dest[i] & 0xFF;
            if(colours==4)white=(dest[i]>>24) & 0xFF;
			p[0]=0;p[1]=0;p[2]=0;
            if(colours==4){p[3]=0;}
    		for(j=0;j<8;j++){
    			bit=1<<j;
    			if( green &  (1<<(7-j)) )p[0] |= bit;
    			if(red   & (1<<(7-j)))p[1] |= bit;
    			if(blue  & (1<<(7-j)))p[2] |= bit;
                if(colours==4){
    			    if(white  & (1<<(7-j)))p[3] |= bit;
                }
    		}
    		p+=colours;
    	}
    	p-=(nbr*colours);
        while(time_us_64()<endreset){}
        disable_interrupts_pico();
        WS2812e(gppin, T1H, T1L, T0H, T0L, nbr*colours, (char *)p);
        enable_interrupts_pico();
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
void __not_in_flash_func(bitstream)(int gppin, unsigned int *data, int num){
    for(int i=0;i<num;i++){
        gpio_xor_mask64(gppin);
        shortpause(data[i])
    }
}
void __not_in_flash_func(serialtx)(int gppin, unsigned char *string, int bittime){
    int mask;
    int count = 0;
    while(count++ < string[0]) {
        systick_hw->cvr=0;
        gpio_clr_mask64(gppin);                                    // send the start bit
        mask=1;
        while(systick_hw->cvr>bittime){}; 
        systick_hw->cvr=0;
        for (mask=1;mask<0x100; mask<<=1) {
            if(string[count] & mask) {                               // check the bit to send
                gpio_set_mask64(gppin);                                    // send the start bit
            } else {
                gpio_clr_mask64(gppin);                                    // send the start bit
            }
            while(systick_hw->cvr>bittime){}; 
            systick_hw->cvr=0;
        }
        gpio_set_mask64(gppin);                                    // send the start bit
        while(systick_hw->cvr>bittime){}; 
    }
}
unsigned short FloatToUint32(MMFLOAT x) {
    if(x<0 || x > 4294967295)
        error("Number range");
    return (x >= 0 ? (unsigned int)(x + 0.5) : (unsigned int)(x - 0.5)) ;
}
int __not_in_flash_func(serialrx)(int gppin, unsigned char *string, int timeout, int bittime, int half, int maxchars, char *termchars){
    int i,c,count=0;
    while(1){
        while(gpio_get_all64() & gppin) {                              // wait for the start bit
            if(readusclock() >= timeout) return -1;                   // return if there is a timeout
        }
        systick_hw->cvr=0;
        while(systick_hw->cvr>half){}; 
        systick_hw->cvr=0;
        if(gpio_get_all64() & gppin) continue;                         // go around again if not low
        c=0;
        for(i = 0; i < 8; i++) {
            while(systick_hw->cvr>bittime){}; 
            systick_hw->cvr=0;
            c |= (((gpio_get_all64() & gppin) ? 1 : 0) << i);                      // and add this bit in
        }
        while(systick_hw->cvr>bittime){}; 
        if(!(gpio_get_all64() & gppin)) continue;                      // a framing error if not high
        count++;
        string[count] = c;                                          // save the character
        string[0] = count;                                          // and update the numbers of characters in the string
        if(count >= maxchars) return 2;                // do we have our maximun number of characters?
        if(termchars) {
            for(i = 0; i < termchars[0]; i++) {                     // check each terminating character
                if(c == termchars[i + 1]) return 3;                 // and exit if this character is one of them
            }
        }
    }
}
/*  @endcond */
void cmd_device(void){
	unsigned char *tp;
	tp = checkstring(cmdline, (unsigned char *)"WS2812");
	if(tp) {
        cmdline=tp;
		cmd_WS2812();
		return;
	}
	tp = checkstring(cmdline, (unsigned char *)"KEYPAD");
	if(tp) {
        cmdline=tp;
		cmd_keypad();
		return;
	}
	tp = checkstring(cmdline, (unsigned char *)"LCD");
	if(tp) {
        cmdline=tp;
		cmd_lcd();
		return;
	}
#ifndef PICOMITEVGA
	tp = checkstring(cmdline, (unsigned char *)"CAMERA");
	if(tp) {
        cmdline=tp;
		cmd_camera();
		return;
	}
#endif
    tp = checkstring(cmdline, (unsigned char *)"WII CLASSIC");
    if(tp==NULL)tp = checkstring(cmdline, (unsigned char *)"WII");
	if(tp) {
        cmdline=tp;
		cmd_Classic();
		return;
	}
	tp = checkstring(cmdline, (unsigned char *)"NUNCHUCK");
	if(tp) {
        cmdline=tp;
		cmd_Nunchuck();
		return;
	}
#ifdef USBKEYBOARD
	tp = checkstring(cmdline, (unsigned char *)"MOUSE");
	if(tp) {
        cmdline=tp;
		cmd_mouse();
		return;
	}
	tp = checkstring(cmdline, (unsigned char *)"GAMEPAD");
	if(tp) {
        cmdline=tp;
		cmd_gamepad();
		return;
	}
#endif
	tp = checkstring(cmdline, (unsigned char *)"HUMID");
	if(tp) {
        cmdline=tp;
		cmd_DHT22();
		return;
	}
	tp = checkstring(cmdline, (unsigned char *)"SERIALRX");
	if(tp) {
        int maxchars=255;
        char *termchars=NULL;
		getargs(&tp, 13,(unsigned char *)",");
		if(argc < 9) error("Argument count");
        unsigned char code;
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        int pin = getinteger(argv[0]);
        if(!code)pin=codemap(pin);
        if(IsInvalidPin(pin)) error("Invalid pin");
        if(!(ExtCurrentConfig[pin] == EXT_DIG_IN || ExtCurrentConfig[pin] == EXT_NOT_CONFIG)) error("Pin %/| is not off or an input",pin,pin);
        if(ExtCurrentConfig[pin] == EXT_NOT_CONFIG)ExtCfg(pin, EXT_DIG_IN, CNPUSET);
        int gppin=PinDef[pin].GPno;
        int baudrate=getint(argv[2],110,230400);
        unsigned char *string=NULL;
        string = findvar(argv[4], V_FIND);
	    if(!(g_vartbl[g_VarIndex].type & T_STR)) error("Invalid variable");
        int timeout=getint(argv[6],1,100000)*1000;
        void *status=findvar(argv[8], V_FIND);
        int type=g_vartbl[g_VarIndex].type;
	    if(!(type & (T_NBR | T_INT))) error("Invalid variable");
        if(argc>9 && *argv[10])maxchars=getint(argv[10],1,255);
        if(argc==13)termchars=(char *)getstring(argv[12]);
        writeusclock(0);
        int bittime=16777215 + 12  - (ticks_per_second/baudrate) ;
        int half = 16777215 + 12  - (ticks_per_second/(baudrate<<1)) ;
        if(!(gpio_get_all64() & gppin))error("Framing error");
        disable_interrupts_pico();
        int istat=serialrx(gppin, string, timeout, bittime, half, maxchars, termchars);
        enable_interrupts_pico();
        if(type & T_INT)*(int64_t *)status=(int64_t)istat;
        else *(MMFLOAT *)status=(MMFLOAT)istat;
        return;
    }
	tp = checkstring(cmdline, (unsigned char *)"SERIALTX");
	if(tp) {
//        int mask;
		getargs(&tp, 5,(unsigned char *)",");
		if(!(argc == 5)) error("Argument count");
        unsigned char code;
//        int count = 0;
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        int pin = getinteger(argv[0]);
        if(!code)pin=codemap(pin);
        if(IsInvalidPin(pin)) error("Invalid pin");
        int gppin=(1<<PinDef[pin].GPno);
        int baudrate=getint(argv[2],110,230400);
        unsigned char *string=getstring(argv[4]);
        if(!(ExtCurrentConfig[pin] == EXT_DIG_OUT || ExtCurrentConfig[pin] == EXT_NOT_CONFIG)) error("Pin %/| is not off or an output",pin,pin);
        if(ExtCurrentConfig[pin] == EXT_NOT_CONFIG)ExtCfg(pin, EXT_DIG_OUT, 0);
        gpio_set_mask64(gppin);                                    // send the start bit
        int bittime=16777215 + 12  - (ticks_per_second/baudrate) ;
        disable_interrupts_pico();
        serialtx(gppin,string, bittime);
        enable_interrupts_pico();
		return;
	}
	tp = checkstring(cmdline, (unsigned char *)"BITSTREAM");
	if(tp) {
		int i,num,size;
		uint32_t pin;
        int ticks_per_millisecond=ticks_per_second/1000;
		MMFLOAT *a1float=NULL;
		int64_t *a1int=NULL;
		unsigned int *data;
		getargs(&tp, 5,(unsigned char *)",");
		if(!(argc == 5)) error("Argument count");
		num=getint(argv[2],1,10000);
        unsigned char code;
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        pin = getinteger(argv[0]);
        if(!code)pin=codemap(pin);
        if(IsInvalidPin(pin)) error("Invalid pin");
        int gppin=(1<<PinDef[pin].GPno);
        if(!(ExtCurrentConfig[pin] == EXT_DIG_OUT || ExtCurrentConfig[pin] == EXT_NOT_CONFIG)) error("Pin %/| is not off or an output",pin,pin);
        if(ExtCurrentConfig[pin] == EXT_NOT_CONFIG)ExtCfg(pin, EXT_DIG_OUT, 0);
        size=parsenumberarray(argv[4],&a1float, &a1int, 3, 1, NULL, false);
        if(size < num)error("Array too small");
        data=GetTempMemory(num * sizeof(unsigned int));
        if(a1float!=NULL){
            for(i=0; i< num;i++)data[i]= FloatToUint32(*a1float++);
        } else {
            for(i=0; i< num;i++){
                if(*a1int <0 || *a1int>67108)error("Number range");
                data[i]= *a1int++ ;
            }
        }
        for(i=0; i< num;i++){
            data[i]=16777215 + setuptime-((data[i]*ticks_per_millisecond)/1000) ;
        }
//        data[0]+=((ticks_per_millisecond/2000)+(250000-Option.CPU_Speed)/1000);
        disable_interrupts_pico();
        bitstream(gppin,data,num);
        enable_interrupts_pico();
		return;
	}
    error("Syntax");
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
void __not_in_flash_func(ADCint)()
{
	// Clear the interrupt request for DMA control channel
	dma_hw->ints1 = (1u << ADC_dma_chan);
    if(adcint==adcint2)adcint=adcint1;
    else adcint=adcint2;
    ADCDualBuffering=true;
}
/*  @endcond */

void cmd_adc(void){
	unsigned char *tp;
	tp = checkstring(cmdline, (unsigned char *)"OPEN");
	if(tp) {
        getargs(&tp,5,(unsigned char *)",");
        if(ADCopen)error("Already open");
        if(!(argc==3 || argc==5))error("Syntax");
#ifndef PICOMITEWEB
        int nbr=getint(argv[2],1,4); //number of ADC channels
#else
        int nbr=getint(argv[2],1,3); //number of ADC channels
#endif
        frequency=(float)getnumber(argv[0])*nbr;
        if(frequency<ADC_CLK_SPEED/65536.0/96.0 || frequency> ADC_CLK_SPEED/96.0)error("Invalid frequency");
#ifdef rp2350
#ifdef PICOMITEWEB
        if(!(ExtCurrentConfig[31] == EXT_ANA_IN || ExtCurrentConfig[31] == EXT_NOT_CONFIG)) error("Pin GP26 is not off or an ADC input");
        if(ExtCurrentConfig[31] == EXT_NOT_CONFIG)ExtCfg(31, EXT_ANA_IN, 0);
        ExtCfg(31, EXT_COM_RESERVED, 0);
        if(nbr>=2){
            if(!(ExtCurrentConfig[32] == EXT_ANA_IN || ExtCurrentConfig[32] == EXT_NOT_CONFIG)) error("Pin GP27 is not off or an ADC input");
            if(ExtCurrentConfig[32] == EXT_NOT_CONFIG)ExtCfg(32, EXT_ANA_IN, 0);
            ExtCfg(32, EXT_COM_RESERVED, 0);
        }
        if(nbr>=3){
            if(!(ExtCurrentConfig[34] == EXT_ANA_IN || ExtCurrentConfig[34] == EXT_NOT_CONFIG)) error("Pin GP28 is not off or an ADC input");
            if(ExtCurrentConfig[34] == EXT_NOT_CONFIG)ExtCfg(34, EXT_ANA_IN, 0);
            ExtCfg(34, EXT_COM_RESERVED, 0);
        }
#else
if(rp2350a){
        if(!(ExtCurrentConfig[31] == EXT_ANA_IN || ExtCurrentConfig[31] == EXT_NOT_CONFIG)) error("Pin GP26 is not off or an ADC input");
        if(ExtCurrentConfig[31] == EXT_NOT_CONFIG)ExtCfg(31, EXT_ANA_IN, 0);
        ExtCfg(31, EXT_COM_RESERVED, 0);
        if(nbr>=2){
            if(!(ExtCurrentConfig[32] == EXT_ANA_IN || ExtCurrentConfig[32] == EXT_NOT_CONFIG)) error("Pin GP27 is not off or an ADC input");
            if(ExtCurrentConfig[32] == EXT_NOT_CONFIG)ExtCfg(32, EXT_ANA_IN, 0);
            ExtCfg(32, EXT_COM_RESERVED, 0);
        }
        if(nbr>=3){
            if(!(ExtCurrentConfig[34] == EXT_ANA_IN || ExtCurrentConfig[34] == EXT_NOT_CONFIG)) error("Pin GP28 is not off or an ADC input");
            if(ExtCurrentConfig[34] == EXT_NOT_CONFIG)ExtCfg(34, EXT_ANA_IN, 0);
            ExtCfg(34, EXT_COM_RESERVED, 0);
        }
        if(nbr>=4){
            if(!(ExtCurrentConfig[44] == EXT_ANA_IN || ExtCurrentConfig[44] == EXT_NOT_CONFIG)) error("Pin GP29 is not off or an ADC input");
            if(ExtCurrentConfig[44] == EXT_NOT_CONFIG)ExtCfg(44, EXT_ANA_IN, 0);
            ExtCfg(44, EXT_COM_RESERVED, 0);
        }
} else {
        if(!(ExtCurrentConfig[55] == EXT_ANA_IN || ExtCurrentConfig[55] == EXT_NOT_CONFIG)) error("Pin GP40 is not off or an ADC input");
        if(ExtCurrentConfig[55] == EXT_NOT_CONFIG)ExtCfg(55, EXT_ANA_IN, 0);
        ExtCfg(55, EXT_COM_RESERVED, 0);
        if(nbr>=2){
            if(!(ExtCurrentConfig[56] == EXT_ANA_IN || ExtCurrentConfig[56] == EXT_NOT_CONFIG)) error("Pin GP41 is not off or an ADC input");
            if(ExtCurrentConfig[56] == EXT_NOT_CONFIG)ExtCfg(56, EXT_ANA_IN, 0);
            ExtCfg(56, EXT_COM_RESERVED, 0);
        }
        if(nbr>=3){
            if(!(ExtCurrentConfig[57] == EXT_ANA_IN || ExtCurrentConfig[57] == EXT_NOT_CONFIG)) error("Pin GP42 is not off or an ADC input");
            if(ExtCurrentConfig[57] == EXT_NOT_CONFIG)ExtCfg(57, EXT_ANA_IN, 0);
            ExtCfg(57, EXT_COM_RESERVED, 0);
        }
        if(nbr>=4){
            if(!(ExtCurrentConfig[58] == EXT_ANA_IN || ExtCurrentConfig[58] == EXT_NOT_CONFIG)) error("Pin GP43 is not off or an ADC input");
            if(ExtCurrentConfig[58] == EXT_NOT_CONFIG)ExtCfg(58, EXT_ANA_IN, 0);
            ExtCfg(58, EXT_COM_RESERVED, 0);
        }
}
#endif
#else
        if(!(ExtCurrentConfig[31] == EXT_ANA_IN || ExtCurrentConfig[31] == EXT_NOT_CONFIG)) error("Pin GP26 is not off or an ADC input");
        if(ExtCurrentConfig[31] == EXT_NOT_CONFIG)ExtCfg(31, EXT_ANA_IN, 0);
        ExtCfg(31, EXT_COM_RESERVED, 0);
        if(nbr>=2){
            if(!(ExtCurrentConfig[32] == EXT_ANA_IN || ExtCurrentConfig[32] == EXT_NOT_CONFIG)) error("Pin GP27 is not off or an ADC input");
            if(ExtCurrentConfig[32] == EXT_NOT_CONFIG)ExtCfg(32, EXT_ANA_IN, 0);
            ExtCfg(32, EXT_COM_RESERVED, 0);
        }
        if(nbr>=3){
            if(!(ExtCurrentConfig[34] == EXT_ANA_IN || ExtCurrentConfig[34] == EXT_NOT_CONFIG)) error("Pin GP28 is not off or an ADC input");
            if(ExtCurrentConfig[34] == EXT_NOT_CONFIG)ExtCfg(34, EXT_ANA_IN, 0);
            ExtCfg(34, EXT_COM_RESERVED, 0);
        }
#ifndef PICOMITEWEB
        if(nbr>=4){
            if(!(ExtCurrentConfig[44] == EXT_ANA_IN || ExtCurrentConfig[44] == EXT_NOT_CONFIG)) error("Pin GP29 is not off or an ADC input");
            if(ExtCurrentConfig[44] == EXT_NOT_CONFIG)ExtCfg(44, EXT_ANA_IN, 0);
            ExtCfg(44, EXT_COM_RESERVED, 0);
        }
#endif
#endif
        if(argc==5){
        	InterruptUsed = true;
        	ADCInterrupt = (char *)GetIntAddress(argv[4]);							// get the interrupt location
    	} else ADCInterrupt = NULL;
        ADCopen=nbr;
		return;
	}
	tp = checkstring(cmdline, (unsigned char *)"RUN");
    if(tp){
        getargs(&tp, 3, (unsigned char *)",");
		if(!ADCopen)error("ADC not open");
        if(!(argc == 3))error("Argument count");
        ADCmax=0;
        ADCpos=0;
        adcint1=adcint2=NULL;
        int64_t *adcval=NULL;
#ifdef rp2350
        int dims[MAXDIM]={0};
#else
        short dims[MAXDIM]={0};
#endif
        int card1=parseintegerarray(argv[0], &adcval, 1, 1, dims, true);
        adcint1=(uint8_t *)adcval;
        adcval=NULL;
        ADCmax=parseintegerarray(argv[2], &adcval, 2, 1, dims, true);
        adcint2=(uint8_t *)adcval;
        if(card1!=ADCmax)error("Array size mismatch %,%",card1, ADCmax);
        ADCmax *=8;
        dma_channel_cleanup(ADC_dma_chan);
        dma_channel_cleanup(ADC_dma_chan2);
        adc_init();
        adc_set_round_robin(ADCopen==1 ? 1 : ADCopen==2 ? 3 : ADCopen==3 ? 7 : 15);
        adc_fifo_setup(
            true,    // Write each completed conversion to the sample FIFO
            true,    // Enable DMA data request (DREQ)
            1,       // DREQ (and IRQ) asserted when at least 1 sample present
            false,   // We won't see the ERR bit because of 8 bit reads; disable.
            true     // Shift each sample to 8 bits when pushing to FIFO
        );
        adcint=adcint1;
        SetADCFreq(frequency);
        // Set up the DMA to start transferring data as soon as it appears in FIFO
        dma_channel_config cfg = dma_channel_get_default_config(ADC_dma_chan);

        // Reading from constant address, writing to incrementing byte addresses
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
        channel_config_set_read_increment(&cfg, false);
        channel_config_set_write_increment(&cfg, true);
        channel_config_set_irq_quiet(&cfg, false);
        channel_config_set_dreq(&cfg, DREQ_ADC);
        channel_config_set_chain_to(&cfg, ADC_dma_chan2);
        dma_channel_configure(ADC_dma_chan, &cfg,
            adcint,    // dst
            &adc_hw->fifo,  // src
            ADCmax,  // transfer count
            false            // start immediately
        );
        dma_channel_config c2 = dma_channel_get_default_config(ADC_dma_chan2); //Get configurations for control channel
        channel_config_set_transfer_data_size(&c2, DMA_SIZE_32); //Set control channel data transfer size to 32 bits
        channel_config_set_read_increment(&c2, false); //Set control channel read increment to false
        channel_config_set_write_increment(&c2, false); //Set control channel write increment to false
        channel_config_set_dreq(&c2, 0x3F);
//                                channel_config_set_chain_to(&c2, dma_tx_chan); 
        dma_channel_configure(ADC_dma_chan2,
                &c2,
                &dma_hw->ch[ADC_dma_chan].al2_write_addr_trig,
                &adcint,
                1,
                false); //Configure control channel  
	    dma_channel_set_irq1_enabled(ADC_dma_chan, true);

	// set DMA IRQ handler
        irq_set_exclusive_handler(DMA_IRQ_1, ADCint);
	// set highest IRQ priority
        irq_set_enabled(DMA_IRQ_1, true);
        dma_start_channel_mask(1u << ADC_dma_chan2);
        adc_run(true);
        adcint=adcint2;
        ADCDualBuffering=false;
		return;
	}

	tp = checkstring(cmdline, (unsigned char *)"FREQUENCY");
	if(tp) {
        getargs(&tp,1,(unsigned char *)",");
        if(!ADCopen)error("Not open");
        float localfrequency=(float)getnumber(argv[0])*ADCopen;
        if(localfrequency<ADC_CLK_SPEED/65536.0/96.0 || localfrequency> ADC_CLK_SPEED/96.0)error("Invalid frequency");
        frequency=localfrequency;
		return;
	}
	tp = checkstring(cmdline, (unsigned char *)"START");
	if(tp) {
        getargs(&tp, 23, (unsigned char *)",");
		if(!ADCopen)error("ADC not open");
        if(!(argc >= 1))error("Argument count");
        a1float=NULL; a2float=NULL; a3float=NULL; a4float=NULL;
        ADCmax=0;
        ADCpos=0;
        int card;
        MMFLOAT top;
        for(int i=0;i<4;i++){
            ADCscale[i]=VCC/4095.0;
            ADCbottom[i]=0;
        }
        ADCmax=parsefloatrarray(argv[0], (MMFLOAT **)&a1float, 1, 1, NULL, true);
        if(argc>=3 && *argv[2]){
            if(ADCopen<2)error("Second channel not open");
            card=parsefloatrarray(argv[2], (MMFLOAT **)&a2float, 2, 1, NULL, true);
            if(card!=ADCmax)error("Array size mismatch");
        }
        if(argc>=5 && *argv[4]){
           if(ADCopen<3)error("Third channel not open");
            card=parsefloatrarray(argv[4], (MMFLOAT **)&a3float, 3, 1, NULL, true);
            if(card!=ADCmax)error("Array size mismatch");
        }
        if(argc>=7 && *argv[6]){
           if(ADCopen<4)error("Fourth channel not open");
            card=parsefloatrarray(argv[6], (MMFLOAT **)&a4float, 4, 1, NULL, true);
            if(card!=ADCmax)error("Array size mismatch");
        }
        if(argc>=11){
            ADCbottom[0]=getnumber(argv[8]);
            top=getnumber(argv[10]);
            ADCscale[0]=(top-ADCbottom[0])/4095.0;
        }
        if(argc>=15){
            ADCbottom[1]=getnumber(argv[12]);
            top=getnumber(argv[14]);
            ADCscale[1]=(top-ADCbottom[1])/4095.0;
        }
        if(argc>=19){
            ADCbottom[2]=getnumber(argv[16]);
            top=getnumber(argv[18]);
            ADCscale[2]=(top-ADCbottom[2])/4095.0;
        }
        if(argc>=23){
            ADCbottom[3]=getnumber(argv[20]);
            top=getnumber(argv[22]);
            ADCscale[3]=(top-ADCbottom[3])/4095.0;
        }
        ADCmax++;
        ADCbuffer=GetMemory(ADCmax*ADCopen*2);
        adc_init();
        adc_set_round_robin(ADCopen==1 ? 1 : ADCopen==2 ? 3 : ADCopen==3 ? 7 : 15);
        adc_fifo_setup(
            true,    // Write each completed conversion to the sample FIFO
            true,    // Enable DMA data request (DREQ)
            1,       // DREQ (and IRQ) asserted when at least 1 sample present
            false,   // We won't see the ERR bit because of 8 bit reads; disable.
            false     // Shift each sample to 8 bits when pushing to FIFO
        );
        SetADCFreq(frequency);
        // Set up the DMA to start transferring data as soon as it appears in FIFO
        dma_channel_config cfg = dma_channel_get_default_config(ADC_dma_chan);

        // Reading from constant address, writing to incrementing byte addresses
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
        channel_config_set_read_increment(&cfg, false);
        channel_config_set_write_increment(&cfg, true);
        channel_config_set_irq_quiet(&cfg, true);

        // Pace transfers based on availability of ADC samples
        channel_config_set_dreq(&cfg, DREQ_ADC);

        dma_channel_configure(ADC_dma_chan, &cfg,
            (uint8_t *)ADCbuffer,    // dst
            &adc_hw->fifo,  // src
            ADCmax*ADCopen,  // transfer count
            true            // start immediately
        );
        adc_run(true);

        // Once DMA finishes, stop any new conversions from starting, and clean up
        // the FIFO in case the ADC was still mid-conversion.
        if(!ADCInterrupt){
            while(dma_channel_is_busy(ADC_dma_chan))tight_loop_contents();
            __compiler_memory_barrier();
            adc_run(false);
            adc_fifo_drain();
            int k=0;
            for(int i=0;i<ADCmax;i++){
                for(int j=0;j<ADCopen;j++){
                    if(j==0)*a1float++ = (MMFLOAT)ADCbuffer[k++]*ADCscale[0]+ADCbottom[0];
                    if(j==1)*a2float++ = (MMFLOAT)ADCbuffer[k++]*ADCscale[1]+ADCbottom[1];
                    if(j==2)*a3float++ = (MMFLOAT)ADCbuffer[k++]*ADCscale[2]+ADCbottom[2];
                    if(j==3)*a4float++ = (MMFLOAT)ADCbuffer[k++]*ADCscale[3]+ADCbottom[3];
                }
            }
            FreeMemory((void *)ADCbuffer);
            adc_init();
            last_adc=99;
        } else dmarunning=true;
		return;
	}
	tp = checkstring(cmdline, (unsigned char *)"CLOSE");
	if(tp) {
        if(!ADCopen)error("Not open");
        irq_set_enabled(DMA_IRQ_1, false);
        dma_hw->abort = ((1u << ADC_dma_chan2) | (1u << ADC_dma_chan));
        dma_channel_abort(ADC_dma_chan);
        dma_channel_abort(ADC_dma_chan2);
        adc_set_round_robin(0);
        SetADCFreq(500000);
        #ifdef rp2350
if(rp2350a){
        ExtCfg(31, EXT_NOT_CONFIG, 0);
        if(ADCopen>=2)ExtCfg(32, EXT_NOT_CONFIG, 0);
        if(ADCopen>=3)ExtCfg(34, EXT_NOT_CONFIG, 0);
        if(ADCopen>=4)ExtCfg(44, EXT_NOT_CONFIG, 0);
} else {
        ExtCfg(55, EXT_NOT_CONFIG, 0);
        if(ADCopen>=2)ExtCfg(56, EXT_NOT_CONFIG, 0);
        if(ADCopen>=3)ExtCfg(57, EXT_NOT_CONFIG, 0);
        if(ADCopen>=4)ExtCfg(58, EXT_NOT_CONFIG, 0);
}
#else
        ExtCfg(31, EXT_NOT_CONFIG, 0);
        if(ADCopen>=2)ExtCfg(32, EXT_NOT_CONFIG, 0);
        if(ADCopen>=3)ExtCfg(34, EXT_NOT_CONFIG, 0);
        if(ADCopen>=4)ExtCfg(44, EXT_NOT_CONFIG, 0);
#endif
        ADCopen=0;
        adcint=adcint1=adcint2=NULL;
        ADCDualBuffering=false;
        dmarunning=false;
        last_adc=99;
        adc_init();
		return;
	}
    error("Syntax");
}
void SetADCFreq(float frequency){
    //Our ADC clock is running at ADC_CLK_SPEED (in Hz) so we need to stretch the time to produce the required frequency
    //The time delta for our frequency is 1/frequency*96 - 1/(ADC_CLK_SPEED) seconds
    //This must be added to clk_div as a number of CPU clock ticks i.e. delta/(1/ADC_CLK_SPEED)
    //Plus we need to add 95 to cater for the actual time taken for the conversion
    double delta = 1.0/(frequency*96.0) - 1.0/((double)ADC_CLK_SPEED);
    float div=delta/(1.0/((double)ADC_CLK_SPEED))*96.0 +95.0;
    if(div<=96.0)div=0;
    adc_set_clkdiv(div);
}

/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
void MIPS16 ClearExternalIO(void) {
    int i;
  	CloseAudio(1);
	#ifndef PICOMITEVGA
        cameraclose();
    #endif
    InterruptUsed = false;
	InterruptReturn = NULL;
    irq_set_enabled(DMA_IRQ_1, false);
#ifdef rp2350
    irq_set_enabled(PWM_IRQ_WRAP_1, false);
#endif
    closeframebuffer('A');
    if(CallBackEnabled==1) gpio_set_irq_enabled_with_callback(PinDef[IRpin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
    else if(CallBackEnabled & 1){
        gpio_set_irq_enabled(PinDef[IRpin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        CallBackEnabled &= (~1);
    }
    if(CallBackEnabled==2) gpio_set_irq_enabled_with_callback(PinDef[Option.INT1pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
    else if(CallBackEnabled & 2){
        gpio_set_irq_enabled(PinDef[Option.INT1pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        CallBackEnabled &= (~2);
    }
    if(CallBackEnabled==4) gpio_set_irq_enabled_with_callback(PinDef[Option.INT2pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
    else if(CallBackEnabled & 4){
        gpio_set_irq_enabled(PinDef[Option.INT2pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        CallBackEnabled &= (~4);
    }
    if(CallBackEnabled==8) gpio_set_irq_enabled_with_callback(PinDef[Option.INT3pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
    else  if(CallBackEnabled & 8){
        gpio_set_irq_enabled(PinDef[Option.INT3pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        CallBackEnabled &= (~8);
    }
    if(CallBackEnabled==16) gpio_set_irq_enabled_with_callback(PinDef[Option.INT4pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
    else  if(CallBackEnabled & 16){
        gpio_set_irq_enabled(PinDef[Option.INT4pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        CallBackEnabled &= (~16);
    }
    CallBackEnabled &= (~32);
    for(i=0;i<MAXBLITBUF;i++){
    	spritebuff[i].spritebuffptr = NULL;
    	blitbuff[i].blitbuffptr = NULL;
    }
    CallBackEnabled=0;
    KeypadClose();
    if(*lcd_pins){
        for(i = 0; i < 6; i++) {
          ExtCfg(lcd_pins[i], EXT_NOT_CONFIG, 0);                   // all set to unconfigured
            *lcd_pins = 0;
        }
    }
    memset(lcd_pins,0,sizeof(lcd_pins));
    SerialClose(1); 
    SerialClose(2);                                 // same for serial ports
    if(!I2C0locked)i2c_disable();   
    if(!I2C1locked)i2c2_disable();   
    sprite_transparent=0;
    SPIClose();
    SPI2Close();
    if(IRpin!=99){
        gpio_set_irq_enabled_with_callback(PinDef[IRpin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
        IrInterrupt = NULL;
        ExtCfg(IRpin, EXT_NOT_CONFIG, 0);
    }
    ResetDisplay();
    IrState = IR_CLOSED;
    IrInterrupt = NULL;
    IrGotMsg = false;
    memset(&PIDchannels,0,sizeof(s_PIDchan)*(MAXPID+1));
#ifdef rp2350
#ifdef PICOMITEWEB
	for(i = 1; i < (NBRPINS) ; i++) {
		if(CheckPin(i, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) {    // don't reset invalid or boot reserved pins
          ExtCfg(i, EXT_NOT_CONFIG, 0);                                       // all set to unconfigured
		}
	}
#else
	for(i = 1; i < (rp2350a ? 44: NBRPINS) ; i++) {
		if(CheckPin(i, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) {    // don't reset invalid or boot reserved pins
          ExtCfg(i, EXT_NOT_CONFIG, 0);                                       // all set to unconfigured
		}
	}
#endif
#else
	for(i = 1; i < (NBRPINS) ; i++) {
		if(CheckPin(i, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) {    // don't reset invalid or boot reserved pins
          ExtCfg(i, EXT_NOT_CONFIG, 0);                                       // all set to unconfigured
		}
	}
#endif
	for(i = 0; i < NBRINTERRUPTS; i++) {
      inttbl[i].pin = 0;                                            // disable all interrupts
  	}
    #ifndef PICOMITEWEB
    if(!Option.AllPins){
        if(CheckPin(41, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))ExtCfg(41,EXT_DIG_OUT,Option.PWM);
        if(CheckPin(42, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))ExtCfg(42,EXT_DIG_IN,0);
        if(CheckPin(44, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))ExtCfg(44,EXT_ANA_IN,0);
    }
    if(CheckPin(HEARTBEATpin, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED) && !Option.NoHeartbeat){
        gpio_init(PinDef[HEARTBEATpin].GPno);
        gpio_set_dir(PinDef[HEARTBEATpin].GPno, GPIO_OUT);
        ExtCurrentConfig[PinDef[HEARTBEATpin].pin]=EXT_HEARTBEAT;
    }
    #endif
    FreeMemorySafe((void **)&ds18b20Timers);
    KeypadInterrupt = NULL;

    for(i = 0; i < NBRSETTICKS; i++) TickInt[i] = NULL;
    for(i = 0; i < NBRSETTICKS; i++) TickActive[i] = 0;
	for(i = 0; i < NBR_PULSE_SLOTS; i++) PulseCnt[i] = 0;             // disable any pending pulse commands
    PulseActive = false;
#ifdef rp2350
    slice0=0;slice1=0;slice2=0;slice3=0;slice4=0;slice5=0;slice6=0;slice7=0;slice8=0;slice9=0;slice10=0;slice11=0;
#ifdef PICOMITE
    for(i=0; i<=(rp2350a ? 7:11);i++)if(!(i==Option.AUDIO_SLICE || i==BacklightSlice || i==KeyboardlightSlice))PWMoff(i);
#else
    for(i=0; i<=(rp2350a ? 7:11);i++)if(!(i==Option.AUDIO_SLICE || i==BacklightSlice))PWMoff(i);
#endif
#else
    slice0=0;slice1=0;slice2=0;slice3=0;slice4=0;slice5=0;slice6=0;slice7=0;
    for(i=0; i<=7;i++)if(!(i==Option.AUDIO_SLICE || i==BacklightSlice))PWMoff(i);
#endif
    IRpin=99;
    PWM0Apin=99;
    PWM1Apin=99;
    PWM2Apin=99;
    PWM3Apin=99;
    PWM4Apin=99;
    PWM5Apin=99;
    PWM6Apin=99;
    PWM7Apin=99;
#ifdef rp2350
    PWM8Apin=99;
    PWM9Apin=99;
    PWM10Apin=99;
    PWM11Apin=99;
#endif
    PWM0Bpin=99;
    PWM1Bpin=99;
    PWM2Bpin=99;
    PWM3Bpin=99;
    PWM4Bpin=99;
    PWM5Bpin=99;
    PWM6Bpin=99;
    PWM7Bpin=99;
#ifdef rp2350
    PWM8Bpin=99;
    PWM9Bpin=99;
    PWM10Bpin=99;
    PWM11Bpin=99;
#endif
    UART1RXpin=99;
    UART1TXpin=99;
    UART0TXpin=99;
    UART0RXpin=99;
    SPI0TXpin=99;
    SPI0RXpin=99;
    SPI0SCKpin=99;
    SPI1TXpin=99;
    SPI1RXpin=99;
    SPI1SCKpin=99;
    if(!I2C0locked){
        I2C0SDApin=99;
        I2C0SCLpin=99;
    }
    if(!I2C1locked){
        I2C1SDApin=99;
        I2C1SCLpin=99;	
    }
#ifdef rp2350
if(rp2350a){
    if(ADCopen) ExtCfg(31, EXT_NOT_CONFIG, 0);
    if(ADCopen>=2)ExtCfg(32, EXT_NOT_CONFIG, 0);
    if(ADCopen>=3)ExtCfg(34, EXT_NOT_CONFIG, 0);
    if(ADCopen>=4)ExtCfg(44, EXT_NOT_CONFIG, 0);
} else {
    if(ADCopen) ExtCfg(55, EXT_NOT_CONFIG, 0);
    if(ADCopen>=2)ExtCfg(56, EXT_NOT_CONFIG, 0);
    if(ADCopen>=3)ExtCfg(57, EXT_NOT_CONFIG, 0);
    if(ADCopen>=4)ExtCfg(58, EXT_NOT_CONFIG, 0);
}
#else
    if(ADCopen) ExtCfg(31, EXT_NOT_CONFIG, 0);
    if(ADCopen>=2)ExtCfg(32, EXT_NOT_CONFIG, 0);
    if(ADCopen>=3)ExtCfg(34, EXT_NOT_CONFIG, 0);
    if(ADCopen>=4)ExtCfg(44, EXT_NOT_CONFIG, 0);
#endif
    ADCopen=0;
    adc_set_round_robin(0);
    SetADCFreq(500000);
    KeyInterrupt=NULL;
    OnKeyGOSUB=NULL;
#ifndef USBKEYBOARD
    OnPS2GOSUB=NULL;
    PS2code=0;
    PS2int=false;
    if(!Option.MOUSE_CLOCK)mouse0close();
#endif 
    for (int i=0; i<6;i++)nunInterruptc[i]=NULL;
    if((classic1 || nunchuck1) && (classicread || nunchuckread))WiiReceive(6, (char *)nunbuff);
    classic1=0;
    nunchuck1=0;
    classicread=false;
    nunchuckread=false;
#ifdef PICOMITEWEB
    MQTTInterrupt=NULL;
    MQTTComplete=0;
    closeMQTT();
#endif
#ifndef PICOMITEWEB
#ifdef PICOMITEVGA
    CollisionFound = false;
    COLLISIONInterrupt=NULL;
#endif
#endif
#ifdef GUICONTROLS
    gui_int_down = false;
    gui_int_up = false;
    GuiIntDownVector=NULL;
    GuiIntUpVector=NULL;
#endif
    dmarunning=false;
    ADCDualBuffering=false;
    ADCInterrupt=NULL;
    CSubInterrupt=NULL;
    CSubComplete=0;
    keyselect=0;
    g_myrand=NULL;
    CMM1=0;
    dirOK=2;
    nextline[0]=0;nextline[1]=0;nextline[2]=0;nextline[3]=99;
    memset(pioTXlast,0,sizeof(pioTXlast));
    memset(pioRXinterrupts,0,sizeof(pioRXinterrupts));
    memset(pioTXinterrupts,0,sizeof(pioTXinterrupts));
    piointerrupt=0;
    DMAinterruptRX=NULL;
    DMAinterruptTX=NULL;
    WAVInterrupt=NULL;
    dma_hw->abort = ((1u << dma_rx_chan2) | (1u << dma_rx_chan));
    if(dma_channel_is_busy(dma_rx_chan))dma_channel_abort(dma_rx_chan);
    if(dma_channel_is_busy(dma_rx_chan2))dma_channel_abort(dma_rx_chan2);
//    dma_channel_cleanup(dma_rx_chan);
//    dma_channel_cleanup(dma_rx_chan2);
    dma_hw->abort = ((1u << dma_tx_chan2) | (1u << dma_tx_chan));
    if(dma_channel_is_busy(dma_tx_chan))dma_channel_abort(dma_tx_chan);
    if(dma_channel_is_busy(dma_tx_chan2))dma_channel_abort(dma_tx_chan2);
//    dma_channel_cleanup(dma_tx_chan);
//    dma_channel_cleanup(dma_tx_chan2);
    dma_hw->abort = ((1u << ADC_dma_chan2) | (1u << ADC_dma_chan));
    if(dma_channel_is_busy(ADC_dma_chan))dma_channel_abort(ADC_dma_chan);
    if(dma_channel_is_busy(ADC_dma_chan2))dma_channel_abort(ADC_dma_chan2);
//    dma_channel_cleanup(ADC_dma_chan);
//    dma_channel_cleanup(ADC_dma_chan2);
    adcint=adcint1=adcint2=NULL;
}



void __not_in_flash_func(TM_EXTI_Handler_1)(void) {
	if(ExtCurrentConfig[Option.INT1pin] == EXT_PER_IN) {
        if(--INT1Timer <= 0) {
            INT1Value = INT1Count;
            INT1Timer = INT1InitTimer;
            INT1Count = 0;
        }
	}
    else {
        if(CFuncInt1)
            CallCFuncInt1();                                        // Hardware interrupt 2 for a CFunction (see CFunction.c)
        else
            INT1Count++;
    }
}



// perform the counting functions for INT2
void __not_in_flash_func(TM_EXTI_Handler_2)(void) {
    if(ExtCurrentConfig[Option.INT2pin] == EXT_PER_IN) {
        if(--INT2Timer <= 0) {
            INT2Value = INT2Count;
            INT2Timer = INT2InitTimer;
            INT2Count = 0;
        }
    }
    else {
        if(CFuncInt2)
            CallCFuncInt2();                                        // Hardware interrupt 2 for a CFunction (see CFunction.c)
        else
            INT2Count++;
    }
}




// perform the counting functions for INT3
void __not_in_flash_func(TM_EXTI_Handler_3)(void) {
	if(ExtCurrentConfig[Option.INT3pin] == EXT_PER_IN) {
        if(--INT3Timer <= 0) {
            INT3Value = INT3Count;
            INT3Timer = INT3InitTimer;
            INT3Count = 0;
        }
	}
    else {
        if(CFuncInt3)
            CallCFuncInt3();                                        // Hardware interrupt 2 for a CFunction (see CFunction.c)
        else
            INT3Count++;
    }
}




// perform the counting functions for INT4
void __not_in_flash_func(TM_EXTI_Handler_4)(void) {
	if(ExtCurrentConfig[Option.INT4pin] == EXT_PER_IN) {
        if(--INT4Timer <= 0) {
            INT4Value = INT4Count;
            INT4Timer = INT4InitTimer;
            INT4Count = 0;
        }
	}
    else {
        if(CFuncInt4)
            CallCFuncInt4();                                        // Hardware interrupt 2 for a CFunction (see CFunction.c)
        else
            INT4Count++;
    }

}
void MIPS16 __not_in_flash_func(IRHandler)(void) {
    int ElapsedMicroSec;
    static unsigned int LastIrBits;
        ElapsedMicroSec = readIRclock();
        switch(IrState) {
            case IR_WAIT_START:
                writeIRclock(0);                                           // reset the timer
                IrState = IR_WAIT_START_END;                        // wait for the end of the start bit
                break;
            case IR_WAIT_START_END:
                if(ElapsedMicroSec > 2000 && ElapsedMicroSec < 2800)
                    IrState = SONY_WAIT_BIT_START;                  // probably a Sony remote, now wait for the first data bit
                else if(ElapsedMicroSec > 8000 && ElapsedMicroSec < 10000)
                    IrState = NEC_WAIT_FIRST_BIT_START;             // probably an NEC remote, now wait for the first data bit
                else {
                    IrReset();                                      // the start bit was not valid
                    break;
                }
                IrCount = 0;                                        // count the bits in the message
                IrBits = 0;                                         // reset the bit accumulator
                writeIRclock(0);                                           // reset the timer
                break;
            case SONY_WAIT_BIT_START:
                if(ElapsedMicroSec < 300 || ElapsedMicroSec > 900) { IrReset(); break; }
                writeIRclock(0);                                           // reset the timer
                IrState = SONY_WAIT_BIT_END;                         // wait for the end of this data bit
                break;
            case SONY_WAIT_BIT_END:
                if(ElapsedMicroSec < 300 || ElapsedMicroSec > 1500 || IrCount > 20) { IrReset(); break; }
                IrBits |= (ElapsedMicroSec > 900) << IrCount;       // get the data bit
                IrCount++;                                          // and increment our count
                writeIRclock(0);                                           // reset the timer
                IrState = SONY_WAIT_BIT_START;                       // go back and wait for the next data bit
                break;
            case NEC_WAIT_FIRST_BIT_START:
            	if(ElapsedMicroSec > 2000 && ElapsedMicroSec < 2500) {
                    IrBits = LastIrBits;                            // key is held down so just repeat the last code
                    IrCount = 32;                                   // and signal that we are finished
                    IrState = NEC_WAIT_BIT_END;
                    break;
                }
                else if(ElapsedMicroSec > 4000 && ElapsedMicroSec < 5000)
                    IrState = NEC_WAIT_BIT_END;                     // wait for the end of this data bit
                else {
                    IrReset();                                      // the start bit was not valid
                    break;
                }
                writeIRclock(0);                                           // reset the timer
                break;
            case NEC_WAIT_BIT_START:
                if(ElapsedMicroSec < 400 || ElapsedMicroSec > 1800) { IrReset(); break; }
                IrBits |= (ElapsedMicroSec > 840) << (31 - IrCount);// get the data bit
                LastIrBits = IrBits;
                IrCount++;                                          // and increment our count
                writeIRclock(0);                                           // reset the timer
                IrState = NEC_WAIT_BIT_END;                         // wait for the end of this data bit
                break;
            case NEC_WAIT_BIT_END:
                if(ElapsedMicroSec < 400 || ElapsedMicroSec > 700) { IrReset(); break; }
                if(IrCount == 32) break;
                writeIRclock(0);                                           // reset the timer
                IrState = NEC_WAIT_BIT_START;                       // go back and wait for the next data bit
                break;
        }
    }
void __not_in_flash_func(gpio_callback)(uint gpio, uint32_t events) {
#ifndef USBKEYBOARD
    static uint64_t data;
    data=gpio_get_all64();
    if(Option.KEYBOARD_CLOCK) if( !(Option.KeyboardConfig == NO_KEYBOARD || Option.KeyboardConfig == CONFIG_I2C ) && gpio==PinDef[Option.KEYBOARD_CLOCK].GPno) CNInterrupt(data);
    if(MOUSE_CLOCK && gpio==PinDef[MOUSE_CLOCK].GPno)MNInterrupt(data);
#endif
    if(gpio==PinDef[IRpin].GPno)IRHandler();
    if(gpio==PinDef[Option.INT1pin].GPno)TM_EXTI_Handler_1();
    if(gpio==PinDef[Option.INT2pin].GPno)TM_EXTI_Handler_2();
    if(gpio==PinDef[Option.INT3pin].GPno)TM_EXTI_Handler_3();
    if(gpio==PinDef[Option.INT4pin].GPno)TM_EXTI_Handler_4();
}
/*  @endcond */

