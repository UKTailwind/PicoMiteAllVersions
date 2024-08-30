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
#include "tusb.h"
#include "hardware/structs/usb.h"
extern volatile int ConsoleRxBufHead;
extern volatile int ConsoleRxBufTail;
extern volatile int keytimer;
int justset = 0;
// extern char ConsoleRxBuf[];
uint32_t repeattime;
void USR_KEYBRD_ProcessData(uint8_t data);
static void process_mouse_report(hid_mouse_report_t const * report, uint8_t n);
// key codes that must be tracked for up/down state
#define CTRL 0x14 // left and right generate the same code
#define L_SHFT 0x12
#define R_SHFT 0x59
#define CAPS 0x58
#define NUML 0x77
//static uint8_t ds4_dev_addr = 0;
//static uint8_t ds4_instance = 0;
extern const char *KBrdList[];
//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

void tuh_mount_cb(uint8_t dev_addr)
{
  // application set-up
//  sprintf(buff,"A device with address %d is mounted\r\n", dev_addr);
}

void tuh_umount_cb(uint8_t dev_addr)
{
  // application tear-down
//  sprintf(buff,"A device with address %d is unmounted \r\n", dev_addr);
}
//--------------------------------------------------------------------+

// If your host terminal support ansi escape code such as TeraTerm
// it can be use to simulate mouse cursor movement within terminal
#define USE_ANSI_ESCAPE   0

#define MAX_REPORT  4
typedef enum
{
    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                       = 0x04,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                       = 0x05,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                       = 0x06,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                       = 0x07,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                       = 0x08,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                       = 0x09,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                       = 0x0A,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                       = 0x0B,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                       = 0x0C,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                       = 0x0D,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                       = 0x0E,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                       = 0x0F,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                       = 0x10,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                       = 0x11,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                       = 0x12,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                       = 0x13,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                       = 0x14,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                       = 0x15,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                       = 0x16,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                       = 0x17,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                       = 0x18,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                       = 0x19,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                       = 0x1A,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                       = 0x1B,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                       = 0x1C,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                       = 0x1D,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT                 = 0x1E,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                                = 0x1F,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                              = 0x20,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                            = 0x21,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                           = 0x22,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                            = 0x23,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                         = 0x24,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                          = 0x25,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                  = 0x26,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS                 = 0x27,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                            = 0x28,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                  = 0x29,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                                = 0x2C,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                    = 0x2D,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                          = 0x2E,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE       = 0x2F,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE     = 0x30,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                   = 0x32,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                     = 0x33,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                    = 0x34,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                  = 0x35,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                     = 0x36,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN                 = 0x37,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK         = 0x38,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                      = 0x3A,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                      = 0x3B,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                      = 0x3C,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                      = 0x3D,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                      = 0x3E,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                      = 0x3F,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                      = 0x40,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                      = 0x41,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                      = 0x42,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                                     = 0x43,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                                     = 0x44,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                                     = 0x45,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                            = 0x46,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                  = 0x49,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                    = 0x4A,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                                 = 0x4B,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                          = 0x4C,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                     = 0x4D,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                               = 0x4E,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                             = 0x4F,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                              = 0x50,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                              = 0x51,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                                = 0x52,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                                = 0x54,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                  = 0x55,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                     = 0x56,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                      = 0x57,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                     = 0x58,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                                 = 0x59,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                          = 0x5A,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                           = 0x5B,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                          = 0x5C,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                         = 0x5D,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                         = 0x5E,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                                = 0x5F,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                            = 0x60,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                             = 0x61,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                              = 0x62,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                         = 0x63,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE           = 0x64,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APPLICATION                             = 0x65,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POWER                                   = 0x66,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_SIZE                              = 0x67,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F13                                     = 0x68,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F14                                     = 0x69,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F15                                     = 0x6A,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F16                                     = 0x6B,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F17                                     = 0x6C,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F18                                     = 0x6D,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F19                                     = 0x6E,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F20                                     = 0x6F,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F21                                     = 0x70,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F22                                     = 0x71,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F23                                     = 0x72,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F24                                     = 0x73,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EXECUTE                                 = 0x74,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HELP                                    = 0x75,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MENU                                    = 0x76,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SELECT                                  = 0x77,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_STOP                                    = 0x78,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_AGAIN                                   = 0x79,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UNDO                                    = 0x7A,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CUT                                     = 0x7B,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COPY                                    = 0x7C,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PASTE                                   = 0x7D,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FIND                                    = 0x7E,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MUTE                                    = 0x7F,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_VOLUME_UP                               = 0x80,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_VOLUME_DOWN                             = 0x81,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LOCKING_CAPS_LOCK                       = 0x82,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LOCKING_NUM_LOCK                        = 0x83,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LOCKING_SCROLL_LOCK                     = 0x84,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_COMMA                                     = 0x85,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_EQUAL_SIGN                                = 0x86,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL1                          = 0x87,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL2                          = 0x88,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL3                          = 0x89,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL4                          = 0x8A,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL5                          = 0x8B,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL6                          = 0x8C,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL7                          = 0x8D,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL8                          = 0x8E,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL9                          = 0x8F,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG1                                   = 0x90,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG2                                   = 0x91,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG3                                   = 0x92,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG4                                   = 0x93,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG5                                   = 0x94,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG6                                   = 0x95,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG7                                   = 0x96,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG8                                   = 0x97,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG9                                   = 0x98,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ALTERNATE_ERASE                         = 0x99,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SYS_REQ_ATTENTION                       = 0x9A,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CANCEL                                  = 0x9B,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLEAR                                   = 0x9C,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRIOR                                   = 0x9D,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN                                  = 0x9E,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEPARATOR                               = 0x9F,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OUT                                     = 0xA0,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPER                                    = 0xA1,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLEAR_AGAIN                             = 0xA2,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CR_SEL_PROPS                            = 0xA3,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EX_SEL                                  = 0xA4,
    /* Reserved                                                                         = 0xA5-0xAF */
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_00                                        = 0xB0,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_000                                       = 0xB1,
    USB_HID_KEYBOARD_KEYPAD_THOUSANDS_SEPARATOR                              = 0xB2,
    USB_HID_KEYBOARD_KEYPAD_DECIMAL_SEPARATOR                                = 0xB3,
    USB_HID_KEYBOARD_KEYPAD_CURRENCY_UNIT                                    = 0xB4,
    USB_HID_KEYBOARD_KEYPAD_CURRENTY_SUB_UNIT                                = 0xB5,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_OPEN_PARENTHESIS                          = 0xB6,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_CLOSE_PARENTHESIS                         = 0xB7,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_OPEN_CURLY_BRACE                          = 0xB8,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_CLOSE_CURLY_BRACE                         = 0xB9,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_TAB                                       = 0xBA,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACKSPACE                                 = 0xBB,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_A                                         = 0xBC,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_B                                         = 0xBD,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_C                                         = 0xBE,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_D                                         = 0xBF,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_E                                         = 0xC0,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_F                                         = 0xC1,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_XOR                                       = 0xC2,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_CARROT                                    = 0xC3,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERCENT_SIGN                              = 0xC4,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_LESS_THAN                                 = 0xC5,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_GREATER_THAN                              = 0xC6,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_AMPERSAND                                 = 0xC7,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_DOUBLE_AMPERSAND                          = 0xC8,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PIPE                                      = 0xC9,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_DOUBLE_PIPE                               = 0xCA,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_COLON                                     = 0xCB,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_HASH                                      = 0xCC,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_SPACE                                     = 0xCD,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_AT                                        = 0xCE,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_EXCLAMATION_POINT                         = 0xCF,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_STORE                              = 0xD0,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_RECALL                             = 0xD1,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_CLEAR                              = 0xD2,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_ADD                                = 0xD3,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_SUBTRACT                           = 0xD4,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_MULTIPLY                           = 0xD5,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_DIVIDE                             = 0xD6,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS_MINUS                                = 0xD7,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_CLEAR                                     = 0xD8,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_CLEAR_ENTRY                               = 0xD9,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BINARY                                    = 0xDA,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_OCTAL                                     = 0xDB,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_DECIMAL                                   = 0xDC,
    USB_HID_KEYBOARD_KEYPAD_KEYPAD_HEXADECIMAL                               = 0xDD,
    /* Reserved                                                                         = 0xDE-0xDF */
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_CONTROL                            = 0xE0,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_SHIFT                              = 0xE1,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ALT                                = 0xE2,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_GUI                                = 0xE3,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_CONTROL                           = 0xE4,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_SHIFT                             = 0xE5,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ALT                               = 0xE6,
    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_GUI                               = 0xE7
    //0xE8-0xFFFF reserved
} USB_HID_KEYBOARD_KEYPAD;
const int UKkeyValue[202] = {
	0,0,//    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
	97,65,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                       = 0x04,
	98,66,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                       = 0x05,
	99,67,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                       = 0x06,
	100,68,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                       = 0x07,
	101,69,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                       = 0x08,
	102,70,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                       = 0x09,
	103,71,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                       = 0x0A,
	104,72,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                       = 0x0B,
	105,73,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                       = 0x0C,
	106,74,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                       = 0x0D,
	107,75,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                       = 0x0E,
	108,76,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                       = 0x0F,
	109,77,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                       = 0x10,
	110,78,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                       = 0x11,
	111,79,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                       = 0x12,
	112,80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                       = 0x13,
	113,81,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                       = 0x14,
	114,82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                       = 0x15,
	115,83,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                       = 0x16,
	116,84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                       = 0x17,
	117,85,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                       = 0x18,
	118,86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                       = 0x19,
	119,87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                       = 0x1A,
	120,88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                       = 0x1B,
	121,89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                       = 0x1C,
	122,90,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                       = 0x1D,
	49,33,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT                 = 0x1E,
	50,34,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                                = 0x1F,  QUOTE UK!!!!
	51,35,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                              = 0x20,
	52,36,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                            = 0x21,
	53,37,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                           = 0x22,
	54,94,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                            = 0x23,
	55,38,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                         = 0x24,
	56,42,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                          = 0x25,
	57,40,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                  = 0x26,
	48,41,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS                 = 0x27,
	10,10,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                            = 0x28,
	27,27,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                  = 0x29,
	8,8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
	9,0x9f,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
	32,32,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                                = 0x2C,
	45,95,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                    = 0x2D,
	61,43,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                          = 0x2E,
	91,123,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE       = 0x2F,
	93,125,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE     = 0x30,
	92,124,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31,
	35,126,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                   = 0x32,
	59,58,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                     = 0x33,
	39,64,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                    = 0x34, @ UK///
	96,126,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                  = 0x35,
	44,60,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                     = 0x36,
	46,62,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN                 = 0x37,
	47,63,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK         = 0x38,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
	0x91,0xD1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92,0xD2,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93,0xD3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94,0xD4,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95,0xD5,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96,0xD6,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97,0xD7,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98,0xD8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99,0xD9,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a,0xDa,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b,0xDb,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c,0xDc,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d,0x9d,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e,0x9e,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                  = 0x49,
	0x86,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                    = 0x4A,
	0x88,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                                 = 0x4B,
	0x7f,0xa0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                          = 0x4C,
	0x87,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                     = 0x4D,
	0x89,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                               = 0x4E,
	0x83,0xA3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                             = 0x4F,
	0x82,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                              = 0x50,
	0x81,0xA1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                              = 0x51,
	0x80,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                                = 0x52,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
	47,47,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                                = 0x54,
	42,42,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                  = 0x55,
	45,45,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                     = 0x56,
	43,43,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                      = 0x57,
	10,10,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                     = 0x58,
	49,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                                 = 0x59,
	50,0x81,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                          = 0x5A,
	51,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                           = 0x5B,
	52,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                          = 0x5C,
	53,53,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                         = 0x5D,
	54,0x83,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                         = 0x5E,
	55,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                                = 0x5F,
	56,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                            = 0x60,
	57,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                             = 0x61,
	48,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                              = 0x62,
	46,0x7f,    //USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                         = 0x63,
	92,124    //USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE           = 0x64,
};

const int USkeyValue[202] = {
	0,0,//    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
	97,65,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                       = 0x04,
	98,66,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                       = 0x05,
	99,67,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                       = 0x06,
	100,68,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                       = 0x07,
	101,69,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                       = 0x08,
	102,70,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                       = 0x09,
	103,71,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                       = 0x0A,
	104,72,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                       = 0x0B,
	105,73,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                       = 0x0C,
	106,74,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                       = 0x0D,
	107,75,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                       = 0x0E,
	108,76,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                       = 0x0F,
	109,77,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                       = 0x10,
	110,78,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                       = 0x11,
	111,79,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                       = 0x12,
	112,80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                       = 0x13,
	113,81,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                       = 0x14,
	114,82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                       = 0x15,
	115,83,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                       = 0x16,
	116,84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                       = 0x17,
	117,85,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                       = 0x18,
	118,86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                       = 0x19,
	119,87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                       = 0x1A,
	120,88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                       = 0x1B,
	121,89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                       = 0x1C,
	122,90,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                       = 0x1D,
	49,33,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT                 = 0x1E,
	50,64,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                                = 0x1F,  QUOTE UK!!!!
	51,35,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                              = 0x20,
	52,36,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                            = 0x21,
	53,37,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                           = 0x22,
	54,94,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                            = 0x23,
	55,38,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                         = 0x24,
	56,42,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                          = 0x25,
	57,40,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                  = 0x26,
	48,41,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS                 = 0x27,
	10,10,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                            = 0x28,
	27,27,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                  = 0x29,
	8,8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
	9,0x9f,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
	32,32,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                                = 0x2C,
	45,95,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                    = 0x2D,
	61,43,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                          = 0x2E,
	91,123,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE       = 0x2F,
	93,125,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE     = 0x30,
	92,124,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31,
	92,124,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                   = 0x32,
	59,58,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                     = 0x33,
	39,34,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                    = 0x34, @ UK///
	96,126,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                  = 0x35,
	44,60,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                     = 0x36,
	46,62,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN                 = 0x37,
	47,63,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK         = 0x38,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
	0x91,0xD1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92,0xD2,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93,0xD3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94,0xD4,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95,0xD5,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96,0xD6,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97,0xD7,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98,0xD8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99,0xD9,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a,0xDa,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b,0xDb,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c,0xDc,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d,0x9d,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e,0x9e,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                  = 0x49,
	0x86,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                    = 0x4A,
	0x88,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                                 = 0x4B,
	0x7f,0xa0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                          = 0x4C,
	0x87,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                     = 0x4D,
	0x89,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                               = 0x4E,
	0x83,0xA3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                             = 0x4F,
	0x82,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                              = 0x50,
	0x81,0xA1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                              = 0x51,
	0x80,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                                = 0x52,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
	47,47,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                                = 0x54,
	42,42,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                  = 0x55,
	45,45,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                     = 0x56,
	43,43,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                      = 0x57,
	10,10,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                     = 0x58,
	49,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                                 = 0x59,
	50,0x81,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                          = 0x5A,
	51,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                           = 0x5B,
	52,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                          = 0x5C,
	53,53,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                         = 0x5D,
	54,0x83,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                         = 0x5E,
	55,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                                = 0x5F,
	56,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                            = 0x60,
	57,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                             = 0x61,
	48,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                              = 0x62,
	46,0x7f,    //USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                         = 0x63,
	92,124    //USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE           = 0x64,
};
const int DEkeyValue[202] = {
	0,0,//    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
	97,65,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                       = 0x04,
	98,66,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                       = 0x05,
	99,67,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                       = 0x06,
	100,68,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                       = 0x07,
	101,69,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                       = 0x08,
	102,70,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                       = 0x09,
	103,71,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                       = 0x0A,
	104,72,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                       = 0x0B,
	105,73,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                       = 0x0C,
	106,74,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                       = 0x0D,
	107,75,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                       = 0x0E,
	108,76,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                       = 0x0F,
	109,77,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                       = 0x10,
	110,78,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                       = 0x11,
	111,79,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                       = 0x12,
	112,80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                       = 0x13,
	113,81,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                       = 0x14,
	114,82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                       = 0x15,
	115,83,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                       = 0x16,
	116,84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                       = 0x17,
	117,85,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                       = 0x18,
	118,86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                       = 0x19,
	119,87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                       = 0x1A,
	120,88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                       = 0x1B,
	122,90,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                       = 0x1C,
	121,89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                       = 0x1D,
	49,33,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT                 = 0x1E,
	50,34,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                                = 0x1F,
	51,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                              = 0x20,      DE: §=245
	52,36,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                            = 0x21,
	53,37,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                           = 0x22,
	54,38,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                            = 0x23,
	55,47,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                         = 0x24,
	56,40,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                          = 0x25,
	57,41,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                  = 0x26,
	48,61,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS                 = 0x27,
	10,10,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                            = 0x28,
	27,27,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                  = 0x29,
	8,8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
	9,0x9f,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
	32,32,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                                = 0x2C,
	0,63,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                    = 0x2D,        DE: ß=225
	0,96,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                          = 0x2E,   DE: ´=239
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE       = 0x2F,         DE: ü=129,Ü=154
	43,42,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE     = 0x30,
	35,39,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31,
	35,39,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                   = 0x32,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                     = 0x33,   DE: ö=228,Ö=229
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                    = 0x34,   DE: ä=132,Ä=142
	94,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                  = 0x35,   DE: °=167
	44,59,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                     = 0x36,
	46,58,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN                 = 0x37,
	45,95,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK         = 0x38,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
	0x91,0xD1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92,0xD2,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93,0xD3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94,0xD4,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95,0xD5,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96,0xD6,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97,0xD7,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98,0xD8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99,0xD9,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a,0xDa,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b,0xDb,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c,0xDc,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d,0x9d,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e,0x9e,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                  = 0x49,
	0x86,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                    = 0x4A,
	0x88,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                                 = 0x4B,
	0x7f,0xa0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                          = 0x4C,
	0x87,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                     = 0x4D,
	0x89,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                               = 0x4E,
	0x83,0xA3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                             = 0x4F,
	0x82,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                              = 0x50,
	0x81,0xA1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                              = 0x51,
	0x80,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                                = 0x52,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
	47,47,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                                = 0x54,
	42,42,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                  = 0x55,
	45,45,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                     = 0x56,
	43,43,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                      = 0x57,
	10,10,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                     = 0x58,
	49,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                                 = 0x59,
	50,0x81,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                          = 0x5A,
	51,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                           = 0x5B,
	52,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                          = 0x5C,
	53,53,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                         = 0x5D,
	54,0x83,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                         = 0x5E,
	55,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                                = 0x5F,
	56,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                            = 0x60,
	57,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                             = 0x61,
	48,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                              = 0x62,
	46,0x7f,    //USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                         = 0x63,
	60,62    //USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE           = 0x64,
};
const int FRkeyValue[202] = {
	0,0,//    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
	113,81,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                    = 0x04, FR Q
	98,66,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                     = 0x05,
	99,67,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                     = 0x06,
	100,68,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                    = 0x07,
	101,69,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                    = 0x08,
	102,70,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                    = 0x09,
	103,71,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                    = 0x0A,
	104,72,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                    = 0x0B,
	105,73,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                    = 0x0C,
	106,74,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                    = 0x0D,
	107,75,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                    = 0x0E,
	108,76,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                    = 0x0F,
	44,63,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                     = 0x10, FR ,?
	110,78,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                    = 0x11,
	111,79,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                    = 0x12,
	112,80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                    = 0x13,
	97,65,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                     = 0x14, FR A
	114,82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                    = 0x15,
	115,83,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                    = 0x16,
	116,84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                    = 0x17,
	117,85,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                    = 0x18,
	118,86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                    = 0x19,
	122,90,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                    = 0x1A, FR Z
	120,88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                    = 0x1B,
	121,89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                    = 0x1C,
	119,87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                    = 0x1D, FR W
	38,49,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT               = 0x1E, FR & 1
	233,50,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                             = 0x1F, FR é 2 ~
	34,51,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                            = 0x20, FR " 3 #
	39,52,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                          = 0x21, FR ' 4 {
	40,53,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                         = 0x22, FR ( 5 [
	45,54,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                          = 0x23, FR - 6
	232,55,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                      = 0x24, FR è 7 `
	95,56,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                        = 0x25, FR _ 8 '\'
	135,57,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS               = 0x26, FR ç 9 ^
	00,48,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS               = 0x27, FR à 0 @
	10,10,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                          = 0x28,
	27,27,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                = 0x29,
	8,8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
	9,0x9f,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
	32,32,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                              = 0x2C,
	41,00,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                  = 0x2D, FR ) ]
	61,43,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                        = 0x2E,
	94,168,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE    = 0x2F, FR ^ ¨
	36,163,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE  = 0x30, FR $ £
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31, -- not present --
	42,181,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                = 0x32, FR * µ
	109,77,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                  = 0x33, FR M
	249,37,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                 = 0x34, FR ù %
	178,00,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE               = 0x35, FR ²
	59,46,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                   = 0x36, FR ; .
	58,47,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN               = 0x37, FR : /
	33,167,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK      = 0x38, FR ! §
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
	0x91,0xD1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92,0xD2,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93,0xD3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94,0xD4,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95,0xD5,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96,0xD6,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97,0xD7,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98,0xD8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99,0xD9,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a,0xDa,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b,0xDb,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c,0xDc,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d,0x9d,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e,0x9e,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                = 0x49,
	0x86,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                  = 0x4A,
	0x88,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                               = 0x4B,
	0x7f,0xa0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                    = 0x4C,
	0x87,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                               = 0x4D,
	0x89,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                         = 0x4E,
	0x83,0xA3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                       = 0x4F,
	0x82,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                        = 0x50,
	0x81,0xA1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                        = 0x51,
	0x80,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                          = 0x52,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
	47,47,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                              = 0x54,
	42,42,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                = 0x55,
	45,45,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                   = 0x56,
	43,43,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                    = 0x57,
	10,10,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                   = 0x58,
	49,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                             = 0x59,
	50,0x81,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                      = 0x5A,
	51,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                       = 0x5B,
	52,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                      = 0x5C,
	53,53,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                       = 0x5D,
	54,0x83,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                     = 0x5E,
	55,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                            = 0x5F,
	56,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                        = 0x60,
	57,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                         = 0x61,
	48,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                          = 0x62,
	46,0x7f,    //USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                     = 0x63,
	60,62    //USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE          = 0x64, FR < >
};
const int ESkeyValue[202] = {
	0,0,//          USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                    = 0x00,
	0,0,//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                       = 0x01,
	0,0,//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                             = 0x02,
	0,0,//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                       = 0x03,
	97,65,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                     = 0x04,
	98,66,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                     = 0x05,
	99,67,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                     = 0x06,
	100,68,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                     = 0x07,
	101,69,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                     = 0x08, ALT GR -> €
	102,70,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                     = 0x09,
	103,71,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                     = 0x0A,
	104,72,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                     = 0x0B,
	105,73,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                     = 0x0C,
	106,74,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                     = 0x0D,
	107,75,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                     = 0x0E,
	108,76,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                     = 0x0F,
	109,77,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                     = 0x10,
	110,78,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                     = 0x11,
	111,79,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                     = 0x12,
	112,80,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                     = 0x13,
	113,81,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                     = 0x14,
	114,82,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                     = 0x15,
	115,83,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                     = 0x16,
	116,84,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                     = 0x17,
	117,85,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                     = 0x18,
	118,86,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                     = 0x19,
	119,87,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                     = 0x1A,
	120,88,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                     = 0x1B,
	121,89,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                     = 0x1C,
	122,90,//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                     = 0x1D,·
	49,33,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT               = 0x1E, ALT GR -> |
	50,34,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                              = 0x1F, ALT GR -> @
	51,0,//         USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                            = 0x20, ALT GR -> #
	52,36,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                          = 0x21, ALT GR -> ~
	53,37,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                         = 0x22,
	54,38,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                          = 0x23,
	55,47,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                       = 0x24,
	56,40,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                        = 0x25,
	57,41,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                = 0x26,
	48,61,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS               = 0x27,
	10,10,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                          = 0x28,
	27,27,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                = 0x29,
	8,8,//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                = 0x2A,
	9,0x9f,//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                   = 0x2B,
	32,32,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                              = 0x2C,
	39,63,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                  = 0x2D,
	0,0,//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                        = 0x2E,
	0,94,//         USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE     = 0x2F, ALT GR -> [
	43,42,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE   = 0x30, ALT GR -> ]
	0,0,//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                   = 0x31, ALT GR -> }
	92,0,//         USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                 = 0x32,
	0,0,//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                   = 0x33,
	0,0,//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                  = 0x34, ALT GR -> {
	96,96,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                = 0x35, ALT GR -> backslash  (Tecla º)
	44,59,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                   = 0x36,
	46,58,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN               = 0x37,
	45,95,//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK       = 0x38,
	0,0,//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                             = 0x39,
	0x91,0xD1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92,0xD2,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93,0xD3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94,0xD4,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95,0xD5,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96,0xD6,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97,0xD7,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98,0xD8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99,0xD9,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a,0xDa,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b,0xDb,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c,0xDc,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d,0x9d,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e,0x9e,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                = 0x49,
	0x86,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                  = 0x4A,
	0x88,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                               = 0x4B,
	0x7f,0xa0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                        = 0x4C,
	0x87,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                   = 0x4D,
	0x89,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                             = 0x4E,
	0x83,0xA3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                           = 0x4F,
	0x82,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                            = 0x50,
	0x81,0xA1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                            = 0x51,
	0x80,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                              = 0x52,
	0,0,//          USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                      = 0x53,
	47,47,//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                              = 0x54,
	42,42,//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                = 0x55,
	45,45,//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                   = 0x56,
	43,43,//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                    = 0x57,
	10,10,//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                   = 0x58,
	49,0x87,//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                               = 0x59,
	50,0x81,//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                        = 0x5A,
	51,0x89,//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                         = 0x5B,
	52,0x82,//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                        = 0x5C,
	53,53,//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                       = 0x5D,
	54,0x83,//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                       = 0x5E,
	55,0x86,//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                              = 0x5F,
	56,0x80,//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                          = 0x60,
	57,0x88,//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                           = 0x61,
	48,0x84,//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                            = 0x62,
	46,0x7f,//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                       = 0x63,
	60,62 //        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE         = 0x64,
};
const int BEkeyValue[202] = {
	0,0,//    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
	113,81,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                       = 0x04,
	98,66,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                       = 0x05,
	99,67,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                       = 0x06,
	100,68,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                       = 0x07,
	101,69,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                       = 0x08,
	102,70,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                       = 0x09,
	103,71,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                       = 0x0A,
	104,72,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                       = 0x0B,
	105,73,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                       = 0x0C,
	106,74,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                       = 0x0D,
	107,75,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                       = 0x0E,
	108,76,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                       = 0x0F,
	44,63,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                       = 0x10,
	110,78,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                       = 0x11,
	111,79,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                       = 0x12,
	112,80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                       = 0x13,
	97,65,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                       = 0x14,
	114,82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                       = 0x15,
	115,83,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                       = 0x16,
	116,84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                       = 0x17,
	117,85,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                       = 0x18,
	118,86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                       = 0x19,
	122,90,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                             = 0x1A,
	120,88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                       = 0x1B,
	121,89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                             = 0x1C,
	119,87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                       = 0x1D,
	38,49,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT                 = 0x1E,
	64,50,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                                = 0x1F,
	34,51,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                              = 0x20,
	39,52,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                            = 0x21,
	40,53,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                           = 0x22,
	36,54,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                            = 0x23,
	96,55,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                         = 0x24,
	33,56,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                          = 0x25,
	123,57,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                  = 0x26,
	125,48,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS                 = 0x27,
	10,10,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                            = 0x28,
	27,27,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                  = 0x29,
	8,8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
	9,9,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
	32,32,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                                = 0x2C,
	41,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                    = 0x2D,
	45,95,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                          = 0x2E,
	91,94,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE       = 0x2F,
	36,42,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE     = 0x30,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                   = 0x32,
	109,77,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                     = 0x33,
	0,37,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                    = 0x34,
	35,124,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                  = 0x35,
	59,46,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                     = 0x36,
	58,47,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN                 = 0x37,
	61,43,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK         = 0x38,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
	0x91,0xD1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92,0xD2,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93,0xD3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94,0xD4,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95,0xD5,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96,0xD6,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97,0xD7,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98,0xD8,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99,0xD9,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a,0xDa,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b,0xDb,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c,0xDc,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d,0x9d,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e,0x9e,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                  = 0x49,
	0x86,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                    = 0x4A,
	0x88,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                                 = 0x4B,
	0x7f,0xa0,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                          = 0x4C,
	0x87,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                     = 0x4D,
	0x89,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                               = 0x4E,
	0x83,0xA3,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                             = 0x4F,
	0x82,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                              = 0x50,
	0x81,0xA1,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                              = 0x51,
	0x80,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                                = 0x52,
	0,0,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
	47,47,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                                = 0x54,
	42,42,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                  = 0x55,
	45,45,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                     = 0x56,
	43,43,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                      = 0x57,
	10,10,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                     = 0x58,
	49,0x87,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                                 = 0x59,
	50,0x81,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                          = 0x5A,
	51,0x89,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                           = 0x5B,
	52,0x82,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                          = 0x5C,
	53,53,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                         = 0x5D,
	54,0x83,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                         = 0x5E,
	55,0x86,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                                = 0x5F,
	56,0x80,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                            = 0x60,
	57,0x88,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                             = 0x61,
	48,0x84,//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                              = 0x62,
	46,0x7f,    //USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                         = 0x63,
	60,62    //USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE           = 0x64,
};
volatile struct s_HID HID[4]={0};
typedef struct TU_ATTR_PACKED
{
  uint8_t x, y, z, rz; // joystick

  struct {
    uint8_t dpad     : 4; // (hat format, 0x08 is released, 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW)
    uint8_t square   : 1; // west
    uint8_t cross    : 1; // south
    uint8_t circle   : 1; // east
    uint8_t triangle : 1; // north
  };

  struct {
    uint8_t l1     : 1;
    uint8_t r1     : 1;
    uint8_t l2     : 1;
    uint8_t r2     : 1;
    uint8_t share  : 1;
    uint8_t option : 1;
    uint8_t l3     : 1;
    uint8_t r3     : 1;
  };

  struct {
    uint8_t ps      : 1; // playstation button
    uint8_t tpad    : 1; // track pad click
    uint8_t counter : 6; // +1 each report
  };

  uint8_t l2_trigger; // 0 released, 0xff fully pressed
  uint8_t r2_trigger; // as above

    uint16_t timestamp;
    uint8_t  battery;
  //
    int16_t gyro[3];  // x, y, z;
    int16_t accel[3]; // x, y, z

  // there is still lots more info

} sony_ds4_report_t;

typedef struct TU_ATTR_PACKED {
  // First 16 bits set what data is pertinent in this structure (1 = set; 0 = not set)
  uint8_t set_rumble : 1;
  uint8_t set_led : 1;
  uint8_t set_led_blink : 1;
  uint8_t set_ext_write : 1;
  uint8_t set_left_volume : 1;
  uint8_t set_right_volume : 1;
  uint8_t set_mic_volume : 1;
  uint8_t set_speaker_volume : 1;
  uint8_t set_flags2;

  uint8_t reserved;

  uint8_t motor_right;
  uint8_t motor_left;

  uint8_t lightbar_red;
  uint8_t lightbar_green;
  uint8_t lightbar_blue;
  uint8_t lightbar_blink_on;
  uint8_t lightbar_blink_off;

  uint8_t ext_data[8];

  uint8_t volume_left;
  uint8_t volume_right;
  uint8_t volume_mic;
  uint8_t volume_speaker;

  uint8_t other[9];
} sony_ds4_output_report_t;
#define PS4  128
#define PS3  129
#define SNES 130
#define XBOX 131
static inline bool is_xbox(uint8_t dev_addr)
{
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  return ( (vid == 0x11c0 && pid == 0x5500)    // EasySMX Wireless, u, Android mode (u)		 
		   || (vid == 0x11c1 && pid == 0x9101) // EasySMX Wireless, c, PC Mode, D-input, emulation
         );
}
void process_xbox(uint8_t const* report, uint16_t len, uint8_t n)
{
	//PInt(len);
	/*for (int i=0;i<len;i++) {
		PInt(i);
		PIntHC(report[i]);
	}
	PRet();*/
	nunstruct[n].type=XBOX;
	uint16_t b=0;
    if(len == 9) {
		if(report[0]&0x10)b|=0x0400;  // Button y/triangle
		if(report[0]&0x02)b|=0x0800;  // Button b/circle
		if(report[0]&0x08)b|=0x1000;  // Button x/square
		if(report[0]&0x01)b|=0x2000;  // Button a/cross
		if(report[1]&0x08)b|=0x0002;  // Button start -> start?
		if(report[1]&0x04)b|=0x0008;  // Button home -> xbox/PS?
		if(report[1]&0x10)b|=0x0004;  // Button select -> back/share?
		if(report[0]&0x80)b|=0x0001;  // Button R/R1	
		if(report[0]&0x40)b|=0x0010;  // Button L/L1
		if(report[2] == 0x4)b|=0x20;  // Button down cursor
		if(report[2] == 0x2)b|=0x40;  // Button right cursor
		if(report[2] == 0x0)b|=0x80;  // Button up cursor
		if(report[2] == 0x6)b|=0x100; // Button left cursor
		nunstruct[n].ax=report[3];
		nunstruct[n].ay=report[4];
		nunstruct[n].Z=report[5];
		nunstruct[n].C=report[6];
		nunstruct[n].L=report[8];
		nunstruct[n].R=report[7];
	}
	else {
		// TODO
	}
	if((b ^ nunstruct[n].x0) & nunstruct[n].x1){
		nunfoundc[n]=1;
	}
	nunstruct[n].x0=b;
}
// Each HID instance can has multiple reports
/*static struct
{
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
} hid_info[CFG_TUH_HID];*/

static void process_kbd_report(hid_keyboard_report_t const *report, uint8_t n);
//static void process_mouse_report(hid_mouse_report_t const * report);
//static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);
int caps_lock=0;
int num_lock=0;
int scroll_lock=0;
int KeyDown[7];
const int *keylayout;
uint8_t Current_USB_devices=0;
uint8_t Current_controllers=0;

void USB_bus_reset(void){
	hw_set_bits(&usb_hw->phy_direct_override, USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS |
	USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS) ;
	hw_set_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DM_OE_BITS | USB_USBPHY_DIRECT_TX_DP_OE_BITS);
	uint32_t save=usb_hw->phy_direct;
	hw_clear_bits(&usb_hw->phy_direct,USB_USBPHY_DIRECT_TX_DM_BITS |USB_USBPHY_DIRECT_TX_DP_BITS);
	uSec(10000);
	usb_hw->phy_direct=save;
	hw_clear_bits(&usb_hw->phy_direct_override, USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS);
}
void clearrepeat(void){
	keytimer=0;
	repeattime=Option.RepeatStart;
	memset(KeyDown,0,sizeof(KeyDown));
}
bool diff_than_2(uint8_t x, uint8_t y)
{
  return (x - y > 4) || (y - x > 4);
}
static inline bool is_sony_ds4(uint8_t dev_addr)
{
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  return ( (vid == 0x054c && (pid == 0x09cc || pid == 0x05c4)) // Sony DualShock4
           || (vid == 0x0f0d && pid == 0x005e)                 // Hori FC4
           || (vid == 0x0f0d && pid == 0x00ee)                 // Hori PS4 Mini (PS4-099U)
           || (vid == 0x1f4f && pid == 0x1002)                 // ASW GG xrd controller
         );
}
/*
LX returns the position of the analog left joystick x axis
LY returns the position of the analog left joystick y axis
RX returns the position of the analog right joystick x axis
RY returns the position of the analog right joystick y axis
L returns the position of the analog left button
R returns the position of the analog right button
B returns a bitmap of the state of all the buttons. A bit will be set to 1 if the
button is pressed.
T returns the ID code of the controller
Wii Classic = &HA4200101
Generic Gamepad = 130
PS4 = 128
PS3 = 129
The button bitmap is as follows:
BIT 0: Button R/R1
BIT 1: Button start
BIT 2: Button home
BIT 3: Button select
BIT 4: Button L/L1
BIT 5: Button down cursor
BIT 6: Button right cursor
BIT 7: Button up cursor
BIT 8: Button left cursor
BIT 9: Button ZR/R2
BIT 10: Button x/triangle
BIT 11: Button a/circle
BIT 12: Button y/square
BIT 13: Button b/cross
BIT 14: Button ZL/L2
BIT 15: TPAD - PS4 only*/
static inline bool is_sony_ds3(uint8_t dev_addr)
{
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  return ( (vid == 0x054c && (pid == 0x0268)) // Sony DualShock3
         );
}
bool diff_report(sony_ds4_report_t const* rpt1, sony_ds4_report_t const* rpt2)
{
  bool result;

  // x, y, z, rz must different than 2 to be counted
  result = diff_than_2(rpt1->x, rpt2->x) || diff_than_2(rpt1->y , rpt2->y ) ||
           diff_than_2(rpt1->z, rpt2->z) || diff_than_2(rpt1->rz, rpt2->rz);

  // check the rest with mem compare
  result |= memcmp(&rpt1->rz + 1, &rpt2->rz + 1, sizeof(sony_ds4_report_t)-6);

  return result;
}
void process_gamepad(uint8_t const* report, uint16_t len, uint8_t n){
	nunstruct[n].type=SNES;
	uint16_t b=0;
	if(report[5] & 0x10)b|=1<<10;
	if(report[5] & 0x20)b|=1<<11;
	if(report[5] & 0x40)b|=1<<13;
	if(report[5] & 0x80)b|=1<<12;
	if(report[6] & 1)b|=1<<4;
	if(report[6] & 2)b|=1;
	if(report[6] & 0x10)b|=1<<3;
	if(report[6] & 0x20)b|=1<<1;
	if(report[0] < 0x7f)b|=1<<8;
	if(report[0] > 0x7f)b|=1<<6;
	if(report[1] < 0x7f)b|=1<<7;
	if(report[1] > 0x7f)b|=1<<5;
	if((b ^ nunstruct[n].x0) & nunstruct[n].x1){
		nunfoundc[n]=1;
	}
	nunstruct[n].x0=b;
}
void process_sony_ds3(uint8_t const* report, uint16_t len, uint8_t n)
{
	nunstruct[n].type=PS3;
	uint16_t b=0;
	if(report[3]&0x80)b|=1<<12;
	if(report[3]&0x40)b|=1<<13;
	if(report[3]&0x20)b|=1<<11;
	if(report[3]&0x10)b|=1<<10;
	if(report[3]&0x8)b|=1;
	if(report[3]&0x4)b|=1<<4;
	if(report[3]&0x2)b|=1<<9;
	if(report[3]&0x1)b|=1<<14;
	if(report[2]&0x80)b|=1<<8;
	if(report[2]&0x40)b|=1<<5;
	if(report[2]&0x20)b|=1<<6;
	if(report[2]&0x10)b|=1<<7;
	if(report[2]&0x8)b|=1<<1;
	if(report[2]&0x1)b|=1<<3;
	if(report[4]&0x1)b|=1<<2;
	nunstruct[n].ax=report[6];
	nunstruct[n].ay=report[7];
	nunstruct[n].Z=report[8];
	nunstruct[n].C=report[9];
	nunstruct[n].L=report[18];
	nunstruct[n].R=report[19];
	if((b ^ nunstruct[n].x0) & nunstruct[n].x1){
		nunfoundc[n]=1;
	}
	nunstruct[n].x0=b;
}
void process_sony_ds4(uint8_t const* report, uint16_t len, uint8_t n)
{
//const char* dpad_str[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW", "none" };

  // previous report used to compare for changes

  uint8_t const report_id = report[0];
  report++;
  len--;

  // all buttons state is stored in ID 1
  if (report_id == 1)
  {
    sony_ds4_report_t ds4_report;
    memcpy(&ds4_report, report, sizeof(ds4_report));
	nunstruct[n].type=PS3;
	uint16_t b=0;
	if (ds4_report.square   )b|=1<<12;
	if (ds4_report.cross    )b|=1<<13;
	if (ds4_report.circle   )b|=1<<11;
	if (ds4_report.triangle )b|=1<<10;
	if (ds4_report.r1       )b|=1;
	if (ds4_report.l1       )b|=1<<4;
	if (ds4_report.r2       )b|=1<<9;
	if (ds4_report.l2       )b|=1<<14;
	if( ds4_report.dpad==0  )b|=1<<7;
	if( ds4_report.dpad==2  )b|=1<<6;
	if( ds4_report.dpad==4  )b|=1<<5;
	if( ds4_report.dpad==6  )b|=1<<8;
	if( ds4_report.dpad==1  )b|=3<<6;
	if( ds4_report.dpad==3  )b|=3<<5;
	if( ds4_report.dpad==5  )b|=1<<5;
	if( ds4_report.dpad==7  )b|=((1<<5) | (1<<8));
	if (ds4_report.option   )b|=1<<1;
	if (ds4_report.share    )b|=1<<3;
	if (ds4_report.ps       )b|=1<<2;
	if (ds4_report.tpad     )b|=1<<15;
	nunstruct[n].ax=ds4_report.x;
	nunstruct[n].ay=ds4_report.y;
	nunstruct[n].Z=ds4_report.z;
	nunstruct[n].C=ds4_report.rz;
	nunstruct[n].L=ds4_report.l2_trigger;
	nunstruct[n].R=ds4_report.r2_trigger;
	memcpy((void *)nunstruct[n].gyro, ds4_report.gyro, 6*sizeof(uint16_t));
	if((b ^ nunstruct[n].x0) & nunstruct[n].x1){
		nunfoundc[n]=1;
	}
	nunstruct[n].x0=b;
    }
}
void hid_app_task(void)
{
  static uint64_t timer;
  uint64_t timenow=time_us_64();
  if(!USBenabled)return;
  if(KeyDown[0] && keytimer>repeattime){
			uint8_t c = KeyDown[0];
			if (!(c == 0 || c==PDOWN || c==PUP || c==CTRLKEY('P') || c==CTRLKEY('L') || c==25 || c==F5 || c==F4 || c==CTRLKEY('T')/* || (markmode && (c==DEL || c==CTRLKEY(']')))*/))
			{
				USR_KEYBRD_ProcessData(c);
			}
//			if(c==PDOWN || c==PUP || c==CTRLKEY('P') || c==CTRLKEY('L') || c==25 || c==F5  || c==F4 || c==CTRLKEY('T')/* || (markmode && (c==DEL || c==CTRLKEY(']')))*/)mmemset(&last_k_info,0,sizeof(HID_KEYBD_Info_TypeDef));
			keytimer=0;
			repeattime=Option.RepeatRate;
  }
	for(int i=0;i<4;i++){
		if(HID[i].active==false || HID[i].report_requested)continue;
		if(HID[i].report_timer>=HID[i].report_rate){
			if(HID[i].Device_type==HID_ITF_PROTOCOL_KEYBOARD && HID[i].notfirsttime==0){
				HID[i].notfirsttime=1;
				tuh_hid_set_report(HID[i].Device_address, HID[i].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[i].sendlights,1);

			}
			HID[i].report_requested=true;
			if ( !tuh_hid_receive_report(HID[i].Device_address, HID[i].Device_instance) )
			{
				MMPrintString("Warning USB failure on channel ");PInt(i+1);MMPrintString("\r\n> ");
				HID[i].active=false;
			}

		}
		if(HID[i].Device_type==PS4 && timenow-timer>50000){
			timer=timenow;
			sony_ds4_output_report_t output_report = {0};
			output_report.set_rumble = 1;
			output_report.motor_left = HID[i].motorleft;
			output_report.motor_right = HID[i].motorright;
			output_report.set_led = 1;
			output_report.lightbar_red=HID[i].r;
			output_report.lightbar_blue=HID[i].b;
			output_report.lightbar_green=HID[i].g;
			tuh_hid_send_report(HID[i].Device_address, HID[i].Device_instance, 5, &output_report, sizeof(output_report));
		}
	}

}
//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
int FindFreeSlot(void){
	for(int i=0;i<4;i++){
		if(!HID[i].active)return i;
	}
	return -1;
}
/*static struct
{
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
}hid_info[CFG_TUH_HID];*/

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
	__dsb();
//  uint16_t pid,vid;
  uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
//  PInt(itf_protocol);
//  tuh_vid_pid_get(dev_addr, &vid, &pid);
  int slot=FindFreeSlot();
  if(slot==-1)error("USB device limit reached");
//  char buff[STRINGSIZE];
//  PIntHC(vid);PIntHC(pid);PRet();
//  sprintf(buff,"HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
  /* Start HID Interface */
  if(Option.USBKeyboard==CONFIG_UK)keylayout=UKkeyValue;
  else if(Option.USBKeyboard==CONFIG_US)keylayout=USkeyValue;
  else if(Option.USBKeyboard==CONFIG_GR)keylayout=DEkeyValue;
  else if(Option.USBKeyboard==CONFIG_FR)keylayout=FRkeyValue;
  else if(Option.USBKeyboard==CONFIG_ES)keylayout=ESkeyValue;
  else if(Option.USBKeyboard==CONFIG_BE)keylayout=BEkeyValue;
  if(Current_USB_devices==4)error("USB device limit reached");

  // Interface protocol (hid_interface_protocol_enum_t)
//  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };

//  sprintf(buff,"HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);
	if ( itf_protocol == HID_ITF_PROTOCOL_KEYBOARD  )
	{
//		char buff[128];
//		hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
//			sprintf(buff,"HID has %u reports \r\n", hid_info[instance].report_count);
//			MMPrintString(buff);
		HID[slot].Device_address = dev_addr;
		HID[slot].Device_instance = instance;
		HID[slot].Device_type=HID_ITF_PROTOCOL_KEYBOARD;
		caps_lock=Option.capslock;
		num_lock=Option.numlock;
		if(num_lock) HID[slot].sendlights|=(uint8_t)1;
		if(caps_lock) HID[slot].sendlights|=(uint8_t)2;
		HID[slot].report_rate=20; //mSec between report
		HID[slot].report_timer=-(10+(slot+2)*500);
		HID[slot].active=true;
		HID[slot].report_requested=false;
		if(!CurrentLinePtr) {
			MMPrintString((char *)KBrdList[(int)Option.USBKeyboard]);
			MMPrintString(" USB Keyboard Connected on channel ");
			PInt(slot+1);
			MMPrintString("\r\n> ");
		}
//		tuh_hid_set_report(HID[slot].Device_address, HID[slot].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights,1);
		Current_USB_devices++;
		return;
  	}
  // By default host stack will use activate boot protocol on supported interface.
  // Therefore for this simple example, we only need to parse generic report descriptor (with built-in parser)
	if ( itf_protocol == HID_ITF_PROTOCOL_MOUSE )
	{
//		char mode[]={0xf3, 0xc8, 0xf3, 0x64, 0xf3, 0x50};
//		hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
	//		printf("HID has %u reports \r\n", hid_info[instance].report_count);
		HID[slot].Device_address = dev_addr;
		HID[slot].Device_instance = instance;
		HID[slot].Device_type=HID_ITF_PROTOCOL_MOUSE;
		HID[slot].report_rate=20; //mSec between reports
		HID[slot].report_timer=-(10+(slot+2)*500);
		HID[slot].active=true;
		HID[slot].report_requested=false;
		if(!CurrentLinePtr) {MMPrintString("USB Mouse Connected on channel ");PInt(slot+1);MMPrintString("\r\n> ");}
//		tuh_hid_send_report(HID[slot].Device_address, HID[slot].Device_instance,5,mode, sizeof(mode));
//		tuh_hid_set_report(HID[slot].Device_address, HID[slot].Device_instance, 0, HID_REPORT_TYPE_INPUT, mode, sizeof(mode));
		Current_USB_devices++;
		return;
	}
	if ( itf_protocol == HID_ITF_PROTOCOL_NONE )
	{
//		hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
	// Sony DualShock 4 [CUH-ZCT2x]
		if ( is_sony_ds4(dev_addr) )
		{ 
			if(!CurrentLinePtr) {MMPrintString("PS4 Controller Connected on channel ");PInt(slot+1);MMPrintString("\r\n> ");}
			HID[slot].Device_address = dev_addr;
			HID[slot].Device_instance = instance;
			HID[slot].Device_type=PS4;
			HID[slot].report_rate=20; //mSec between reports
			HID[slot].report_timer=-(10+(slot+2)*500);
			HID[slot].active=true;
			HID[slot].report_requested=false;
			HID[slot].motorleft=0;
			HID[slot].motorright=0;
		}
		else if ( is_sony_ds3(dev_addr) )
		{
			if(!CurrentLinePtr) {MMPrintString("PS3 Controller Connected on channel ");PInt(slot+1);MMPrintString("\r\n> ");}
			HID[slot].Device_address = dev_addr;
			HID[slot].Device_instance = instance;
			HID[slot].Device_type=PS3;
			HID[slot].report_rate=20; //mSec between reports
			HID[slot].report_timer=-(10+(slot+2)*500);
			HID[slot].active=true;
		 	HID[slot].report_requested=false;
		}
		else if ( is_xbox(dev_addr) )
		{
			if(!CurrentLinePtr) {MMPrintString("XBox Controller Connected on channel ");PInt(slot+1);MMPrintString("\r\n> ");}
			HID[slot].Device_address = dev_addr;
			HID[slot].Device_instance = instance;
			HID[slot].Device_type=XBOX;
			HID[slot].report_rate=20; //mSec between reports
			HID[slot].report_timer=-(10+(slot+2)*500);
			HID[slot].active=true;
		 	HID[slot].report_requested=false;
		} else {
			if(!CurrentLinePtr) {MMPrintString("Generic Gamepad Connected on channel ");PInt(slot+1);MMPrintString("\r\n> ");}
			HID[slot].Device_address = dev_addr;
			HID[slot].Device_instance = instance;
			HID[slot].report_timer=-(10+(slot+2)*500);
			HID[slot].active=false;
			HID[slot].report_rate=20; //mSec between reports
			HID[slot].Device_type=SNES;
			HID[slot].active=true;
			HID[slot].report_requested=false;
		}
	}
	Current_USB_devices++;
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	__dsb();
	int i;
	for(i=0;i<4;i++){
//		PInt(i);PIntHC(HID[i].Device_type);PRet();
		if(instance==HID[i].Device_instance && dev_addr==HID[i].Device_address && HID[i].Device_type==HID_ITF_PROTOCOL_KEYBOARD){
			if(!CurrentLinePtr) MMPrintString("USB Keyboard Disconnected\r\n> ");
			break;
		}
		else if(instance==HID[i].Device_instance && dev_addr==HID[i].Device_address && HID[i].Device_type==HID_ITF_PROTOCOL_MOUSE){
			if(!CurrentLinePtr) MMPrintString("USB Mouse Disconnected\r\n> ");
			break;
		}
		else if(instance==HID[i].Device_instance && dev_addr==HID[i].Device_address && HID[i].Device_type==PS4){
			if(!CurrentLinePtr) MMPrintString("PS4 Controller Disconnected\r\n> ");
			break;
		}
		else if(instance==HID[i].Device_instance && dev_addr==HID[i].Device_address && HID[i].Device_type==PS3){
			if(!CurrentLinePtr) MMPrintString("PS3 Controller Disconnected\r\n> ");
			break;
		}
		else if(instance==HID[i].Device_instance && dev_addr==HID[i].Device_address && HID[i].Device_type==XBOX){
			if(!CurrentLinePtr) MMPrintString("XBox Controller Disconnected\r\n> ");
			break;
		}
		else if(instance==HID[i].Device_instance && dev_addr==HID[i].Device_address && HID[i].Device_type==SNES){
			if(!CurrentLinePtr) MMPrintString("Generic Gamepad Disconnected\r\n> ");
			break;
		}
	}
	memset((void *)&HID[i],0,sizeof(struct s_HID));
	HID[i].report_requested=true;
	Current_USB_devices--;
//  sprintf(buff,"HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
}
// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
	__dsb();
	uint8_t n=255;
	uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
	for(int i=0;i<4;i++)if(instance==HID[i].Device_instance && dev_addr==HID[i].Device_address){
		n=i;
		break;
	}
	switch (itf_protocol)
	{
		case HID_ITF_PROTOCOL_KEYBOARD:
//		MMPrintString("HID receive boot keyboard report\r\n");
		process_kbd_report( (hid_keyboard_report_t const*) report ,n);
		break;

	case HID_ITF_PROTOCOL_MOUSE:
//		MMPrintString("HID receive boot mouse report\r\n");
//  PInt(report[0]);
//  for (int i=1;i<len;i++)PIntHC(report[i]);
//  PRet();
		process_mouse_report( (hid_mouse_report_t const*) report, n+1);
		break;

	default:
//		MMPrintString("HID receive boot gamepad report\r\n");
		if ( is_sony_ds4(dev_addr) )
		{
			process_sony_ds4(report, len, n+1);
		} 
		else if ( is_sony_ds3(dev_addr) )
		{
			process_sony_ds3(report, len, n+1);
		}			
		else if ( is_xbox(dev_addr) )
		{
			process_xbox(report, len, n+1);
		}  else {
			process_gamepad(report, len, n+1);
		}
	}
	HID[n].report_requested=false;
	HID[n].report_timer=0;
}

//--------------------------------------------------------------------+
// Keyboard
//--------------------------------------------------------------------+
uint8_t APP_MapKeyToUsage(uint8_t *report, int keyno, int modifier)
{
    uint8_t keyCode=report[keyno];
    if(keyCode == USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK
                    || keyCode == USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK
                    || keyCode == USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR) return 0;

    if(keyCode >= USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A && keyCode <= USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE)
    {
        if((modifier & KEYBOARD_MODIFIER_RIGHTALT)  && Option.USBKeyboard==CONFIG_GR){
        	if(keyCode==0x24)return 0x7B;
        	else if(keyCode==0x25)return 0x5B;
        	else if(keyCode==0x26)return 0x5D;
        	else if(keyCode==0x27)return 0x7D;
        	else if(keyCode==0x2D)return 0x5C;
        	else if(keyCode==0x14)return 0x40;
        	else if(keyCode==0x64)return 124;
        }
        if((modifier & KEYBOARD_MODIFIER_RIGHTALT) && Option.USBKeyboard==CONFIG_FR){
			 if(keyCode==0x1F)     return 126;// ~
			 else if(keyCode==0x20)return 35; // #
			 else if(keyCode==0x21)return 123;// {
			 else if(keyCode==0x22)return 91; // [
			 else if(keyCode==0x23)return 124;// |
			 else if(keyCode==0x24)return 96 ;// `
			 else if(keyCode==0x25)return 92; // '\'
			 else if(keyCode==0x26)return 94; // ^
			 else if(keyCode==0x27)return 64; // @
			 else if(keyCode==0x2D)return 93; // ]
			 else if(keyCode==0x2E)return 125;// }
        }
        if((modifier & KEYBOARD_MODIFIER_RIGHTALT) && Option.USBKeyboard == CONFIG_ES){
			if(keyCode==0x35)     return 92;   // backslash
			else if(keyCode==0x1E)return 124;  // |
			else if(keyCode==0x08)return 0;    // €
			else if(keyCode==0x1F)return 64;   // @
			else if(keyCode==0x20)return 35;   // #
			else if(keyCode==0x21)return 0;    // ~
			else if(keyCode==0x2F)return 91;   // [
			else if(keyCode==0x30)return 93;   // ]
			else if(keyCode==0x31)return 125;  // }
			else if(keyCode==0x34)return 123;  // {
			}
        if((modifier & KEYBOARD_MODIFIER_RIGHTALT) && Option.USBKeyboard == CONFIG_BE){
			if(keyCode==0x64)     return 92;   // backslash
			else if(keyCode==0x20)return 35;  // |
			else if(keyCode==0x2F)return 91;    // €
			else if(keyCode==0x30)return 93;   // @
			else if(keyCode==0x31)return 96;   // #
			else if(keyCode==0x34)return 39;    // ~
			else if(keyCode==0x38)return 126;   // [
        }
        if(keyCode >= USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A && keyCode <= USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z && ((modifier & KEYBOARD_MODIFIER_LEFTCTRL) || (modifier & KEYBOARD_MODIFIER_RIGHTCTRL))) //Ctrl Key pressed for normal alpha key
        {
            return (keylayout[keyCode<<1]-96);
        }
        if(keyCode >=USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH && keyCode<=USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE) //Key pressed on the numeric keypad
        {
        	if(num_lock)return (keylayout[keyCode<<1]);
        	else return (keylayout[(keyCode<<1)+1]);
        }
        if(keyCode >=USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A && keyCode<=USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z) //Alpha key pressed
        {
        	int toggle=1;
        	if(caps_lock)toggle=!toggle;
        	if((modifier & KEYBOARD_MODIFIER_LEFTSHIFT) || (modifier & KEYBOARD_MODIFIER_RIGHTSHIFT))toggle=!toggle;
        	if(toggle)return (keylayout[keyCode<<1]);
            else return (keylayout[(keyCode<<1)+1]);
        } else { //remaining keys
        	if(!((modifier & KEYBOARD_MODIFIER_LEFTSHIFT) || (modifier & KEYBOARD_MODIFIER_RIGHTSHIFT)))return (keylayout[keyCode<<1]);
            else return (keylayout[(keyCode<<1)+1]);
        }

    }
    return 0;
}
void USR_KEYBRD_ProcessData(uint8_t data)
{
  int sendCRLF=2;
	if(data==0)return;
	if(data=='\n'){
		if(sendCRLF==3)USR_KEYBRD_ProcessData('\r');
		if(sendCRLF==2)data='\r';
	}
	if(data==keyselect && KeyInterrupt!=NULL){
		Keycomplete=1;
		return;
	}
	ConsoleRxBuf[ConsoleRxBufHead]  = data;   // store the byte in the ring buffer
	if(BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey) {// if the user wants to stop the progran
		MMAbort = true;                                         // set the flag for the interpreter to see
		ConsoleRxBufHead = ConsoleRxBufTail;                    // empty the buffer
		return;
	}
	ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;     // advance the head of the queue
	if(ConsoleRxBufHead == ConsoleRxBufTail) {                           // if the buffer has overflowed
		ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
	}
}

static void process_kbd_report(hid_keyboard_report_t const *report, uint8_t n)
{
  static uint8_t prev_keys[6] = {0}; // previous report to check key released
  int total=0;
  static int lasttotal=0;
  uint8_t current_keys[6]={0};
  int modifier=report->modifier;
  for(int i=0;i<6;i++){
    if(report->keycode[i])total++;
  }
  if(total==0)lasttotal=0;
  for(int i=0;i<total;i++)current_keys[i]=report->keycode[total-i-1];
  //------------- example code ignore control (non-printable) key affects -------------//
		if(((keytimer>Option.RepeatStart || current_keys[0]!=prev_keys[0]) && total>=lasttotal)){
			keytimer=0;
        // not existed in previous report means the current key is pressed
//        bool const is_shift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
			if(current_keys[0]==0x39){
				if(caps_lock){
					HID[n].sendlights&=~(uint8_t)2;
					caps_lock=0;
          tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights,1);
				} else {
					HID[n].sendlights|=0x02;
					caps_lock=1;
          tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights,1);
				}
			} else if(current_keys[0]==0x53){
				if(num_lock){
					HID[n].sendlights&=~(uint8_t)1;
					num_lock=0;
          tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights,1);
				} else {
					HID[n].sendlights|=0x01;
					num_lock=1;
          tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights,1);
				}
			} else if(current_keys[0]==0x47){
				if(scroll_lock){
					HID[n].sendlights&=~(uint8_t)4;
					scroll_lock=0;
          tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights,1);
				} else {
					HID[n].sendlights|=0x04;
					scroll_lock=1;
          tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights,1);
				}
			} else {
				uint8_t c = APP_MapKeyToUsage(current_keys,0, modifier);
				if (c != 0)
				{
					USR_KEYBRD_ProcessData(c);
		      repeattime=Option.RepeatStart;
				}
      }
      memcpy(prev_keys ,current_keys, sizeof(prev_keys));
    } 
    lasttotal=total;
    for(int i=0;i<6;i++){
        uint8_t c = APP_MapKeyToUsage(current_keys, i, modifier);
        if (c != 0)	KeyDown[i]=c;
        else KeyDown[i]=0;
    }
    KeyDown[6]=(modifier & KEYBOARD_MODIFIER_LEFTALT ? 1: 0) |
        (modifier & KEYBOARD_MODIFIER_LEFTCTRL ? 2: 0) |
        (modifier & KEYBOARD_MODIFIER_LEFTGUI ? 4: 0) |
        (modifier & KEYBOARD_MODIFIER_LEFTSHIFT ? 8: 0) |
        (modifier & KEYBOARD_MODIFIER_RIGHTALT ? 16: 0) |
        (modifier & KEYBOARD_MODIFIER_RIGHTCTRL ? 32: 0) |
        (modifier & KEYBOARD_MODIFIER_RIGHTGUI ? 64: 0) |
        (modifier & KEYBOARD_MODIFIER_RIGHTSHIFT ? 128: 0);
}

//--------------------------------------------------------------------+
// Mouse
//--------------------------------------------------------------------+

void cursor_movement(int8_t x, int8_t y, int8_t wheel)
{
#if USE_ANSI_ESCAPE
  // Move X using ansi escape
  if ( x < 0)
  {
    sprintf(buff,ANSI_CURSOR_BACKWARD(%d), (-x)); // move left
  }else if ( x > 0)
  {
    sprintf(buff,ANSI_CURSOR_FORWARD(%d), x); // move right
  }

  // Move Y using ansi escape
  if ( y < 0)
  {
    sprintf(buff,ANSI_CURSOR_UP(%d), (-y)); // move up
  }else if ( y > 0)
  {
    sprintf(buff,ANSI_CURSOR_DOWN(%d), y); // move down
  }

  // Scroll using ansi escape
  if (wheel < 0)
  {
    sprintf(buff,ANSI_SCROLL_UP(%d), (-wheel)); // scroll up
  }else if (wheel > 0)
  {
    sprintf(buff,ANSI_SCROLL_DOWN(%d), wheel); // scroll down
  }

  sprintf(buff,"\r\n");
#else
    char buff[STRINGSIZE];
  sprintf(buff,"(%d %d %d)\r\n", x, y, wheel);
#endif
}

static void process_mouse_report(hid_mouse_report_t const * report, uint8_t n)
{
/*         if(checkstring(argv[2], (unsigned char *)"X"))iret=nunstruct[n].ax;
        else if(checkstring(argv[2], (unsigned char *)"Y"))iret=nunstruct[n].ay;
        else if(checkstring(argv[2], (unsigned char *)"L"))iret=nunstruct[n].L;
        else if(checkstring(argv[2], (unsigned char *)"R"))iret=nunstruct[n].R;
        else if(checkstring(argv[2], (unsigned char *)"W"))iret=nunstruct[n].az;
        else if(checkstring(argv[2], (unsigned char *)"D"))iret=nunstruct[n].Z;*/

  //------------- button state  -------------//
    static uint64_t leftpress=0;
	static uint8_t leftstate=0;
	uint64_t timenow=time_us_64();
	if(timenow-leftpress>500000){
		leftstate=0;
		nunstruct[n].Z=0;
	}
	if(leftstate==0 && (report->buttons & MOUSE_BUTTON_LEFT)){
		leftpress=timenow;
		leftstate=1;
	}
	if(leftstate==1 && !(report->buttons & MOUSE_BUTTON_LEFT)){ //
		leftpress=timenow;
		leftstate=2;
	}
	if(leftstate==2 && (report->buttons & MOUSE_BUTTON_LEFT)){ //second press within 500mSec
	    if(timenow-leftpress>100000){
			leftpress=timenow;
			leftstate=3;
			nunstruct[n].Z=1;
			nunfoundc[n]=1;
		} else {
			leftstate=0;
		}
		
	}
	nunstruct[n].L=report->buttons & MOUSE_BUTTON_LEFT   ? 1 : 0;
	nunstruct[n].R=report->buttons & MOUSE_BUTTON_RIGHT   ? 1 : 0;
	nunstruct[n].C=report->buttons & MOUSE_BUTTON_MIDDLE   ? 1 : 0;
	nunstruct[n].ax+=report->x/2;
	if(nunstruct[n].ax>=HRes)nunstruct[n].ax=HRes-1;
	if(nunstruct[n].ax<0)nunstruct[n].ax=0;
	nunstruct[n].ay+=report->y/2;
	if(nunstruct[n].ay>=VRes)nunstruct[n].ay=VRes-1;
	if(nunstruct[n].ay<0)nunstruct[n].ay=0;
	nunstruct[n].az+=report->wheel;
	if(nunstruct[n].x0!=(report->buttons & 0b111)){
		nunfoundc[n]=1;
	}
	nunstruct[n].x0=report->buttons & 0b111;
}

  //------------- cursor movement -------------//
//  cursor_movement(report->x, report->y, report->wheel);
//}
void cmd_gamepad(unsigned char *p){
	unsigned char *tp=NULL;
	int n;
	if((tp=checkstring(p,(unsigned char *)"INTERRUPT ENABLE"))){
		getargs(&tp,5,(unsigned char *)",");
		if(!(argc==3 || argc==5))error("Syntax");
		n=getint(argv[0],1,4);
		nunInterruptc[n] = (char *)GetIntAddress(argv[2]);					// get the interrupt location
		InterruptUsed = true;
		nunstruct[n].x1=0b1111111111111111;
		if(argc==5)nunstruct[n].x1=getint(argv[4],0,0b1111111111111111);
		return;
	} else if((tp = checkstring(p, (unsigned char *)"HAPTIC"))){
		getargs(&tp,5,(unsigned char *)",");
		if(!(argc==5))error("Syntax");
		n=getint(argv[0],1,4)-1;
		if(HID[n].Device_type!=PS4)error("PS4 only");
		HID[n].motorleft=getint(argv[2],0,255);
		HID[n].motorright=getint(argv[4],0,255);
	} else if((tp = checkstring(p, (unsigned char *)"COLOUR"))){
		getargs(&tp,3,(unsigned char *)",");
		if(!(argc==3))error("Syntax");
		n=getint(argv[0],1,4)-1;
		if(HID[n].Device_type!=PS4)error("PS4 only");
		int colour=getint(argv[2],0,0xFFFFFF);
		HID[n].r=colour>>16;
		HID[n].g=(colour>>8) & 0xff;
		HID[n].b=colour & 0xff;
	} else if((tp = checkstring(p, (unsigned char *)"HAPTIC"))){
		getargs(&tp,5,(unsigned char *)",");
		if(!(argc==5))error("Syntax");
		n=getint(argv[0],1,4)-1;
		if(HID[n].Device_type!=PS4)error("PS4 only");
		HID[n].motorleft=getint(argv[2],0,255);
		HID[n].motorright=getint(argv[4],0,255);
	} else if((tp = checkstring(p, (unsigned char *)"INTERRUPT DISABLE"))){
		getargs(&tp,1,(unsigned char *)",");
		n=getint(argv[0],1,4);
		nunInterruptc[n]=NULL;
	} else error("Syntax");
}
void cmd_mouse(unsigned char *p){
	unsigned char *tp=NULL;
	int n;
	if((tp=checkstring(p,(unsigned char *)"INTERRUPT ENABLE"))){
		getargs(&tp,3,(unsigned char *)",");
		if(!(argc==3))error("Syntax");
		n=getint(argv[0],1,4);
		nunInterruptc[n] = (char *)GetIntAddress(argv[2]);					// get the interrupt location
		InterruptUsed = true;
		return;
	} else if((tp = checkstring(p, (unsigned char *)"SET"))){
		getargs(&tp,7,(unsigned char *)",");
		if(!(argc==7))error("Syntax");
		n=getint(argv[0],1,4);
		nunstruct[n].ax=getint(argv[2],-HRes,HRes);
		nunstruct[n].ay=getint(argv[4],-VRes,VRes);
		nunstruct[n].az=getint(argv[6],-1000000,1000000); 
	} else if((tp = checkstring(p, (unsigned char *)"INTERRUPT DISABLE"))){
		getargs(&tp,1,(unsigned char *)",");
		n=getint(argv[0],1,4);
		nunInterruptc[n]=NULL;
	} else error("Syntax");
}


