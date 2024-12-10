/***********************************************************************************************************************
MMBasic

mouse.c

Handles the a few miscellaneous functions for the MX470 version.

Copyright 2016 - 2021 Peter Mather.  All Rights Reserved.

This file and modified versions of this file are supplied to specific individuals or organisations under the following
provisions:

- This file, or any files that comprise the MMBasic source (modified or not), may not be distributed or copied to any other
  person or organisation without written permission.

- Object files (.o and .hex files) generated using this file (modified or not) may not be distributed or copied to any other
  person or organisation without written permission.

- This file is provided in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

************************************************************************************************************************/


#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
char *mouse0Interruptc=NULL;
volatile int mouse0foundc=0;
bool mouse0=false;
int mouseID=0;
volatile int readreturn=-1;
static volatile int PS2State, KCount, KParity, runmode=0;
bool mouseupdated=false;
volatile unsigned char Code = 0;
int MOUSE_CLOCK,MOUSE_DATA;
volatile short mouse[4];
volatile unsigned int bno=0;
volatile unsigned char LastCode = 0;
void setstream(void);
void mouse_init();
static bool sendCommand(int cmd);
int ReadReturn(int timeout);
// definition of the mouse PS/2 state machine
#define PS2START    0
#define PS2BIT      1
#define PS2PARITY   2
#define PS2STOP     3
#define PS2ERROR    9
#define MDATA 1
#define MCLK 0
#define HIGH 1
#define LOW 0
#define MouseTimeout 500
void MouseKBDIntEnable(int status){
if (status)
  {
	PinSetBit(MOUSE_CLOCK, TRISSET);                                         // same for data
	PinSetBit(MOUSE_DATA, TRISSET);                                          // data low
    if (!CallBackEnabled)
    {
      CallBackEnabled = 64;
      gpio_set_irq_enabled_with_callback(PinDef[MOUSE_CLOCK].GPno, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    }
    else
    {
      CallBackEnabled |= 64;
      gpio_set_irq_enabled(PinDef[MOUSE_CLOCK].GPno, GPIO_IRQ_EDGE_FALL, true);
    }
  }
  else
  {
	PinSetBit(MOUSE_CLOCK, TRISSET);                                         // same for data
	PinSetBit(MOUSE_DATA, TRISSET);                                          // data low
    if (CallBackEnabled == 64){
		CallBackEnabled=0;
      	gpio_set_irq_enabled_with_callback(PinDef[MOUSE_CLOCK].GPno, GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
	} else {
      	gpio_set_irq_enabled(PinDef[MOUSE_CLOCK].GPno, GPIO_IRQ_EDGE_FALL, false);
    	CallBackEnabled &= (~64);
	}
	PS2State = PS2START;
  }
}
void mousecheck(int n){
	if(MouseTimer < MouseTimeout){
		routinechecks();
		return;
	}
    MouseKBDIntEnable(0);      											// disable interrupt in case called from within CNInterrupt()
	  runmode=0;
    mouse0=false;
    ExtCfg(MOUSE_CLOCK, EXT_NOT_CONFIG, 0);
    ExtCfg(MOUSE_DATA, EXT_NOT_CONFIG, 0);
    PS2State = PS2START;
    error("Mouse timeout % ",n);
}
/***************************************************************************************************
initMouse
Initialise the mouse routine.
****************************************************************************************************/
void initMouse0(int sensitivity) {
	if(!(MOUSE_CLOCK || Option.MOUSE_CLOCK))return;
  if(!Option.MOUSE_CLOCK){
    ExtCfg(MOUSE_CLOCK, EXT_COM_RESERVED, 0);
    ExtCfg(MOUSE_DATA, EXT_COM_RESERVED, 0);
  } else {
    MOUSE_CLOCK=Option.MOUSE_CLOCK;
    MOUSE_DATA=Option.MOUSE_DATA;
  }
	gpio_init(PinDef[MOUSE_CLOCK].GPno);
	gpio_set_pulls(PinDef[MOUSE_CLOCK].GPno,true,false);
	gpio_set_dir(PinDef[MOUSE_CLOCK].GPno, GPIO_IN);
	gpio_set_input_hysteresis_enabled(PinDef[MOUSE_CLOCK].GPno,true);
	gpio_init(PinDef[MOUSE_DATA].GPno);
	gpio_set_pulls(PinDef[MOUSE_DATA].GPno,true,false);
	gpio_set_dir(PinDef[MOUSE_DATA].GPno, GPIO_IN);
  gpio_set_drive_strength(PinDef[MOUSE_DATA].GPno,GPIO_DRIVE_STRENGTH_8MA);
  gpio_set_drive_strength(PinDef[MOUSE_CLOCK].GPno,GPIO_DRIVE_STRENGTH_8MA);
  int maxW=HRes;
	int maxH=VRes;
	mouseID=0;
	runmode=0;
    MouseKBDIntEnable(0);      											// disable interrupt in case called from within CNInterrupt()
	// enable pullups on the clock and data lines.
	// This stops them from floating and generating random chars when no mouse is attached

    // reserve the mouse pins
	if(!sendCommand(0xFF))return;                                              // Reset
	ReadReturn(500);
	sendCommand(0xF5);                                              // Turn off streaming
	ReadReturn(5);
	if(sensitivity){
		int scaling;
		if(sensitivity>4){
			scaling=1;
			sensitivity-=4;
		}
		sensitivity--;
		if(scaling){
		 	sendCommand(0xE7);                                              //
			ReadReturn(5);
		}
		sendCommand(0xE8);
		ReadReturn(5);
		sendCommand(sensitivity);
		ReadReturn(5);
	}
	sendCommand(0xF3);                                              //
	ReadReturn(5);
	sendCommand(200);                                              //
	ReadReturn(5);
	sendCommand(0xF3);                                              //
	ReadReturn(5);
	sendCommand(100);                                              //
	ReadReturn(5);
	sendCommand(0xF3);                                              //
	ReadReturn(5);
	sendCommand(80);                                              //
	ReadReturn(5);
	sendCommand(0xF2);                                              //
    mouseID=ReadReturn(10);
 	sendCommand(0xF3);                                              //
	ReadReturn(5);
	sendCommand(200);                                              //
	ReadReturn(5);
	sendCommand(0xF4);                                              // Turn on streaming
	ReadReturn(5);

    // setup Change Notification interrupt
    PS2State = PS2START;
	memset((struct s_nunstruct *)&nunstruct[2],0,sizeof(struct s_nunstruct));
    nunstruct[2].classic[0]=mouseID;
    nunstruct[2].type=0; //used for the double click timer
    nunstruct[2].ax=maxW/2;
    nunstruct[2].ay=maxH/2;
	runmode=1;
	Code = 0;
	bno=0;
	LastCode = 0;
//	 __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_3);
    MouseKBDIntEnable(1);       										// enable interrupt
  mouse0=true;
}
void mouse0close(void){
    if(!mouse0)return;
    mouseID=0;
    runmode=0;
    sendCommand(0xFF);                                              // Turn off streaming
    ReadReturn(5);
    MouseKBDIntEnable(0);      											// disable interrupt in case called from within CNInterrupt()
	  ExtCfg(MOUSE_CLOCK, EXT_NOT_CONFIG, 0);
    ExtCfg(MOUSE_DATA, EXT_NOT_CONFIG, 0);
    mouse0=false;
  	runmode=0;
    mouse0Interruptc=NULL;
  	memset((struct s_nunstruct *)&nunstruct[2],0,sizeof(struct s_nunstruct));
}
static bool sendCommand(int cmd)
{
  int i, j;

  // calculate the parity and add to the command as the 9th bit
  for (j = i = 0; i < 8; i++)
    j += ((cmd >> i) & 1);
  cmd = (cmd & 0xff) | (((j + 1) & 1) << 8);
  PinSetBit(MOUSE_CLOCK, TRISCLR);
  PinSetBit(MOUSE_CLOCK, LATCLR);
  uSec(250);
  PinSetBit(MOUSE_DATA, TRISCLR);
  PinSetBit(MOUSE_DATA, LATCLR);
  PinSetBit(MOUSE_CLOCK, TRISSET);
  InkeyTimer = 0;
  uSec(2);
  while (PinRead(MOUSE_CLOCK))
    if (InkeyTimer >= 500)
    {         // wait for the keyboard to pull the clock low
      return false; // wait for the keyboard to pull the clock low
    }

  // send each bit including parity
  for (i = 0; i < 9; i++)
  {
    if (cmd & 1)
    {
      PinSetBit(MOUSE_DATA, LATSET);
    }
    else
    {
      PinSetBit(MOUSE_DATA, LATCLR);
    }
    while (!PinRead(MOUSE_CLOCK))
      if (InkeyTimer >= 500)
      {         // wait for the keyboard to pull the clock low
        return false; // wait for the keyboard to pull the clock low
      }
    while (PinRead(MOUSE_CLOCK))
      if (InkeyTimer >= 500)
      {         // wait for the keyboard to pull the clock low
        return false; // wait for the keyboard to pull the clock low
      }
    cmd >>= 1;
  }

  //    PinSetBit(MOUSE_CLOCK, TRISSET);
  PinSetBit(MOUSE_DATA, TRISSET);

  while (PinRead(MOUSE_DATA))
    if (InkeyTimer >= 500)
    {         // wait for the keyboard to pull the clock low
      return false; // wait for the keyboard to pull the clock low
    }         // wait for the keyboard to pull data low (ACK)
  while (PinRead(MOUSE_CLOCK))
    if (InkeyTimer >= 500)
    {         // wait for the keyboard to pull the clock low
      return false; // wait for the keyboard to pull the clock low
    }         // wait for the clock to go low
  while (!(PinRead(MOUSE_CLOCK)) || !(PinRead(MOUSE_DATA)))
    if (InkeyTimer >= 500)
    {         // wait for the keyboard to pull the clock low
      return false; // wait for the keyboard to pull the clock low
    }
  return true;
}
/***************************************************************************************************
sendCommand - Send a command to to mouse.
****************************************************************************************************/

int ReadReturn(int timeout){
	 int i;
	 Code = 0;
	 bno=0;
	 LastCode = 0;
	 readreturn=-1;
	 MouseKBDIntEnable(1);
	 i=100;
	 MouseTimer = 0;
	 while(MouseTimer<timeout){
		 if(readreturn!=-1){
			 i=readreturn;
			 readreturn=-1;
			 routinechecks();
		 }
	 }
	 return i;
}


/***************************************************************************************************
change notification interrupt service routine
****************************************************************************************************/
void __not_in_flash_func(MNInterrupt)(uint64_t dd) {
	static unsigned long long int lefttimer=0, righttimer=0;
    int maxW=HRes;
	int maxH=VRes;
  int d = dd & (1<<PinDef[MOUSE_DATA].GPno);

  // Make sure it was a falling edge
  if (!(dd & (1<<PinDef[MOUSE_CLOCK].GPno)))
    {
    	if(!Timer5)PS2State=PS2START;
	    // Sample the data
        switch(PS2State){
            default:
            case PS2ERROR:                                          // this can happen if a timing or parity error occurs
                // fall through to PS2START

            case PS2START:
                if(!d) {                							// PS2DAT == 0
                    KCount = 8;         							// init bit counter
                    KParity = 0;        							// init parity check
                    Code = 0;
                    PS2State = PS2BIT;
        			Timer5=5;
                }
                break;

            case PS2BIT:
                Code >>= 1;            								// shift in data bit
                if(d) Code |= 0x80;                					// PS2DAT == 1
                KParity ^= Code;      								// calculate parity
                if (--KCount <= 0) PS2State = PS2PARITY;   			// all bit read
                break;

            case PS2PARITY:
                if(d) KParity ^= 0x80;                				// PS2DAT == 1
                if (KParity & 0x80)  {  								// parity odd, continue
                    PS2State = PS2STOP;
				        } else {
                    PS2State = PS2ERROR;
                }
                break;
            case PS2STOP:
                if(d) {                 							// PS2DAT == 1
                    readreturn=Code;
                    if(runmode){
                        mouse[bno++]=Code;
                        if(!(mouse[0] & 0x08))bno=0;//bit 3 must be set in first byte
                        if(bno==(mouseID==3 ? 4: 3)){
                            bno=0;
                            if(mouse[0] & 0b10000)mouse[1] |=0xFF00;
                            if(mouse[0] & 0b100000)mouse[2] |=0xFF00;
                            nunstruct[2].ax+=mouse[1];
                            if(nunstruct[2].ax<0)nunstruct[2].ax=0;
                            if(nunstruct[2].ax>=maxW)nunstruct[2].ax=maxW-1;
                            nunstruct[2].ay-=mouse[2];
                            mouseupdated=1;
                             if(nunstruct[2].ay<0)nunstruct[2].ay=0;
                            if(nunstruct[2].ay>=maxH)nunstruct[2].ay=maxH-1;
                            nunstruct[2].L = mouse[0] & 0b1;
                            nunstruct[2].R=(mouse[0] & 0b10)>>1;
                            nunstruct[2].C=(mouse[0] & 0b100)>>2;
                            if(nunstruct[2].type>1000) nunstruct[2].Z=0;
                            if((mouse[0] & 3) != (LastCode & 3)){
                            	if((mouse[0] & 1) && !(LastCode & 1) && (mSecTimer-lefttimer>16)){ //left button press
                            		nunfoundc[2]=1;
                            		if(nunstruct[2].type>=500 || nunstruct[2].type<100) nunstruct[2].type=0;
                            		else {
                            			nunstruct[2].Z=1;
                            			nunstruct[2].type=500 ;
                            		}
                            		lefttimer=mSecTimer;
                            	}
                               	if(!(mouse[0] & 1) && (LastCode & 1)){ //left button release
                               	}
                            	if((mouse[0] & 2) && !(LastCode & 2) && (mSecTimer-righttimer>16)){
                            		righttimer=mSecTimer;
                            	}
                            }
                            LastCode=mouse[0];
                            if(mouseID==3){
                            	if(mouse[3] & 0x80)mouse[3]|=0xFF00;
                            	nunstruct[2].az=(volatile int)nunstruct[2].az+mouse[3];
                            }
                            nunstruct[2].x1=nunstruct[2].ax/(FontTable[gui_font >> 4][0] * (gui_font & 0b1111));
                            nunstruct[2].y1=nunstruct[2].ay/(FontTable[gui_font >> 4][1] * (gui_font & 0b1111));
                       }
                    }
                }
                PS2State = PS2START;
                break;
	    }
	}
}
void cmd_mouse(void){
	unsigned char *tp=NULL;
  int n;
  if((tp=checkstring(cmdline, (unsigned char *)"OPEN"))){
      getargs(&tp,7,(unsigned char *)",");
//      if(Option.MOUSE_CLOCK)error("Already open");
      if(!(argc==5))error("Syntax");
      getint(argv[0],2,2);
      char code;
      if(!(code=codecheck(argv[2])))argv[2]+=2;
      int pin1 = getinteger(argv[2]);
      if(!code)pin1=codemap(pin1);
      if(!(code=codecheck(argv[4])))argv[4]+=2;
      int pin2 = getinteger(argv[4]);
      if(!code)pin2=codemap(pin2);
      if(IsInvalidPin(pin1)) error("Invalid pin %/|",pin1,pin1);
      if(IsInvalidPin(pin2)) error("Invalid pin %/|",pin2,pin2);
      if(!(pin1==Option.MOUSE_CLOCK && pin2==Option.MOUSE_DATA)){
        if(ExtCurrentConfig[pin1] >= EXT_COM_RESERVED )  error("Pin %/| is in use",pin1,pin1);
        if(ExtCurrentConfig[pin2] >= EXT_COM_RESERVED )  error("Pin %/| is in use",pin2,pin2);
      }
      if(Option.MOUSE_CLOCK && !(pin1==Option.MOUSE_CLOCK && pin2==Option.MOUSE_DATA))error("OPTION MOUSE declared with different pins");
      MOUSE_CLOCK=pin1;
      MOUSE_DATA=pin2;
      if(!mouse0)initMouse0(0);
      if(!mouse0){
        if(!Option.MOUSE_CLOCK){
          ExtCfg(MOUSE_CLOCK, EXT_NOT_CONFIG, 0);
          ExtCfg(MOUSE_DATA, EXT_NOT_CONFIG, 0);
        }
        MOUSE_CLOCK=0;
        MOUSE_DATA=0;
        error("Open failed");
      }
  } else  if((tp=checkstring(cmdline, (unsigned char *)"CLOSE"))){
      getargs(&tp,1,(unsigned char *)",");
      if(!mouse0)error("Not open");
      if(Option.MOUSE_CLOCK)error("Option MOUSE set - close invalid");
      n=getint(argv[0],2,2);
      mouse0close();
	} else 	if((tp=checkstring(cmdline,(unsigned char *)"INTERRUPT ENABLE"))){
      getargs(&tp,3,(unsigned char *)",");
      if(!mouse0)error("Not open");
      if(!(argc==3))error("Syntax");
      n=getint(argv[0],2,2);
      nunInterruptc[n] = (char *)GetIntAddress(argv[2]);					// get the interrupt location
      InterruptUsed = true;
      return;
	} else if((tp = checkstring(cmdline, (unsigned char *)"SET"))){
      getargs(&tp,7,(unsigned char *)",");
      if(!mouse0)error("Not open");
      if(!(argc==7))error("Syntax");
      n=getint(argv[0],2,2);
      nunstruct[n].ax=getint(argv[2],-HRes,HRes);
      nunstruct[n].ay=getint(argv[4],-VRes,VRes);
      nunstruct[n].az=getint(argv[6],-1000000,1000000); 
	} else if((tp = checkstring(cmdline, (unsigned char *)"INTERRUPT DISABLE"))){
      getargs(&tp,1,(unsigned char *)",");
      if(!mouse0)error("Not open");
      n=getint(argv[0],2,2);
      nunInterruptc[n]=NULL;
	} else error("Syntax");
}

