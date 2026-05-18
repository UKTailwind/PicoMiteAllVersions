/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/*
 * Keyboard.h
 *
 *  Created on: 20 Apr 2019
 *      Author: Peter
 */

#ifndef PS2KEYBOARD_H_
#define PS2KEYBOARD_H_
/* *********************************************************************************************************************
MMBasic

Keyboard.h

Include file that contains the globals and defines for Keyboard.c in MMBasic.

Copyright 2011 - 2018 Geoff Graham.  All Rights Reserved.

This file and modified versions of this file are supplied to specific individuals or organisations under the following
provisions:

- This file, or any files that comprise the MMBasic source (modified or not), may not be distributed or copied to any other
  person or organisation without written permission.

- Object files (.o and .hex files) generated using this file (modified or not) may not be distributed or copied to any other
  person or organisation without written permission.

- This file is provided in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

************************************************************************************************************************/


// global keyboard functions
extern void initKeyboard(void);
extern void CNInterrupt(uint64_t dd);
extern void MNInterrupt(uint64_t dd); 
extern void CheckKeyboard(void);
extern volatile int PS2code;
extern volatile bool PS2int;
// definition of the keyboard PS/2 state machine
#define PS2START    0
#define PS2BIT      1
#define PS2PARITY   2
#define PS2STOP     3
#define PS2COMMAND	4
#define PS2ERROR    9

// definitions related to setting the keyboard type
#define NO_KEYBOARD             0
#define CONFIG_US		1
#define CONFIG_FR		2
#define CONFIG_GR		3
#define CONFIG_IT		4
#define CONFIG_BE		5
#define CONFIG_UK		6
#define CONFIG_ES		7
#define CONFIG_BR		8
#define CONFIG_I2C  128
#endif /* PS2KEYBOARD_H_ */
/*  @endcond */
