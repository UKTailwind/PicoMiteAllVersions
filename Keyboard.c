/*
 * Keyboard.c
 *
 *  Created on: 20 Apr 2019
 *      Author: Peter
 */

/***************************************************************************************************************************
MMBasic

keyboard.c

Does all the hard work in getting data from the PS2 keyboard

Copyright 2011 - 2018 Geoff Graham.  All Rights Reserved.

This file and modified versions of this file are supplied to specific individuals or organisations under the following
provisions:

- This file, or any files that comprise the MMBasic source (modified or not), may not be distributed or copied to any other
  person or organisation without written permission.

- Object files (.o and .hex files) generated using this file (modified or not) may not be distributed or copied to any other
  person or organisation without written permission.

- This file is provided in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

****************************************************************************************************************************

This routine is based on a technique and code presented by Lucio Di Jasio in his excellent book
"Programming 32-bit Microcontrollers in C - Exploring the PIC32".

Thanks to Muller Fabrice (France), Alberto Leibovich (Argentina) and the other contributors who provided the code for
the non US keyboard layouts

****************************************************************************************************************************/

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
void setLEDs(int num, int caps, int scroll);
bool sendCommand(int cmd, int clock, int data);
volatile char CapsLock;
volatile char NumLock;
volatile int setleds = 0;
// PS2 KBD state machine and buffer
static volatile int PS2State;
unsigned char KBDBuf;
static int KCount, KParity;
extern volatile int ConsoleRxBufHead;
extern volatile int ConsoleRxBufTail;
int justset = 0;
// extern char ConsoleRxBuf[];

volatile int KeyDownRegister;
volatile int KeyDownCode;
volatile int PS2code=0;
volatile bool PS2int=false;
// key codes that must be tracked for up/down state
#define CTRL 0x14 // left and right generate the same code
#define L_SHFT 0x12
#define R_SHFT 0x59
#define CAPS 0x58
#define NUML 0x77
  static char LShift = 0;
  static char RShift = 0;
  static char PS2Ctrl = 0;
  static char AltGrDown = 0;
  static char KeyUpCode = false;
  static char KeyE0 = false;

// this is a map of the keycode characters and the character to be returned for the keycode
const char keyCodes[8][128] =
    // US Layout
    {
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, '`', 0,         // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'q', '1', 0,    // 16-23    10-07
            0, 0, 'z', 's', 'a', 'w', '2', 0,        // 24-31    18-1F
            0, 'c', 'x', 'd', 'e', '4', '3', 0,      // 32-39    20-27
            0, ' ', 'v', 'f', 't', 'r', '5', 0,      // 40-48    28-2F
            0, 'n', 'b', 'h', 'g', 'y', '6', 0,      // 48-56    30-37
            0, 0, 'm', 'j', 'u', '7', '8', 0,        // 56-63    38-3F
            0, ',', 'k', 'i', 'o', '0', '9', 0,      // 64-71    40-47
            0, '.', '/', 'l', ';', 'p', '-', 0,      // 72-79    48-4F
            0, 0, '\'', 0, '[', '=', 0, 0,           // 80-87    50-57
            CAPS, R_SHFT, ENTER, ']', 0, 0x5c, 0, 0, // 88-95    58-5F
            0, 0, 0, 0, 0, 0, BKSP, 0,               // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // FR Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, 0, 0,           // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'a', '&', 0,    // 16-23    10-07
            0, 0, 'w', 's', 'q', 'z', 0, 0,          // 24-31    18-1F
            0, 'c', 'x', 'd', 'e', '\'', '"', 0,     // 32-39    20-27
            0, ' ', 'v', 'f', 't', 'r', '(', 0,      // 40-48    28-2F
            0, 'n', 'b', 'h', 'g', 'y', '-', 0,      // 48-56    30-37
            0, 0, ',', 'j', 'u', 0, '_', 0,          // 56-63    38-3F
            0, ';', 'k', 'i', 'o', 0, 0, 0,          // 64-71    40-47
            0, ':', '!', 'l', 'm', 'p', ')', 0,      // 72-79    48-4F
            0, 0, 0, 0, '^', '=', 0, 0,              // 80-87    50-57
            CAPS, R_SHFT, ENTER, '$', 0, '*', 0, 0,  // 88-95    58-5F
            0, '<', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // GR Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, '^', 0,         // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'q', '1', 0,    // 16-23    10-07
            0, 0, 'y', 's', 'a', 'w', '2', 0,        // 24-31    18-1F
            0, 'c', 'x', 'd', 'e', '4', '3', 0,      // 32-39    20-27
            0, ' ', 'v', 'f', 't', 'r', '5', 0,      // 40-47    28-2F
            0, 'n', 'b', 'h', 'g', 'z', '6', 0,      // 48-55    30-37
            0, 0, 'm', 'j', 'u', '7', '8', 0,        // 56-63    38-3F
            0, ',', 'k', 'i', 'o', '0', '9', 0,      // 64-71    40-47
            0, '.', '-', 'l', 0, 'p', 0, 0,          // 72-79    48-4F
            0, 0, 0, 0, 0, '\'', 0, 0,               // 80-87    50-57
            CAPS, R_SHFT, ENTER, '+', 0, '#', 0, 0,  // 88-95    58-5F
            0, '<', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // IT Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, 0x5c, 0,        // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'q', '1', 0,    // 16-23    10-07
            0, 0, 'z', 's', 'a', 'w', '2', 0,        // 24-31    18-1F
            0, 'c', 'x', 'd', 'e', '4', '3', 0,      // 32-39    20-27
            0, ' ', 'v', 'f', 't', 'r', '5', 0,      // 40-48    28-2F
            0, 'n', 'b', 'h', 'g', 'y', '6', 0,      // 48-56    30-37
            0, 0, 'm', 'j', 'u', '7', '8', 0,        // 56-63    38-3F
            0, ',', 'k', 'i', 'o', '0', '9', 0,      // 64-71    40-47
            0, '.', '-', 'l', 0, 'p', '\'', 0,       // 72-79    48-4F
            0, 0, 0, 0, 0, 0, 0, 0,                  // 80-87    50-57
            CAPS, R_SHFT, ENTER, '+', 0, 0, 0, 0,    // 88-95    58-5F
            0, '<', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // BE Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, 0, 0,           // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'a', '&', 0,    // 16-23    10-07
            0, 0, 'w', 's', 'q', 'z', 0, 0,          // 24-31    18-1F
            0, 'c', 'x', 'd', 'e', '\'', '"', 0,     // 32-39    20-27
            0, ' ', 'v', 'f', 't', 'r', '(', 0,      // 40-48    28-2F
            0, 'n', 'b', 'h', 'g', 'y', 0, 0,        // 48-56    30-37
            0, 0, ',', 'j', 'u', 0, '!', 0,          // 56-63    38-3F
            0, ';', 'k', 'i', 'o', 0, 0, 0,          // 64-71    40-47
            0, ':', '=', 'l', 'm', 'p', ')', 0,      // 72-79    48-4F
            0, 0, 0, 0, '^', '-', 0, 0,              // 80-87    50-57
            CAPS, R_SHFT, ENTER, '$', 0, 0, 0, 0,    // 88-95    58-5F
            0, '<', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // UK Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,            // 00-07    00-07
            0, F10, F8, F6, F4, TAB, '`', 0,          // 08-15    08-0F
            0, ALT, L_SHFT, 0x5C, CTRL, 'q', '1', 0,  // 16-23    10-07
            0, 0, 'z', 's', 'a', 'w', '2', 0,         // 24-31    18-1F
            0, 'c', 'x', 'd', 'e', '4', '3', 0,       // 32-39    20-27
            0, ' ', 'v', 'f', 't', 'r', '5', 0,       // 40-48    28-2F
            0, 'n', 'b', 'h', 'g', 'y', '6', 0,       // 48-56    30-37
            0, 0, 'm', 'j', 'u', '7', '8', 0,         // 56-63    38-3F
            0, ',', 'k', 'i', 'o', '0', '9', 0,       // 64-71    40-47
            0, '.', '/', 'l', ';', 'p', '-', 0,       // 72-79    48-4F
            0, 0, '\'', 0, '[', '=', 0x5C, 0,         // 80-87    50-57
            CAPS, R_SHFT, ENTER, ']', '#', '#', 0, 0, // 88-95    58-5F
            0, 0x5C, 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,             // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML,  // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0        // 120-127  78-7F
        },
        // ES Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, 0x5c, 0,        // 08-15	   08-0F	  0x5C is backslash
            0, ALT, L_SHFT, 0, CTRL, 'q', '1', 0,    // 16-23    10-07
            0, 0, 'z', 's', 'a', 'w', '2', 0,        // 24-31    18-1F
            0, 'c', 'x', 'd', 'e', '4', '3', 0,      // 32-39    20-27
            0, ' ', 'v', 'f', 't', 'r', '5', 0,      // 40-47    28-2F
            0, 'n', 'b', 'h', 'g', 'y', '6', 0,      // 48-55    30-37
            0, 0, 'm', 'j', 'u', '7', '8', 0,        // 56-63    38-3F
            0, ',', 'k', 'i', 'o', '0', '9', 0,      // 64-71    40-47
            0, '.', '-', 'l', 0, 'p', '\'', 0,       // 72-79	   48-4F
            0, 0, '\'', 0, 0x60, 0, 0, 0,            // 80-87    50-57	  0x60 a single quote
            CAPS, R_SHFT, ENTER, '+', 0, 0, 0, 0,    // 88-95    58-5F
            0, '<', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // BR Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, '\'', 0,        // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'q', '1', 0,    // 16-23    10-17
            0, 0, 'z', 's', 'a', 'w', '2', 0,        // 24-31    18-1F
            0, 'c', 'x', 'd', 'e', '4', '3', 0,      // 32-39    20-27
            0, ' ', 'v', 'f', 't', 'r', '5', 0,      // 40-48    28-2F
            0, 'n', 'b', 'h', 'g', 'y', '6', 0,      // 48-56    30-37
            0, 0, 'm', 'j', 'u', '7', '8', 0,        // 56-63    38-3F
            0, ',', 'k', 'i', 'o', '0', '9', 0,      // 64-71    40-47
            0, '.', ';', 'l', 'c', 'p', '-', 0,      // 72-79    48-4F
            0, '/', '~', 0, 0, '=', 0, 0,            // 80-87    50-57
            CAPS, R_SHFT, ENTER, '[', 0, ']', 0, 0,  // 88-95    58-5F
            0, '\\', 0, 0, 0, 0, BKSP, 0,            // 96-103   60-67
            0, '1', 0, '4', '7', '.', 0, 0,          // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        }};

// this map is with the shift key pressed
const char keySCodes[8][128] =
    // US Layout
    {
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, '~', 0,         // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'Q', '!', 0,    // 16-23    10-17
            0, 0, 'Z', 'S', 'A', 'W', '@', 0,        // 24-31    18-1F
            0, 'C', 'X', 'D', 'E', '$', '#', 0,      // 32-39    20-27
            0, ' ', 'V', 'F', 'T', 'R', '%', 0,      // 40-47    28-2F
            0, 'N', 'B', 'H', 'G', 'Y', '^', 0,      // 48-55    30-37
            0, 0, 'M', 'J', 'U', '&', '*', 0,        // 56-63    38-3F
            0, '<', 'K', 'I', 'O', ')', '(', 0,      // 64-71    40-47
            0, '>', '?', 'L', ':', 'P', '_', 0,      // 72-79    48-4F
            0, 0, '\"', 0, '{', '+', 0, 0,           // 80-87    50-57
            CAPS, R_SHFT, ENTER, '}', 0, '|', 0, 0,  // 88-95    58-5F
            0, 0, 0, 0, 0, 0, BKSP, 0,               // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // FR Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, 0, 0,           // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'A', '1', 0,    // 16-23    10-17
            0, 0, 'W', 'S', 'Q', 'Z', '2', 0,        // 24-31    18-1F
            0, 'C', 'X', 'D', 'E', '4', '3', 0,      // 32-39    20-27
            0, ' ', 'V', 'F', 'T', 'R', '5', 0,      // 40-47    28-2F
            0, 'N', 'B', 'H', 'G', 'Y', '6', 0,      // 48-55    30-37
            0, 0, '?', 'J', 'U', '7', '8', 0,        // 56-63    38-3F
            0, '.', 'K', 'I', 'O', '0', '9', 0,      // 64-71    40-47
            0, '/', 0, 'L', 'M', 'P', 0, 0,          // 72-79    48-4F
            0, 0, '%', 0, 0, '+', 0, 0,              // 80-87    50-57
            CAPS, R_SHFT, ENTER, 0, 0, 0, 0, 0,      // 88-95    58-5F
            0, '>', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // GR Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, 0, 0,           // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'Q', '!', 0,    // 16-23    10-07
            0, 0, 'Y', 'S', 'A', 'W', '\"', 0,       // 24-31    18-1F
            0, 'C', 'X', 'D', 'E', '$', 0, 0,        // 32-39    20-27
            0, '\'', 'V', 'F', 'T', 'R', '%', 0,      // 40-47    28-2F
            0, 'N', 'B', 'H', 'G', 'Z', '&', 0,      // 48-55    30-37
            0, 0, 'M', 'J', 'U', '/', '(', 0,        // 56-63    38-3F
            0, ';', 'K', 'I', 'O', '=', ')', 0,      // 64-71    40-47
            0, ':', '_', 'L', 0, 'P', '?', 0,        // 72-79    48-4F
            0, 0, 0, 0, 0, 0, 0, 0,                  // 80-87    50-57
            CAPS, R_SHFT, ENTER, '*', 0, 0, 0, 0,    // 88-95    58-5F
            0, '>', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // IT Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, '|', 0,         // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'Q', '!', 0,    // 16-23    10-17
            0, 0, 'Z', 'S', 'A', 'W', '\"', 0,       // 24-31    18-1F
            0, 'C', 'X', 'D', 'E', '$', 0, 0,        // 32-39    20-27
            0, ' ', 'V', 'F', 'T', 'R', '%', 0,      // 40-47    28-2F
            0, 'N', 'B', 'H', 'G', 'Y', '&', 0,      // 48-55    30-37
            0, 0, 'M', 'J', 'U', '/', '(', 0,        // 56-63    38-3F
            0, ';', 'K', 'I', 'O', '=', ')', 0,      // 64-71    40-47
            0, ':', '_', 'L', 0, 'P', '?', 0,        // 72-79    48-4F
            0, 0, 0, 0, 0, '^', 0, 0,                // 80-87    50-57
            CAPS, R_SHFT, ENTER, '*', 0, 0, 0, 0,    // 88-95    58-5F
            0, '>', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // BE Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, 0, 0,           // 08-15    08-0F
            0, ALT, L_SHFT, 0, CTRL, 'A', '1', 0,    // 16-23    10-17
            0, 0, 'W', 'S', 'Q', 'Z', '2', 0,        // 24-31    18-1F
            0, 'C', 'X', 'D', 'E', '4', '3', 0,      // 32-39    20-27
            0, ' ', 'V', 'F', 'T', 'R', '5', 0,      // 40-47    28-2F
            0, 'N', 'B', 'H', 'G', 'Y', '6', 0,      // 48-55    30-37
            0, 0, '?', 'J', 'U', '7', '8', 0,        // 56-63    38-3F
            0, '.', 'K', 'I', 'O', '0', '9', 0,      // 64-71    40-47
            0, '/', '+', 'L', 'M', 'P', 0, 0,        // 72-79    48-4F
            0, 0, '%', 0, 0, '_', 0, 0,              // 80-87    50-57
            CAPS, R_SHFT, ENTER, '*', 0, 0, 0, 0,    // 88-95    58-5F
            0, '>', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // UK Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,            // 00-07    00-07
            0, F10, F8, F6, F4, TAB, '~', 0,          // 08-15    08-0F
            0, ALT, L_SHFT, '|', CTRL, 'Q', '!', 0,   // 16-23    10-17
            0, 0, 'Z', 'S', 'A', 'W', '\"', 0,        // 24-31    18-1F
            0, 'C', 'X', 'D', 'E', '$', '#', 0,       // 32-39    20-27
            0, ' ', 'V', 'F', 'T', 'R', '%', 0,       // 40-47    28-2F
            0, 'N', 'B', 'H', 'G', 'Y', '^', 0,       // 48-55    30-37
            0, 0, 'M', 'J', 'U', '&', '*', 0,         // 56-63    38-3F
            0, '<', 'K', 'I', 'O', ')', '(', 0,       // 64-71    40-47
            0, '>', '?', 'L', ':', 'P', '_', 0,       // 72-79    48-4F
            0, 0, '@', 0, '{', '+', '|', 0,           // 80-87    50-57
            CAPS, R_SHFT, ENTER, '}', '~', '~', 0, 0, // 88-95    58-5F
            0, '|', 0, 0, 0, 0, BKSP, 0,              // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,             // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML,  // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0        // 120-127  78-7F
        },
        // ES Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, 0x5C, 0,        // 08-15    08-0F	 0x5C is backslash
            0, ALT, L_SHFT, 0, CTRL, 'Q', '!', 0,    // 16-23    10-17
            0, 0, 'Z', 'S', 'A', 'W', '\"', 0,       // 24-31    18-1F
            0, 'C', 'X', 'D', 'E', '$', 0, 0,        // 32-39    20-27
            0, ' ', 'V', 'F', 'T', 'R', '%', 0,      // 40-47    28-2F
            0, 'N', 'B', 'H', 'G', 'Y', '&', 0,      // 48-55    30-37
            0, 0, 'M', 'J', 'U', '/', '(', 0,        // 56-63    38-3F
            0, ';', 'K', 'I', 'O', '=', ')', 0,      // 64-71    40-47
            0, ':', '_', 'L', 0, 'P', '?', 0,        // 72-79	   48-4F
            0, 0, 0, 0, '^', 0, 0, 0,                // 80-87    50-57
            CAPS, R_SHFT, ENTER, '*', 0, 0, 0, 0,    // 88-95    58-5F
            0, '>', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', 0, 0, 0,            // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        },
        // BR Layout
        {
            // Base 10   Hex
            0, F9, 0, F5, F3, F1, F2, F12,           // 00-07    00-07
            0, F10, F8, F6, F4, TAB, '\"', 0,        // 08-15    08-0F
            0, ALT, L_SHFT, CTRL, 'Q', 0, '!', 0,    // 16-23    10-17
            0, 0, 'Z', 'S', 'A', 'W', '@', 0,        // 24-31    18-1F
            0, 'C', 'X', 'D', 'E', '$', '#', 0,      // 32-39    20-27
            0, ' ', 'V', 'F', 'T', 'R', '%', 0,      // 40-47    28-2F
            0, 'N', 'B', 'H', 'G', 'Y', 0, 0,        // 48-55    30-37
            0, 0, 'M', 'J', 'U', '&', '*', 0,        // 56-63    38-3F
            0, '<', 'K', 'I', 'O', ')', '(', 0,      // 64-71    40-47
            0, '>', ':', 'L', 'C', 'P', '_', 0,      // 72-79    48-4F
            0, '?', '^', 0, 0, '+', 0, 0,            // 80-87    50-57
            CAPS, R_SHFT, ENTER, '{', 0, '}', 0, 0,  // 88-95    58-5F
            0, '|', 0, 0, 0, 0, BKSP, 0,             // 96-103   60-67
            0, '1', 0, '4', '7', '.', 0, 0,          // 104-111  68-6F
            '0', '.', '2', '5', '6', '8', ESC, NUML, // 112-119  70-77
            F11, '+', '3', '-', '*', '9', 0, 0       // 120-127  78-7F
        }};

// this map is for when the keycode preceeded by 0xe0
const char keyE0Codes[56] =
    // General Layout on all Keyboard for the Keypad
    {
        // Base 10
        0, END, 0, LEFT, HOME, 0, 0, 0,             // 104-111
        INSERT, DEL, DOWN, 0, RIGHT, UP, ESC, NUML, // 112-119
        F11, '+', PDOWN, '-', '*', PUP, SLOCK, 0,   // 120-127
};

const char keyE0Codes_ES[56] =
    // Layout for spanish Keyboard for the Keypad
    {
        // Base 10
        0, END, 0, LEFT, HOME, 0, 0, 0,               // 104-111
        INSERT, DEL, DOWN, PUP, RIGHT, UP, ESC, NUML, // 112-119
        F11, 0, PDOWN, 0, 0, 0, SLOCK, 0,             // 120-127
};

void KBDIntEnable(int status)
{
  PinSetBit(Option.KEYBOARD_CLOCK, TRISSET); // if tris = 1 then it is an input
  PinSetBit(Option.KEYBOARD_DATA, TRISSET);  // if tris = 1 then it is an input
  PinSetBit(Option.KEYBOARD_CLOCK, CNPUSET); // if tris = 1 then it is an input
  PinSetBit(Option.KEYBOARD_DATA, CNPUSET);  // if tris = 1 then it is an input
  if (status)
  {
    if (!CallBackEnabled)
    {
      CallBackEnabled = 32;
      gpio_set_irq_enabled_with_callback(PinDef[Option.KEYBOARD_CLOCK].GPno, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    }
    else
    {
      CallBackEnabled |= 32;
      gpio_set_irq_enabled(PinDef[Option.KEYBOARD_CLOCK].GPno, GPIO_IRQ_EDGE_FALL, true);
    }
  }
  else
  {
    if (CallBackEnabled == 32)
      gpio_set_irq_enabled_with_callback(PinDef[Option.KEYBOARD_CLOCK].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
    else
      gpio_set_irq_enabled(PinDef[Option.KEYBOARD_CLOCK].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
    CallBackEnabled &= (~32);
  }
}

/***************************************************************************************************
initKeyboard
Initialise the keyboard routine.
****************************************************************************************************/
void initKeyboard(void)
{
  //	GPIO_InitTypeDef GPIO_InitDef;

    if (Option.KeyboardConfig ==  NO_KEYBOARD || Option.KeyboardConfig==CONFIG_I2C) return;
    KBDIntEnable(0); // disable interrupt in case called from within CNInterrupt()

    // enable pullups on the clock and data lines.
    //	sendCommand(0xFF);
    //	HAL_Delay(100);
    // setup Change Notification interrupt
    KBDIntEnable(1); // enable interrupt
    PS2State = PS2START;
    CapsLock = Option.capslock;
    NumLock = Option.numlock;
    uSec(100000);
    setLEDs(CapsLock, NumLock, 0);
}

/***************************************************************************************************
sendCommand - Send a command to to keyboard.
****************************************************************************************************/
bool sendCommand(int cmd, int clock, int data)
{
  int i, j;

  // calculate the parity and add to the command as the 9th bit
  for (j = i = 0; i < 8; i++) j += ((cmd >> i) & 1);
  cmd = (cmd & 0xff) | (((j + 1) & 1) << 8);
  InkeyTimer = 0;
/*  while (!PinRead(clock)) //wait for clock high
    if (InkeyTimer >= 500)
    {         // wait for the keyboard to pull the clock low
      return false; // wait for the keyboard to pull the clock low
    }*/
  PinSetBit(clock, TRISCLR);
  PinSetBit(clock, LATCLR);
  uSec(250);
  PinSetBit(data, TRISCLR);
  PinSetBit(data, LATCLR);
  uSec(2);
  PinSetBit(clock, TRISSET);
  uSec(2);
  while (PinRead(clock))
    if (InkeyTimer >= 500)
    {         // wait for the keyboard to pull the clock low
      return false; // wait for the keyboard to pull the clock low
    }

  // send each bit including parity
  for (i = 0; i < 9; i++)
  {
    if (cmd & 1)
    {
      PinSetBit(data, LATSET);
    }
    else
    {
      PinSetBit(data, LATCLR);
    }
    while (!PinRead(clock))
      if (InkeyTimer >= 500)
      {         // wait for the keyboard to pull the clock low
        return false; // wait for the keyboard to pull the clock low
      }
    while (PinRead(clock))
      if (InkeyTimer >= 500)
      {         // wait for the keyboard to pull the clock low
        return false; // wait for the keyboard to pull the clock low
      }
    cmd >>= 1;
  }

  //    PinSetBit(clock, TRISSET);
  PinSetBit(data, TRISSET);

  while (PinRead(data))
    if (InkeyTimer >= 500)
    {         // wait for the keyboard to pull the clock low
      return false; // wait for the keyboard to pull the clock low
    }         // wait for the keyboard to pull data low (ACK)
  while (PinRead(clock))
    if (InkeyTimer >= 500)
    {         // wait for the keyboard to pull the clock low
      return false; // wait for the keyboard to pull the clock low
    }         // wait for the clock to go low
  while (!(PinRead(clock)) || !(PinRead(data)))
    if (InkeyTimer >= 500)
    {         // wait for the keyboard to pull the clock low
      return false; // wait for the keyboard to pull the clock low
    }
  return true;
}

// set the keyboard LEDs
void setLEDs(int caps, int num, int scroll)
{
  setleds = 0;
  KBDIntEnable(0); // disable interrupt while we play
  PS2State = PS2START;
  sendCommand(0xED, Option.KEYBOARD_CLOCK, Option.KEYBOARD_DATA); // Set/Reset Status Indicators Command
  uSec(50000);
  sendCommand(((caps & 1) << 2) | ((num & 1) << 1) | (scroll & 1), Option.KEYBOARD_CLOCK, Option.KEYBOARD_DATA); // set the various LEDs
  uSec(50000);
  sendCommand(0xF3, Option.KEYBOARD_CLOCK, Option.KEYBOARD_DATA); // Set/Reset Status Indicators Command
  uSec(50000);
  sendCommand(Option.repeat, Option.KEYBOARD_CLOCK, Option.KEYBOARD_DATA);
  KBDIntEnable(1); // re enable interrupt
  justset = 1;
}

void __not_in_flash_func(CheckKeyboard)(void)
{
  if (setleds)
  {
    setLEDs(CapsLock, NumLock, 0);
  }
}
void processcode(unsigned char Code){
  unsigned char c = 0;
  //	unsigned int dly;
        if(!(Code==0xe0 || Code==0xf0 || (Code==0x12 && KeyE0) || (Code==0x12 && KeyUpCode)) && Code){
          PS2int=true;
          PS2code=Code;
        }

        if(KeyUpCode)PS2code|=0xf000;
        if(KeyE0)PS2code|=0xe000;
        if(KeyE0 && KeyUpCode)PS2code|=0xe0f000;
        if (Code == 0xaa)
        { // self test code (a keyboard must have just been plugged in)
          CapsLock = 0;
          NumLock = 1;
        }                      // so initialise it
        else if (Code == 0xf0) // a key has been released
          KeyUpCode = true;
        else if (Code == 0xe0) // extended keycode prefix
          KeyE0 = true;
        else
        {
          // Process a scan code from the keyboard into an ASCII character.
          // It then inserts the char into the keyboard queue.

          // for the US keyboard and the right Alt key we need to make it the same as the left Alt key
          if (Option.KeyboardConfig == CONFIG_US && KeyE0 && Code == 0x11)
            KeyE0 = false;

          // if a key has been released we are only interested in resetting state keys
          if (KeyUpCode)
          {
            if (Code == L_SHFT)
              LShift = 0; // left shift button is released
            else if (Code == R_SHFT)
              RShift = 0; // right shift button is released
            else if (Code == CTRL)
              PS2Ctrl = 0; // left control button is released
            else if (KeyE0 && Code == 0x11)
              AltGrDown = 0; // release the AltGr key on non US keyboards
            else if (Code == KeyDownCode)
              KeyDownRegister = 0; // normal char so record that it is no longer depressed
            goto SkipOut;
          }

          // we are only here if the key has been pressed (NOT released)
          if (Code == L_SHFT)
          {
            LShift = 1;
            goto SkipOut;
          } // left shift button is pressed
          if (Code == R_SHFT)
          {
            RShift = 1;
            goto SkipOut;
          } // right shift button is pressed
          if (Code == CTRL)
          {
            PS2Ctrl = 1;
            goto SkipOut;
          } // left control button is pressed
          if (Code == CAPS)
          {
            CapsLock = !CapsLock;
            setleds = 1;
            //                            setLEDs(CapsLock, NumLock, 3);
            goto SkipOut;
          }
          if (Code == NUML)
          { // caps or num lock pressed
            NumLock = !NumLock;
            setleds = 1;
            //                            setLEDs(CapsLock, NumLock, 7);
            goto SkipOut;
          }
          if (KeyE0 && Code == 0x11)
          {
            AltGrDown = 1;
            goto SkipOut;
          } // AltGr key pressed on non US Keyboard

          // now get the character into c.  Why, oh why, are scan codes so random?
          if (!KeyE0 && Code == 0x83)
            c = 0x97; // a special case, function key F7
          else if (KeyE0 && Code == 0x4A)
            c = '/'; // another special case, this time the keypad forward slash
          else if (KeyE0 && Code == 0x5A)
            c = NUM_ENT; // yet another special case, this time the keypad enter key
          else if ((KeyE0 || !NumLock) && Code >= 104 && Code < 0x80 && !AltGrDown)
          {             // a keycode from the numeric keypad
            LShift = 0; // when num lock LED is on codes are preceeded by left shift
            if (Option.KeyboardConfig == CONFIG_ES)
              c = keyE0Codes_ES[Code - 104];
            else
              c = keyE0Codes[Code - 104];
            if (PS2Ctrl)
            { // special for PB
              if (c == UP)
                c = PUP; // CTRL-UP to page up
              if (c == DOWN)
                c = PDOWN; // CTRL-DOWN to page down
              if (c == LEFT)
                c = HOME; // CTRL-LEFT to home
              if (c == RIGHT)
                c = END; // CTRL-RIGHT to end
            }
          }
          else if ((Code >= 0x15 && Code < 0x62) && AltGrDown != 0) // a keycode preceeded by Alt-Gr
            switch (Option.KeyboardConfig)
            {               // an international keycode pressed with
            case CONFIG_US: // the AltGr key
              break;        // no code for US keyboard
            case CONFIG_FR: // French Keyboard
              switch (Code)
              {
              case 0x45:
                c = 0x40; // @
                AltGrDown = 0;
                break;
              case 0x25:
                c = 0x7b; // {
                AltGrDown = 0;
                break;
              case 0x2e:
                c = 0x5b; // [
                AltGrDown = 0;
                break;
              case 0x55:
                c = 0x7d; // }
                AltGrDown = 0;
                break;
              case 0x4e:
                c = 0x5d; // ]
                AltGrDown = 0;
                break;
              case 0x3e:
                c = 0x5c; // '\'
                AltGrDown = 0;
                break;
              case 0x1e:
                c = 0x7e; // ~
                AltGrDown = 0;
                break;
              case 0x36:
                c = 0x7c; // |
                AltGrDown = 0;
                break;
              case 0x26:
                c = 0x23; // #
                AltGrDown = 0;
                break;
              default:
                c = 0; // invalid code
                AltGrDown = 0;
                break;
              }
              break;
            case CONFIG_GR: // German Keyboard
              switch (Code)
              {
              case 0x15:
                c = 0x40; // @
                AltGrDown = 0;
                break;
              case 0x3d:
                c = 0x7b; // {
                AltGrDown = 0;
                break;
              case 0x3e:
                c = 0x5b; // [
                AltGrDown = 0;
                break;
              case 0x45:
                c = 0x7d; // }
                AltGrDown = 0;
                break;
              case 0x46:
                c = 0x5d; // ]
                AltGrDown = 0;
                break;
              case 0x4e:
                c = 0x5c; // '\'
                AltGrDown = 0;
                break;
              case 0x5b:
                c = 0x7e; // ~
                AltGrDown = 0;
                break;
              case 0x61:
                c = 0x7c; // |
                AltGrDown = 0;
                break;
              default:
                c = 0; // invalid code
                AltGrDown = 0;
                break;
              }
              break;
            case CONFIG_IT: // Italian Keyboard
              switch (Code)
              {
              case 0x4C:
                c = 0x40; // @
                AltGrDown = 0;
                break;
              case 0x54:
                c = 0x5b; // [
                AltGrDown = 0;
                break;
              case 0x5b:
                c = 0x5d; // ]
                AltGrDown = 0;
                break;
              case 0x52:
                c = 0x23; // #
                AltGrDown = 0;
                break;
              default:
                c = 0; // invalid code
                AltGrDown = 0;
                break;
              }
              break;
            case CONFIG_BE: // Belgian Keyboard
              switch (Code)
              {
              case 0x1e:
                c = 0x40; // @
                AltGrDown = 0;
                break;
              case 0x46:
                c = 0x7b; // {
                AltGrDown = 0;
                break;
              case 0x54:
                c = 0x5b; // [
                AltGrDown = 0;
                break;
              case 0x45:
                c = 0x7d; // }
                AltGrDown = 0;
                break;
              case 0x5b:
                c = 0x5d; // ]
                AltGrDown = 0;
                break;
              case 0x1a:
                c = 0x5c; // '\'
                AltGrDown = 0;
                break;
              case 0x4a:
                c = 0x7e; // ~
                AltGrDown = 0;
                break;
              case 0x16:
                c = 0x7c; // |
                AltGrDown = 0;
                break;
              case 0x26:
                c = 0x23; // #
                AltGrDown = 0;
                break;
              default:
                c = 0; // invalid code
                AltGrDown = 0;
                break;
              }
              break;
            case CONFIG_ES: // Spanish Keyboard
              switch (Code)
              {
              case 0x1E:
                c = 0x40; // @
                AltGrDown = 0;
                break;
              case 0x54:
                c = 0x5b; // [
                AltGrDown = 0;
                break;
              case 0x5b:
                c = 0x5d; // ]
                AltGrDown = 0;
                break;
              case 0x26:
                c = 0x23; // #
                AltGrDown = 0;
                break;
              case 0x52:
                c = 0x7b; // {
                AltGrDown = 0;
                break;
              case 0x5d:
                c = 0x7d; // }
                AltGrDown = 0;
                break;
              case 0x16:
                c = 0x7c; // |
                AltGrDown = 0;
                break;
              case 0x0e:
                c = 0x5c; // '\'
                AltGrDown = 0;
                break;
              case 0x25:
                c = 0x7e; // ~
                AltGrDown = 0;
                break;
              default:
                c = 0; // invalid code
                AltGrDown = 0;
                break;
              }
              break;
            case CONFIG_BR: // Brazilian Keyboard (TO DO)
              switch (Code)
              {
              case 0x1D:
                c = '?'; // ?
                AltGrDown = 0;
                break;
              case 0x14:
                c = '/'; // /
                AltGrDown = 0;
                break;
              default:
                c = 0; // invalid code
                AltGrDown = 0;
                break;
              }
              break;
            }
          else
          {
            switch (Option.KeyboardConfig)
            {
            case CONFIG_US:
              if (LShift || RShift)
                c = keySCodes[0][Code % 128]; // a keycode preceeded by a shift
              else
                c = keyCodes[0][Code % 128]; // just a keycode
              break;
            case CONFIG_FR:
              if (LShift || RShift)
                c = keySCodes[1][Code % 128]; // a keycode preceeded by a shift
              else
                c = keyCodes[1][Code % 128]; // just a keycode
              break;
            case CONFIG_GR:
              if (LShift || RShift)
                c = keySCodes[2][Code % 128]; // a keycode preceeded by a shift
              else
                c = keyCodes[2][Code % 128]; // just a keycode
              break;
            case CONFIG_IT:
              if (LShift || RShift)
                c = keySCodes[3][Code % 128]; // a keycode preceeded by a shift
              else
                c = keyCodes[3][Code % 128]; // just a keycode
              break;
            case CONFIG_BE:
              if (LShift || RShift)
                c = keySCodes[4][Code % 128]; // a keycode preceeded by a shift
              else
                c = keyCodes[4][Code % 128]; // just a keycode
              break;
            case CONFIG_UK:
              if (LShift || RShift)
                c = keySCodes[5][Code % 128]; // a keycode preceeded by a shift
              else
                c = keyCodes[5][Code % 128]; // just a keycode
              break;
            case CONFIG_ES:
              if (LShift || RShift)
                c = keySCodes[6][Code % 128]; // a keycode preceeded by a shift
              else
                c = keyCodes[6][Code % 128]; // just a keycode
              break;
            case CONFIG_BR:
              if (LShift || RShift)
                c = keySCodes[7][Code % 128]; // a keycode preceeded by a shift
              else
                c = keyCodes[7][Code % 128]; // just a keycode
              break;
            }
          }

          if (!c)
            goto SkipOut;

          if (c <= 0x7F)
          { // a normal character
            if (CapsLock && c >= 'a' && c <= 'z')
              c -= 32; // adj for caps lock
            if (PS2Ctrl)
              c &= 0x1F; // adj for control
          }
          else
          { // must be a function key or similar
            if (LShift || RShift)
              c |= 0b00100000;
            // NOTE: Special for PB, CTRL-UP to page up, CTRL-DOWN to page down, CTRL-LEFT to home, CTRL-RIGHT to end
            if (PS2Ctrl && !(c == PUP || c == PDOWN || c == HOME || c == END))
              c |= 0b01000000;
          }

          if (BreakKey && c == BreakKey)
          {                                      // if the user wants to stop the progran
            MMAbort = true;                      // set the flag for the interpreter to see
            ConsoleRxBufHead = ConsoleRxBufTail; // empty the buffer
                                                 // break;
          }
          else
          {
            if (!(justset && c == '3'))
            {
              Timer3=0;
              ConsoleRxBuf[ConsoleRxBufHead] = c; // store the byte in the ring buffer
              if (ConsoleRxBuf[ConsoleRxBufHead] == keyselect && KeyInterrupt != NULL)
              {
                Keycomplete = true;
              }
              else
              {
                ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE; // advance the head of the queue
                if (ConsoleRxBufHead == ConsoleRxBufTail)
                {                                                                  // if the buffer has overflowed
                  ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
                }
              }
            }
            else
              justset = 0;
          }

        SkipOut:
          // end lump of self contained code
          //////////////////////////////////////////////////////////////////////////////////////////////////////////
          KeyUpCode = false;
          KeyE0 = false;
        }
}
/***************************************************************************************************
change notification interrupt service routine
****************************************************************************************************/
void __not_in_flash_func(CNInterrupt)(uint64_t dd)
{
  static unsigned char Code = 0;
  int d = dd & (1<<PinDef[Option.KEYBOARD_DATA].GPno);

  // Make sure it was a falling edge
  if (!(dd & (1<<PinDef[Option.KEYBOARD_CLOCK].GPno)))
  {
    if(!Timer3)PS2State=PS2START;
    switch (PS2State)
    {
    default:
    case PS2ERROR: // this can happen if a timing or parity error occurs
                   //            ClickTimer = 50;
      LShift = 0;  // reset the CTRL, SHIFT and other keys
      RShift = 0;
      PS2Ctrl = 0;
      AltGrDown = 0;
      KeyUpCode = false;
      KeyE0 = false;
      // fall through to PS2START

    case PS2START:
      if (!d)
      {              // PS2DAT == 0
        KCount = 8;  // init bit counter
        KParity = 0; // init parity check
        Code = 0;
        PS2State = PS2BIT;
        Timer3=5;
      }
      break;

    case PS2BIT:
      Code >>= 1; // shift in data bit
      if (d)
        Code |= 0x80;  // PS2DAT == 1
      KParity ^= Code; // calculate parity
      if (--KCount <= 0)
        PS2State = PS2PARITY; // all bit read
      break;

    case PS2PARITY:
      if (d)
        KParity ^= 0x80;  // PS2DAT == 1
      if (KParity & 0x80) // parity odd, continue
        PS2State = PS2STOP;
      else {
        PS2State = PS2ERROR;
        putConsole('q',1);
      }
      break;

    case PS2STOP:
      if (d)
      { // PS2DAT == 1
      processcode(Code);
      Code = 0;
      }
      PS2State = PS2START;
      break;
    }
  }
}

