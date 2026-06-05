/*
 * KeyboardMap.c -- shared keymap data + 8-byte HID boot-keyboard report
 * decoder. Used by both transports:
 *   - USB HID host (USBKeyboard.c is the upper layer)
 *   - BLE HID host / HOG (BTKeyboard.c is the upper layer)
 *
 * The HID report format is identical across transports; only the
 * keymap arrays, scancode-to-ASCII lookup, debounced ring-push, and
 * pressed/released tracking are common. Extracted from USBKeyboard.c
 * so the BLE build does not duplicate the international layouts.
 *
 * The LED-set call sites inside process_key are gated by
 * #ifdef USBKEYBOARD because they reference HID[] (USB-host device
 * table) and tuh_hid_set_report (TinyUSB host). The BLE build does
 * not drive remote-keyboard LEDs anyway; the phone-side firmware
 * manages them.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "tusb.h"
#include "KeyboardMap.h"
#include "I2C.h"   /* nunstruct[], nunfoundc[] -- shared mouse/gamepad state */

extern volatile int ConsoleRxBufHead;
extern volatile int ConsoleRxBufTail;
extern const char *KBrdList[];

/* Mouse button bitmask constants. Provided by tinyusb's class/hid/hid.h
   in the USB-host build (pulled in via tusb.h above); declared locally
   for the BLE-HID-host build where tinyusb's HID class headers aren't
   included. Guarded against tinyusb's TUSB_HID_H_ sentinel. */
#ifndef TUSB_HID_H_
#define MOUSE_BUTTON_LEFT    0x01
#define MOUSE_BUTTON_RIGHT   0x02
#define MOUSE_BUTTON_MIDDLE  0x04
#endif

/* Definitions of state shared with other translation units. USBcode is
   read by the MM.INFO(USB) BASIC function (Functions.c); repeattime is
   used by APP_MapKeyToUsage's auto-repeat logic in process_key below;
   keytimer is incremented by the periodic timer in PicoMite.c and
   reset by process_key/process_kbd_report on each key event.
   These previously lived in USBKeyboard.c / PicoMite.c — moved here
   so the BLE-HID-host build (which doesn't link USBKeyboard.c) still
   resolves them. */
int USBcode = -1;
uint32_t repeattime;
volatile int keytimer = 0;

/* PS/2 scancodes carried over from the original USBKeyboard.c.
   Currently unused by the keymap functions below, but a few BASIC
   commands historically reference them. */
#define CTRL 0x14
#define L_SHFT 0x12
#define R_SHFT 0x59
#define CAPS 0x58
#define NUML 0x77

const int UKkeyValue[202] = {
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
	97, 65,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                       = 0x04,
	98, 66,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                       = 0x05,
	99, 67,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                       = 0x06,
	100, 68,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                       = 0x07,
	101, 69,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                       = 0x08,
	102, 70,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                       = 0x09,
	103, 71,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                       = 0x0A,
	104, 72,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                       = 0x0B,
	105, 73,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                       = 0x0C,
	106, 74,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                       = 0x0D,
	107, 75,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                       = 0x0E,
	108, 76,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                       = 0x0F,
	109, 77,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                       = 0x10,
	110, 78,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                       = 0x11,
	111, 79,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                       = 0x12,
	112, 80,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                       = 0x13,
	113, 81,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                       = 0x14,
	114, 82,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                       = 0x15,
	115, 83,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                       = 0x16,
	116, 84,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                       = 0x17,
	117, 85,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                       = 0x18,
	118, 86,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                       = 0x19,
	119, 87,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                       = 0x1A,
	120, 88,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                       = 0x1B,
	121, 89,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                       = 0x1C,
	122, 90,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                       = 0x1D,
	49, 33,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT                 = 0x1E,
	50, 34,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                                = 0x1F,  QUOTE UK!!!!
	51, 35,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                              = 0x20,
	52, 36,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                            = 0x21,
	53, 37,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                           = 0x22,
	54, 94,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                            = 0x23,
	55, 38,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                         = 0x24,
	56, 42,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                          = 0x25,
	57, 40,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                  = 0x26,
	48, 41,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS                 = 0x27,
	10, 10,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                            = 0x28,
	27, 27,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                  = 0x29,
	8, 8,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
	9, 0x9f,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
	32, 32,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                                = 0x2C,
	45, 95,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                    = 0x2D,
	61, 43,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                          = 0x2E,
	91, 123,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE       = 0x2F,
	93, 125,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE     = 0x30,
	92, 124,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31,
	35, 126,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                   = 0x32,
	59, 58,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                     = 0x33,
	39, 64,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                    = 0x34, @ UK///
	96, 126,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                  = 0x35,
	44, 60,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                     = 0x36,
	46, 62,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN                 = 0x37,
	47, 63,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK         = 0x38,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
	0x91, 0xB1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92, 0xB2, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93, 0xB3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94, 0xB4, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95, 0xB5, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96, 0xB6, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97, 0xB7, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98, 0xB8, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99, 0xB9, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a, 0xBa, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b, 0xBb, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c, 0xBc, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d, 0xBd, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e, 0x9e, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84, 0x84, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                  = 0x49,
	0x86, 0x86, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                    = 0x4A,
	0x88, 0x88, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                                 = 0x4B,
	0x7f, 0xa0, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                          = 0x4C,
	0x87, 0x87, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                     = 0x4D,
	0x89, 0x89, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                               = 0x4E,
	0x83, 0xA3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                             = 0x4F,
	0x82, 0x82, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                              = 0x50,
	0x81, 0xA1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                              = 0x51,
	0x80, 0x80, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                                = 0x52,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
	47, 47,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                                = 0x54,
	42, 42,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                  = 0x55,
	45, 45,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                     = 0x56,
	43, 43,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                      = 0x57,
	10, 10,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                     = 0x58,
	49, 0x87,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                                 = 0x59,
	50, 0x81,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                          = 0x5A,
	51, 0x89,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                           = 0x5B,
	52, 0x82,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                          = 0x5C,
	53, 53,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                         = 0x5D,
	54, 0x83,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                         = 0x5E,
	55, 0x86,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                                = 0x5F,
	56, 0x80,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                            = 0x60,
	57, 0x88,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                             = 0x61,
	48, 0x84,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                              = 0x62,
	46, 0x7f,	// USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                         = 0x63,
	92, 124		// USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE           = 0x64,
};

const int USkeyValue[202] = {
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
	97, 65,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                       = 0x04,
	98, 66,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                       = 0x05,
	99, 67,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                       = 0x06,
	100, 68,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                       = 0x07,
	101, 69,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                       = 0x08,
	102, 70,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                       = 0x09,
	103, 71,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                       = 0x0A,
	104, 72,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                       = 0x0B,
	105, 73,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                       = 0x0C,
	106, 74,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                       = 0x0D,
	107, 75,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                       = 0x0E,
	108, 76,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                       = 0x0F,
	109, 77,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                       = 0x10,
	110, 78,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                       = 0x11,
	111, 79,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                       = 0x12,
	112, 80,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                       = 0x13,
	113, 81,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                       = 0x14,
	114, 82,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                       = 0x15,
	115, 83,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                       = 0x16,
	116, 84,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                       = 0x17,
	117, 85,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                       = 0x18,
	118, 86,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                       = 0x19,
	119, 87,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                       = 0x1A,
	120, 88,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                       = 0x1B,
	121, 89,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                       = 0x1C,
	122, 90,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                       = 0x1D,
	49, 33,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT                 = 0x1E,
	50, 64,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                                = 0x1F,  QUOTE UK!!!!
	51, 35,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                              = 0x20,
	52, 36,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                            = 0x21,
	53, 37,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                           = 0x22,
	54, 94,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                            = 0x23,
	55, 38,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                         = 0x24,
	56, 42,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                          = 0x25,
	57, 40,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                  = 0x26,
	48, 41,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS                 = 0x27,
	10, 10,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                            = 0x28,
	27, 27,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                  = 0x29,
	8, 8,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
	9, 0x9f,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
	32, 32,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                                = 0x2C,
	45, 95,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                    = 0x2D,
	61, 43,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                          = 0x2E,
	91, 123,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE       = 0x2F,
	93, 125,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE     = 0x30,
	92, 124,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31,
	92, 124,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                   = 0x32,
	59, 58,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                     = 0x33,
	39, 34,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                    = 0x34, @ UK///
	96, 126,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                  = 0x35,
	44, 60,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                     = 0x36,
	46, 62,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN                 = 0x37,
	47, 63,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK         = 0x38,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
	0x91, 0xB1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92, 0xB2, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93, 0xB3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94, 0xB4, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95, 0xB5, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96, 0xB6, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97, 0xB7, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98, 0xB8, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99, 0xB9, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a, 0xBa, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b, 0xBb, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c, 0xBc, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d, 0xBd, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e, 0x9e, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84, 0x84, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                  = 0x49,
	0x86, 0x86, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                    = 0x4A,
	0x88, 0x88, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                                 = 0x4B,
	0x7f, 0xa0, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                          = 0x4C,
	0x87, 0x87, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                     = 0x4D,
	0x89, 0x89, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                               = 0x4E,
	0x83, 0xA3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                             = 0x4F,
	0x82, 0x82, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                              = 0x50,
	0x81, 0xA1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                              = 0x51,
	0x80, 0x80, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                                = 0x52,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
	47, 47,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                                = 0x54,
	42, 42,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                  = 0x55,
	45, 45,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                     = 0x56,
	43, 43,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                      = 0x57,
	10, 10,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                     = 0x58,
	49, 0x87,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                                 = 0x59,
	50, 0x81,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                          = 0x5A,
	51, 0x89,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                           = 0x5B,
	52, 0x82,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                          = 0x5C,
	53, 53,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                         = 0x5D,
	54, 0x83,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                         = 0x5E,
	55, 0x86,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                                = 0x5F,
	56, 0x80,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                            = 0x60,
	57, 0x88,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                             = 0x61,
	48, 0x84,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                              = 0x62,
	46, 0x7f,	// USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                         = 0x63,
	92, 124		// USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE           = 0x64,
};
const int DEkeyValue[202] = {
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
	97, 65,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                       = 0x04,
	98, 66,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                       = 0x05,
	99, 67,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                       = 0x06,
	100, 68,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                       = 0x07,
	101, 69,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                       = 0x08,
	102, 70,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                       = 0x09,
	103, 71,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                       = 0x0A,
	104, 72,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                       = 0x0B,
	105, 73,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                       = 0x0C,
	106, 74,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                       = 0x0D,
	107, 75,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                       = 0x0E,
	108, 76,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                       = 0x0F,
	109, 77,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                       = 0x10,
	110, 78,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                       = 0x11,
	111, 79,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                       = 0x12,
	112, 80,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                       = 0x13,
	113, 81,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                       = 0x14,
	114, 82,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                       = 0x15,
	115, 83,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                       = 0x16,
	116, 84,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                       = 0x17,
	117, 85,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                       = 0x18,
	118, 86,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                       = 0x19,
	119, 87,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                       = 0x1A,
	120, 88,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                       = 0x1B,
	122, 90,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                       = 0x1C,
	121, 89,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                       = 0x1D,
	49, 33,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT                 = 0x1E,
	50, 34,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                                = 0x1F,
	51, 200,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                              = 0x20,      DE: §=245
	52, 36,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                            = 0x21,
	53, 37,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                           = 0x22,
	54, 38,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                            = 0x23,
	55, 47,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                         = 0x24,
	56, 40,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                          = 0x25,
	57, 41,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                  = 0x26,
	48, 61,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS                 = 0x27,
	10, 10,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                            = 0x28,
	27, 27,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                  = 0x29,
	8, 8,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
	9, 0x9f,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
	32, 32,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                                = 0x2C,
	201, 63,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                    = 0x2D,        DE: ß=225
	202, 96,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                          = 0x2E,   DE: ´=239
	203, 204,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE       = 0x2F,         DE: ü=129,Ü=154
	43, 42,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE     = 0x30,
	35, 39,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31,
	35, 39,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                   = 0x32,
	205, 206,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                     = 0x33,   DE: ö=228,Ö=229
	207, 208,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                    = 0x34,   DE: ä=132,Ä=142
	94, 209,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                  = 0x35,   DE: °=167
	44, 59,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                     = 0x36,
	46, 58,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN                 = 0x37,
	45, 95,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK         = 0x38,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
	0x91, 0xB1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92, 0xB2, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93, 0xB3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94, 0xB4, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95, 0xB5, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96, 0xB6, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97, 0xB7, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98, 0xB8, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99, 0xB9, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a, 0xBa, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b, 0xBb, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c, 0xBc, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d, 0xBd, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e, 0x9e, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84, 0x84, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                  = 0x49,
	0x86, 0x86, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                    = 0x4A,
	0x88, 0x88, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                                 = 0x4B,
	0x7f, 0xa0, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                          = 0x4C,
	0x87, 0x87, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                     = 0x4D,
	0x89, 0x89, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                               = 0x4E,
	0x83, 0xA3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                             = 0x4F,
	0x82, 0x82, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                              = 0x50,
	0x81, 0xA1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                              = 0x51,
	0x80, 0x80, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                                = 0x52,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
	47, 47,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                                = 0x54,
	42, 42,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                  = 0x55,
	45, 45,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                     = 0x56,
	43, 43,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                      = 0x57,
	10, 10,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                     = 0x58,
	49, 0x87,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                                 = 0x59,
	50, 0x81,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                          = 0x5A,
	51, 0x89,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                           = 0x5B,
	52, 0x82,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                          = 0x5C,
	53, 53,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                         = 0x5D,
	54, 0x83,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                         = 0x5E,
	55, 0x86,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                                = 0x5F,
	56, 0x80,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                            = 0x60,
	57, 0x88,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                             = 0x61,
	48, 0x84,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                              = 0x62,
	46, 0x7f,	// USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                         = 0x63,
	60, 62		// USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE           = 0x64,
};
const int FRkeyValue[202] = {
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
	113, 81,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                    = 0x04, FR Q
	98, 66,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                     = 0x05,
	99, 67,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                     = 0x06,
	100, 68,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                    = 0x07,
	101, 69,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                    = 0x08,
	102, 70,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                    = 0x09,
	103, 71,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                    = 0x0A,
	104, 72,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                    = 0x0B,
	105, 73,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                    = 0x0C,
	106, 74,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                    = 0x0D,
	107, 75,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                    = 0x0E,
	108, 76,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                    = 0x0F,
	44, 63,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                     = 0x10, FR ,?
	110, 78,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                    = 0x11,
	111, 79,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                    = 0x12,
	112, 80,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                    = 0x13,
	97, 65,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                     = 0x14, FR A
	114, 82,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                    = 0x15,
	115, 83,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                    = 0x16,
	116, 84,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                    = 0x17,
	117, 85,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                    = 0x18,
	118, 86,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                    = 0x19,
	122, 90,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                    = 0x1A, FR Z
	120, 88,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                    = 0x1B,
	121, 89,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                    = 0x1C,
	119, 87,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                    = 0x1D, FR W
	38, 49,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT               = 0x1E, FR & 1
	233, 50,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                             = 0x1F, FR é 2 ~
	34, 51,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                            = 0x20, FR " 3 #
	39, 52,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                          = 0x21, FR ' 4 {
	40, 53,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                         = 0x22, FR ( 5 [
	45, 54,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                          = 0x23, FR - 6
	232, 55,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                      = 0x24, FR è 7 `
	95, 56,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                        = 0x25, FR _ 8 '\'
	135, 57,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS               = 0x26, FR ç 9 ^
	00, 48,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS               = 0x27, FR à 0 @
	10, 10,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                          = 0x28,
	27, 27,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                = 0x29,
	8, 8,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
	9, 0x9f,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
	32, 32,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                              = 0x2C,
	41, 00,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                  = 0x2D, FR ) ]
	61, 43,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                        = 0x2E,
	94, 168,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE    = 0x2F, FR ^ ¨
	36, 163,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE  = 0x30, FR $ £
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31, -- not present --
	42, 181,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                = 0x32, FR * µ
	109, 77,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                  = 0x33, FR M
	249, 37,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                 = 0x34, FR ù %
	178, 00,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE               = 0x35, FR ²
	59, 46,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                   = 0x36, FR ; .
	58, 47,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN               = 0x37, FR : /
	33, 167,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK      = 0x38, FR ! §
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
	0x91, 0xB1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92, 0xB2, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93, 0xB3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94, 0xB4, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95, 0xB5, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96, 0xB6, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97, 0xB7, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98, 0xB8, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99, 0xB9, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a, 0xBa, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b, 0xBb, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c, 0xBc, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d, 0xBd, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e, 0x9e, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84, 0x84, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                = 0x49,
	0x86, 0x86, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                  = 0x4A,
	0x88, 0x88, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                               = 0x4B,
	0x7f, 0xa0, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                    = 0x4C,
	0x87, 0x87, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                               = 0x4D,
	0x89, 0x89, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                         = 0x4E,
	0x83, 0xA3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                       = 0x4F,
	0x82, 0x82, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                        = 0x50,
	0x81, 0xA1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                        = 0x51,
	0x80, 0x80, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                          = 0x52,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
	47, 47,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                              = 0x54,
	42, 42,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                = 0x55,
	45, 45,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                   = 0x56,
	43, 43,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                    = 0x57,
	10, 10,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                   = 0x58,
	49, 0x87,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                             = 0x59,
	50, 0x81,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                      = 0x5A,
	51, 0x89,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                       = 0x5B,
	52, 0x82,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                      = 0x5C,
	53, 53,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                       = 0x5D,
	54, 0x83,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                     = 0x5E,
	55, 0x86,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                            = 0x5F,
	56, 0x80,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                        = 0x60,
	57, 0x88,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                         = 0x61,
	48, 0x84,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                          = 0x62,
	46, 0x7f,	// USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                     = 0x63,
	60, 62		// USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE          = 0x64, FR < >
};
const int ESkeyValue[202] = {
	0, 0,		//          USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                    = 0x00,
	0, 0,		//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                       = 0x01,
	0, 0,		//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                             = 0x02,
	0, 0,		//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                       = 0x03,
	97, 65,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                     = 0x04,
	98, 66,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                     = 0x05,
	99, 67,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                     = 0x06,
	100, 68,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                     = 0x07,
	101, 69,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                     = 0x08, ALT GR -> €
	102, 70,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                     = 0x09,
	103, 71,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                     = 0x0A,
	104, 72,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                     = 0x0B,
	105, 73,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                     = 0x0C,
	106, 74,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                     = 0x0D,
	107, 75,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                     = 0x0E,
	108, 76,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                     = 0x0F,
	109, 77,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                     = 0x10,
	110, 78,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                     = 0x11,
	111, 79,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                     = 0x12,
	112, 80,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                     = 0x13,
	113, 81,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                     = 0x14,
	114, 82,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                     = 0x15,
	115, 83,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                     = 0x16,
	116, 84,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                     = 0x17,
	117, 85,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                     = 0x18,
	118, 86,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                     = 0x19,
	119, 87,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                                     = 0x1A,
	120, 88,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                     = 0x1B,
	121, 89,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                                     = 0x1C,
	122, 90,	//       USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                     = 0x1D,·
	49, 33,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT               = 0x1E, ALT GR -> |
	50, 34,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                              = 0x1F, ALT GR -> @
	51, 0,		//         USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                            = 0x20, ALT GR -> #
	52, 36,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                          = 0x21, ALT GR -> ~
	53, 37,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                         = 0x22,
	54, 38,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                          = 0x23,
	55, 47,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                       = 0x24,
	56, 40,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                        = 0x25,
	57, 41,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                = 0x26,
	48, 61,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS               = 0x27,
	10, 10,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                          = 0x28,
	27, 27,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                = 0x29,
	8, 8,		//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                = 0x2A,
	9, 0x9f,	//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                   = 0x2B,
	32, 32,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                              = 0x2C,
	39, 63,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                  = 0x2D,
	0, 0,		//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                        = 0x2E,
	0, 94,		//         USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE     = 0x2F, ALT GR -> [
	43, 42,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE   = 0x30, ALT GR -> ]
	0, 0,		//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                   = 0x31, ALT GR -> }
	92, 0,		//         USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                 = 0x32,
	0, 0,		//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                   = 0x33,
	0, 0,		//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                  = 0x34, ALT GR -> {
	96, 96,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                = 0x35, ALT GR -> backslash  (Tecla º)
	44, 59,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                   = 0x36,
	46, 58,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN               = 0x37,
	45, 95,		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK       = 0x38,
	0, 0,		//          USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                             = 0x39,
	0x91, 0xB1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92, 0xB2, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93, 0xB3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94, 0xB4, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95, 0xB5, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96, 0xB6, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97, 0xB7, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98, 0xB8, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99, 0xB9, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a, 0xBa, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b, 0xBb, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c, 0xBc, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d, 0xBd, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e, 0x9e, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84, 0x84, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                = 0x49,
	0x86, 0x86, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                  = 0x4A,
	0x88, 0x88, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                               = 0x4B,
	0x7f, 0xa0, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                        = 0x4C,
	0x87, 0x87, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                   = 0x4D,
	0x89, 0x89, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                             = 0x4E,
	0x83, 0xA3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                           = 0x4F,
	0x82, 0x82, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                            = 0x50,
	0x81, 0xA1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                            = 0x51,
	0x80, 0x80, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                              = 0x52,
	0, 0,		//          USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                      = 0x53,
	47, 47,		//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                              = 0x54,
	42, 42,		//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                = 0x55,
	45, 45,		//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                   = 0x56,
	43, 43,		//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                    = 0x57,
	10, 10,		//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                   = 0x58,
	49, 0x87,	//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                               = 0x59,
	50, 0x81,	//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                        = 0x5A,
	51, 0x89,	//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                         = 0x5B,
	52, 0x82,	//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                        = 0x5C,
	53, 53,		//        USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                       = 0x5D,
	54, 0x83,	//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                       = 0x5E,
	55, 0x86,	//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                              = 0x5F,
	56, 0x80,	//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                          = 0x60,
	57, 0x88,	//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                           = 0x61,
	48, 0x84,	//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                            = 0x62,
	46, 0x7f,	//      USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                       = 0x63,
	60, 62		//        USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE         = 0x64,
};
const int BEkeyValue[202] = {
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED                      = 0x00,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER                         = 0x01,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL                               = 0x02,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED                         = 0x03,
	113, 81,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A                                       = 0x04,
	98, 66,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B                                       = 0x05,
	99, 67,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C                                       = 0x06,
	100, 68,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D                                       = 0x07,
	101, 69,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E                                       = 0x08,
	102, 70,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F                                       = 0x09,
	103, 71,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G                                       = 0x0A,
	104, 72,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H                                       = 0x0B,
	105, 73,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I                                       = 0x0C,
	106, 74,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J                                       = 0x0D,
	107, 75,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K                                       = 0x0E,
	108, 76,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L                                       = 0x0F,
	44, 63,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M                                       = 0x10,
	110, 78,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N                                       = 0x11,
	111, 79,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O                                       = 0x12,
	112, 80,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P                                       = 0x13,
	97, 65,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q                                       = 0x14,
	114, 82,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R                                       = 0x15,
	115, 83,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S                                       = 0x16,
	116, 84,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T                                       = 0x17,
	117, 85,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U                                       = 0x18,
	118, 86,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V                                       = 0x19,
	122, 90,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W                             = 0x1A,
	120, 88,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X                                       = 0x1B,
	121, 89,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y                             = 0x1C,
	119, 87,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z                                       = 0x1D,
	38, 49,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT                 = 0x1E,
	64, 50,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT                                = 0x1F,
	34, 51,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH                              = 0x20,
	39, 52,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR                            = 0x21,
	40, 53,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT                           = 0x22,
	36, 54,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT                            = 0x23,
	96, 55,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND                         = 0x24,
	33, 56,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK                          = 0x25,
	123, 57,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS                  = 0x26,
	125, 48,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS                 = 0x27,
	10, 10,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER                            = 0x28,
	27, 27,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE                                  = 0x29,
	8, 8,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE                                  = 0x2A,
	9, 9,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB                                     = 0x2B,
	32, 32,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR                                = 0x2C,
	41, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE                    = 0x2D,
	45, 95,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS                          = 0x2E,
	91, 94,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE       = 0x2F,
	36, 42,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE     = 0x30,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE                     = 0x31,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE                   = 0x32,
	109, 77,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON                     = 0x33,
	0, 37,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE                    = 0x34,
	35, 124,	//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE                  = 0x35,
	59, 46,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN                     = 0x36,
	58, 47,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN                 = 0x37,
	61, 43,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK         = 0x38,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK                               = 0x39,
	0x91, 0xB1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1                                = 0x3A,
	0x92, 0xB2, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2                                = 0x3B,
	0x93, 0xB3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3                                = 0x3C,
	0x94, 0xB4, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4                                = 0x3D,
	0x95, 0xB5, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5                                = 0x3E,
	0x96, 0xB6, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6                                = 0x3F,
	0x97, 0xB7, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7                                = 0x40,
	0x98, 0xB8, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8                                = 0x41,
	0x99, 0xB9, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9                                = 0x42,
	0x9a, 0xBa, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10                               = 0x43,
	0x9b, 0xBb, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11                               = 0x44,
	0x9c, 0xBc, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12                               = 0x45,
	0x9d, 0xBd, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN                      = 0x46,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK                             = 0x47,
	0x9e, 0x9e, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE                                   = 0x48,
	0x84, 0x84, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT                                  = 0x49,
	0x86, 0x86, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME                                    = 0x4A,
	0x88, 0x88, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP                                 = 0x4B,
	0x7f, 0xa0, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD                          = 0x4C,
	0x87, 0x87, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END                                     = 0x4D,
	0x89, 0x89, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN                               = 0x4E,
	0x83, 0xA3, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW                             = 0x4F,
	0x82, 0x82, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW                              = 0x50,
	0x81, 0xA1, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW                              = 0x51,
	0x80, 0x80, //    USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW                                = 0x52,
	0, 0,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR                        = 0x53,
	47, 47,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH                                = 0x54,
	42, 42,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK                                  = 0x55,
	45, 45,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS                                     = 0x56,
	43, 43,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS                                      = 0x57,
	10, 10,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER                                     = 0x58,
	49, 0x87,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END                                 = 0x59,
	50, 0x81,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW                          = 0x5A,
	51, 0x89,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN                           = 0x5B,
	52, 0x82,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW                          = 0x5C,
	53, 53,		//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_5                                         = 0x5D,
	54, 0x83,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW                         = 0x5E,
	55, 0x86,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME                                = 0x5F,
	56, 0x80,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW                            = 0x60,
	57, 0x88,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP                             = 0x61,
	48, 0x84,	//    USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT                              = 0x62,
	46, 0x7f,	// USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE                         = 0x63,
	60, 62		// USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE           = 0x64,
};

/* Shared keyboard-state globals. Referenced from external code (e.g.
   KeyDown[] is read by the KEYDOWN() BASIC function in Functions.c).
   Storage lives here so both USB and BTH builds share state. */
int caps_lock = 0;
int num_lock = 0;
int scroll_lock = 0;
int KeyDown[7];
const int *keylayout;

static bool notbefore(int i, uint8_t *current_keys, uint8_t *prev_keys);
static void process_key(int key, uint8_t n, int modifier);

/* HID Keyboard modifier byte bits. Defined here only when tinyusb's
   class/hid/hid.h has not already provided them (USB-host build pulls
   them in via tusb.h; BLE-HID-host build does not). Both layouts are
   identical -- these are standard HID Usage IDs. */
#ifndef TUSB_HID_H_
#define KEYBOARD_MODIFIER_LEFTCTRL 0x01
#define KEYBOARD_MODIFIER_LEFTSHIFT 0x02
#define KEYBOARD_MODIFIER_LEFTALT 0x04
#define KEYBOARD_MODIFIER_LEFTGUI 0x08
#define KEYBOARD_MODIFIER_RIGHTCTRL 0x10
#define KEYBOARD_MODIFIER_RIGHTSHIFT 0x20
#define KEYBOARD_MODIFIER_RIGHTALT 0x40
#define KEYBOARD_MODIFIER_RIGHTGUI 0x80
#endif

/* HID Usage IDs for keyboard scancodes. Moved here from USBKeyboard.c
   so the BLE-HID-host build can use them too. These values are the
   USB HID Usage Page 0x07 (Keyboard) IDs -- standard, not tinyusb-
   specific despite the USB_HID_ prefix on the names. */
typedef enum
{
	USB_HID_KEYBOARD_KEYPAD_RESERVED_NO_EVENT_INDICATED = 0x00,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_ROLL_OVER = 0x01,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POST_FAIL = 0x02,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ERROR_UNDEFINED = 0x03,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A = 0x04,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_B = 0x05,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_C = 0x06,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_D = 0x07,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_E = 0x08,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F = 0x09,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_G = 0x0A,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_H = 0x0B,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_I = 0x0C,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_J = 0x0D,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_K = 0x0E,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_L = 0x0F,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_M = 0x10,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_N = 0x11,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_O = 0x12,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_P = 0x13,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Q = 0x14,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_R = 0x15,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_S = 0x16,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_T = 0x17,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_U = 0x18,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_V = 0x19,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_W = 0x1A,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_X = 0x1B,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Y = 0x1C,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z = 0x1D,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_1_AND_EXCLAMATION_POINT = 0x1E,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_2_AND_AT = 0x1F,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_3_AND_HASH = 0x20,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_4_AND_DOLLAR = 0x21,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_5_AND_PERCENT = 0x22,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_6_AND_CARROT = 0x23,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_7_AND_AMPERSAND = 0x24,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_8_AND_ASTERISK = 0x25,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_9_AND_OPEN_PARENTHESIS = 0x26,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_0_AND_CLOSE_PARENTHESIS = 0x27,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN_ENTER = 0x28,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ESCAPE = 0x29,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE = 0x2A,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_TAB = 0x2B,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SPACEBAR = 0x2C,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MINUS_AND_UNDERSCORE = 0x2D,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_AND_PLUS = 0x2E,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPEN_BRACKET_AND_OPEN_CURLY_BRACE = 0x2F,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLOSE_BRACKET_AND_CLOSE_CURLY_BRACE = 0x30,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_BACK_SLASH_AND_PIPE = 0x31,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_HASH_AND_TILDE = 0x32,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEMICOLON_AND_COLON = 0x33,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APOSTROPHE_AND_QUOTE = 0x34,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_GRAVE_ACCENT_AND_TILDE = 0x35,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COMMA_AND_LESS_THAN = 0x36,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PERIOD_AND_GREATER_THAN = 0x37,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FORWARD_SLASH_AND_QUESTION_MARK = 0x38,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK = 0x39,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F1 = 0x3A,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F2 = 0x3B,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F3 = 0x3C,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F4 = 0x3D,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F5 = 0x3E,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F6 = 0x3F,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F7 = 0x40,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F8 = 0x41,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F9 = 0x42,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F10 = 0x43,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F11 = 0x44,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F12 = 0x45,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRINT_SCREEN = 0x46,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK = 0x47,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAUSE = 0x48,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INSERT = 0x49,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HOME = 0x4A,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_UP = 0x4B,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DELETE_FORWARD = 0x4C,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_END = 0x4D,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PAGE_DOWN = 0x4E,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ARROW = 0x4F,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ARROW = 0x50,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_DOWN_ARROW = 0x51,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UP_ARROW = 0x52,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR = 0x53,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH = 0x54,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_ASTERISK = 0x55,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_MINUS = 0x56,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS = 0x57,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_ENTER = 0x58,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_1_AND_END = 0x59,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_2_AND_DOWN_ARROW = 0x5A,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_3_AND_PAGE_DOWN = 0x5B,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_4_AND_LEFT_ARROW = 0x5C,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_5 = 0x5D,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_6_AND_RIGHT_ARROW = 0x5E,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_7_AND_HOME = 0x5F,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_8_AND_UP_ARROW = 0x60,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_9_AND_PAGE_UP = 0x61,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_0_AND_INSERT = 0x62,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE = 0x63,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE = 0x64,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_APPLICATION = 0x65,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POWER = 0x66,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EQUAL_SIZE = 0x67,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F13 = 0x68,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F14 = 0x69,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F15 = 0x6A,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F16 = 0x6B,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F17 = 0x6C,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F18 = 0x6D,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F19 = 0x6E,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F20 = 0x6F,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F21 = 0x70,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F22 = 0x71,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F23 = 0x72,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_F24 = 0x73,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EXECUTE = 0x74,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_HELP = 0x75,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MENU = 0x76,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SELECT = 0x77,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_STOP = 0x78,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_AGAIN = 0x79,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_UNDO = 0x7A,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CUT = 0x7B,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_COPY = 0x7C,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PASTE = 0x7D,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_FIND = 0x7E,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_MUTE = 0x7F,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_VOLUME_UP = 0x80,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_VOLUME_DOWN = 0x81,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LOCKING_CAPS_LOCK = 0x82,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LOCKING_NUM_LOCK = 0x83,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LOCKING_SCROLL_LOCK = 0x84,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_COMMA = 0x85,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_EQUAL_SIGN = 0x86,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL1 = 0x87,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL2 = 0x88,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL3 = 0x89,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL4 = 0x8A,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL5 = 0x8B,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL6 = 0x8C,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL7 = 0x8D,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL8 = 0x8E,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_INTERNATIONAL9 = 0x8F,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG1 = 0x90,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG2 = 0x91,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG3 = 0x92,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG4 = 0x93,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG5 = 0x94,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG6 = 0x95,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG7 = 0x96,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG8 = 0x97,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LANG9 = 0x98,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_ALTERNATE_ERASE = 0x99,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SYS_REQ_ATTENTION = 0x9A,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CANCEL = 0x9B,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLEAR = 0x9C,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_PRIOR = 0x9D,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RETURN = 0x9E,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SEPARATOR = 0x9F,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OUT = 0xA0,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_OPER = 0xA1,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CLEAR_AGAIN = 0xA2,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CR_SEL_PROPS = 0xA3,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_EX_SEL = 0xA4,
	/* Reserved                                                                         = 0xA5-0xAF */
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_00 = 0xB0,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_000 = 0xB1,
	USB_HID_KEYBOARD_KEYPAD_THOUSANDS_SEPARATOR = 0xB2,
	USB_HID_KEYBOARD_KEYPAD_DECIMAL_SEPARATOR = 0xB3,
	USB_HID_KEYBOARD_KEYPAD_CURRENCY_UNIT = 0xB4,
	USB_HID_KEYBOARD_KEYPAD_CURRENTY_SUB_UNIT = 0xB5,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_OPEN_PARENTHESIS = 0xB6,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_CLOSE_PARENTHESIS = 0xB7,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_OPEN_CURLY_BRACE = 0xB8,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_CLOSE_CURLY_BRACE = 0xB9,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_TAB = 0xBA,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACKSPACE = 0xBB,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_A = 0xBC,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_B = 0xBD,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_C = 0xBE,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_D = 0xBF,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_E = 0xC0,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_F = 0xC1,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_XOR = 0xC2,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_CARROT = 0xC3,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERCENT_SIGN = 0xC4,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_LESS_THAN = 0xC5,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_GREATER_THAN = 0xC6,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_AMPERSAND = 0xC7,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_DOUBLE_AMPERSAND = 0xC8,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_PIPE = 0xC9,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_DOUBLE_PIPE = 0xCA,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_COLON = 0xCB,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_HASH = 0xCC,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_SPACE = 0xCD,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_AT = 0xCE,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_EXCLAMATION_POINT = 0xCF,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_STORE = 0xD0,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_RECALL = 0xD1,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_CLEAR = 0xD2,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_ADD = 0xD3,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_SUBTRACT = 0xD4,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_MULTIPLY = 0xD5,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_MEMORY_DIVIDE = 0xD6,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_PLUS_MINUS = 0xD7,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_CLEAR = 0xD8,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_CLEAR_ENTRY = 0xD9,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_BINARY = 0xDA,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_OCTAL = 0xDB,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_DECIMAL = 0xDC,
	USB_HID_KEYBOARD_KEYPAD_KEYPAD_HEXADECIMAL = 0xDD,
	/* Reserved                                                                         = 0xDE-0xDF */
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_CONTROL = 0xE0,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_SHIFT = 0xE1,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_ALT = 0xE2,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_GUI = 0xE3,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_CONTROL = 0xE4,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_SHIFT = 0xE5,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_ALT = 0xE6,
	USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_GUI = 0xE7
	// 0xE8-0xFFFF reserved
} USB_HID_KEYBOARD_KEYPAD;

uint8_t APP_MapKeyToUsage(uint8_t keyCode, int modifier)
{
	if (keyCode == USB_HID_KEYBOARD_KEYPAD_KEYBOARD_CAPS_LOCK || keyCode == USB_HID_KEYBOARD_KEYPAD_KEYBOARD_SCROLL_LOCK || keyCode == USB_HID_KEYBOARD_KEYPAD_KEYPAD_NUM_LOCK_AND_CLEAR)
		return 0;

	if (keyCode >= USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A && keyCode <= USB_HID_KEYBOARD_KEYPAD_KEYBOARD_NON_US_FORWARD_SLASH_AND_PIPE)
	{
		if ((modifier & KEYBOARD_MODIFIER_RIGHTALT) && Option.USBKeyboard == CONFIG_GR)
		{
			if (keyCode == 0x24)
				return 0x7B;
			else if (keyCode == 0x25)
				return 0x5B;
			else if (keyCode == 0x26)
				return 0x5D;
			else if (keyCode == 0x27)
				return 0x7D;
			else if (keyCode == 0x2D)
				return 0x5C;
			else if (keyCode == 0x14)
				return 0x40;
			else if (keyCode == 0x64)
				return 124;
			else if (keyCode == 0x30)
				return 126; // 48  ~
		}
		if ((modifier & KEYBOARD_MODIFIER_RIGHTALT) && Option.USBKeyboard == CONFIG_FR)
		{
			if (keyCode == 0x1F)
				return 126; // ~
			else if (keyCode == 0x20)
				return 35; // #
			else if (keyCode == 0x21)
				return 123; // {
			else if (keyCode == 0x22)
				return 91; // [
			else if (keyCode == 0x23)
				return 124; // |
			else if (keyCode == 0x24)
				return 96; // `
			else if (keyCode == 0x25)
				return 92; // '\'
			else if (keyCode == 0x26)
				return 94; // ^
			else if (keyCode == 0x27)
				return 64; // @
			else if (keyCode == 0x2D)
				return 93; // ]
			else if (keyCode == 0x2E)
				return 125; // }
		}
		if ((modifier & KEYBOARD_MODIFIER_RIGHTALT) && Option.USBKeyboard == CONFIG_ES)
		{
			if (keyCode == 0x35)
				return 92; // backslash
			else if (keyCode == 0x1E)
				return 124; // |
			else if (keyCode == 0x08)
				return 0; // €
			else if (keyCode == 0x1F)
				return 64; // @
			else if (keyCode == 0x20)
				return 35; // #
			else if (keyCode == 0x21)
				return 0; // ~
			else if (keyCode == 0x2F)
				return 91; // [
			else if (keyCode == 0x30)
				return 93; // ]
			else if (keyCode == 0x31)
				return 125; // }
			else if (keyCode == 0x34)
				return 123; // {
		}
		if ((modifier & KEYBOARD_MODIFIER_RIGHTALT) && Option.USBKeyboard == CONFIG_BE)
		{
			if (keyCode == 0x64)
				return 92; // backslash
			else if (keyCode == 0x20)
				return 35; // |
			else if (keyCode == 0x2F)
				return 91; // €
			else if (keyCode == 0x30)
				return 93; // @
			else if (keyCode == 0x31)
				return 96; // #
			else if (keyCode == 0x34)
				return 39; // ~
			else if (keyCode == 0x38)
				return 126; // [
		}
		if (keyCode >= USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A && keyCode <= USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z && ((modifier & KEYBOARD_MODIFIER_LEFTCTRL) || (modifier & KEYBOARD_MODIFIER_RIGHTCTRL))) // Ctrl Key pressed for normal alpha key
		{
			return (keylayout[keyCode << 1] - 96);
		}
		if (keyCode >= USB_HID_KEYBOARD_KEYPAD_KEYPAD_BACK_SLASH && keyCode <= USB_HID_KEYBOARD_KEYPAD_KEYPAD_PERIOD_AND_DELETE) // Key pressed on the numeric keypad
		{
			if (num_lock)
				return (keylayout[keyCode << 1]);
			else
				return (keylayout[(keyCode << 1) + 1]);
		}
		if (keyCode >= USB_HID_KEYBOARD_KEYPAD_KEYBOARD_A && keyCode <= USB_HID_KEYBOARD_KEYPAD_KEYBOARD_Z) // Alpha key pressed
		{
			int toggle = 1;
			if (caps_lock)
				toggle = !toggle;
			if ((modifier & KEYBOARD_MODIFIER_LEFTSHIFT) || (modifier & KEYBOARD_MODIFIER_RIGHTSHIFT))
				toggle = !toggle;
			if (toggle)
				return (keylayout[keyCode << 1]);
			else
				return (keylayout[(keyCode << 1) + 1]);
		}
		else
		{ // remaining keys
			if (!((modifier & KEYBOARD_MODIFIER_LEFTSHIFT) || (modifier & KEYBOARD_MODIFIER_RIGHTSHIFT)))
				return (keylayout[keyCode << 1]);
			else
				return (keylayout[(keyCode << 1) + 1]);
		}
	}
	return 0;
}
void USR_KEYBRD_ProcessData(uint8_t data)
{
	static uint8_t lastdata;
	static uint64_t lasttime;
	if (data == lastdata && time_us_64() - lasttime < 25000)
		return;
	lasttime = time_us_64();
	lastdata = data;
	int sendCRLF = 2;
	if (data == 0)
		return;
	if (BreakKey && data == BreakKey)
	{										 // if the user wants to stop the progran
		MMAbort = true;						 // set the flag for the interpreter to see
		ConsoleRxBufHead = ConsoleRxBufTail; // empty the buffer
											 // break;
	}
	if (data == '\n')
	{
		if (sendCRLF == 3)
			USR_KEYBRD_ProcessData('\r');
		if (sendCRLF == 2)
			data = '\r';
	}
	if (data == keyselect && KeyInterrupt != NULL)
	{
		Keycomplete = 1;
		return;
	}
	ConsoleRxBuf[ConsoleRxBufHead] = data; // store the byte in the ring buffer
										   /*	if(BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey) {// if the user wants to stop the progran
												   MMAbort = true;                                         // set the flag for the interpreter to see
												   ConsoleRxBufHead = ConsoleRxBufTail;                    // empty the buffer
												   return;
											   }*/

	ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE; // advance the head of the queue
	if (ConsoleRxBufHead == ConsoleRxBufTail)
	{																	 // if the buffer has overflowed
		ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
	}
}
static void process_key(int key, uint8_t n, int modifier)
{
	keytimer = 0;
	/* Caps/Num/Scroll lock toggles. We always flip the local state
	   (so APP_MapKeyToUsage shifts case correctly on subsequent
	   keypresses). The remote-LED feedback via tuh_hid_set_report
	   only runs in the USB-host build — the BLE build doesn't drive
	   the keyboard's physical LEDs because the phone-side firmware
	   manages them locally, and tuh_hid_set_report / HID[] aren't
	   linked in that build anyway. */
#ifndef USBKEYBOARD
	(void)n; /* unused without USB-host LED feedback */
#endif
	if (key == 0x39)
	{ // Caps lock
		if (caps_lock)
		{
			caps_lock = 0;
#ifdef USBKEYBOARD
			HID[n].sendlights &= ~(uint8_t)2;
			tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights, 1);
#endif
		}
		else
		{
			caps_lock = 1;
#ifdef USBKEYBOARD
			HID[n].sendlights |= 0x02;
			tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights, 1);
#endif
		}
	}
	else if (key == 0x53)
	{ // Num lock
		if (num_lock)
		{
			num_lock = 0;
#ifdef USBKEYBOARD
			HID[n].sendlights &= ~(uint8_t)1;
			tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights, 1);
#endif
		}
		else
		{
			num_lock = 1;
#ifdef USBKEYBOARD
			HID[n].sendlights |= 0x01;
			tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights, 1);
#endif
		}
	}
	else if (key == 0x47)
	{ // Scroll lock
		if (scroll_lock)
		{
			scroll_lock = 0;
#ifdef USBKEYBOARD
			HID[n].sendlights &= ~(uint8_t)4;
			tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights, 1);
#endif
		}
		else
		{
			scroll_lock = 1;
#ifdef USBKEYBOARD
			HID[n].sendlights |= 0x04;
			tuh_hid_set_report(HID[n].Device_address, HID[n].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights, 1);
#endif
		}
	}
	else
	{ // normal key
		uint8_t c = APP_MapKeyToUsage(key, modifier);
		if (c != 0)
		{
			USR_KEYBRD_ProcessData(c);
			repeattime = Option.RepeatStart;
		}
	}
}
static bool notbefore(int i, uint8_t *current_keys, uint8_t *prev_keys)
{
	uint8_t test = current_keys[i];
	for (int j = 0; j < 6; j++)
	{
		if (prev_keys[j] == test)
			return false;
	}
	return true;
}
void process_kbd_report(hid_keyboard_report_t const *report, uint8_t n)
{
	static uint8_t prev_keys[6] = {0}; // previous report to check key released
	static bool lasterror = false;
	int total = 0;
	//	static int lasttotal=0;
	uint8_t current_keys[6] = {0};
	int modifier = report->modifier;
	for (int i = 0; i < 6; i++)
	{
		if (report->keycode[i])
			total++;
		if (report->keycode[i] > 0 && report->keycode[i] < 4)
		{
			lasterror = true;
			return;
		}
		lasterror = false;
	}
	for (int i = 0; i < total; i++)
		current_keys[i] = report->keycode[total - i - 1]; // Get local copy of keys pressed
	// now work out if any new keys are pressed
	if (total)
	{ // Anything to do
		if (lasterror && total == 2)
		{
			uint8_t t = current_keys[0];
			current_keys[0] = current_keys[1];
			current_keys[1] = t;
		}
		for (int k = total - 1; k >= 0; k--)
		{ // loop through all the pressed keys in reverse order
			if (notbefore(k, current_keys, prev_keys))
				process_key(current_keys[k], n, modifier);
		}
		if (current_keys[0] != prev_keys[0])
		{
			keytimer = 0;
			if (current_keys[0])
				USBcode = current_keys[0];
		}
		//		if(((keytimer>Option.RepeatStart || current_keys[0]!=prev_keys[0]) && total>=lasttotal)){
		//			process_key(current_keys[0], n, modifier);
		//		}
	}
	else
		keytimer = 0;
	memcpy(prev_keys, current_keys, sizeof(prev_keys));
	for (int i = 0; i < 6; i++)
	{
		uint8_t c = APP_MapKeyToUsage(current_keys[i], modifier);
		if (c != 0)
			KeyDown[i] = c;
		else
			KeyDown[i] = 0;
	}
	//    lasttotal=total;
	KeyDown[6] = (modifier & KEYBOARD_MODIFIER_LEFTALT ? 1 : 0) |
				 (modifier & KEYBOARD_MODIFIER_LEFTCTRL ? 2 : 0) |
				 (modifier & KEYBOARD_MODIFIER_LEFTGUI ? 4 : 0) |
				 (modifier & KEYBOARD_MODIFIER_LEFTSHIFT ? 8 : 0) |
				 (modifier & KEYBOARD_MODIFIER_RIGHTALT ? 16 : 0) |
				 (modifier & KEYBOARD_MODIFIER_RIGHTCTRL ? 32 : 0) |
				 (modifier & KEYBOARD_MODIFIER_RIGHTGUI ? 64 : 0) |
				 (modifier & KEYBOARD_MODIFIER_RIGHTSHIFT ? 128 : 0);
}


/* ============================================================================
 * Shared consumer-control (media key) decoder.
 *
 * A HID Consumer-Control report is a single 16-bit little-endian
 * consumer Usage ID (HID Usage Page 0x0C): the usage of the key being
 * pressed, or 0x0000 when all media keys are released. These keys have
 * no boot-keyboard scancode, so the boot-report decoder above never
 * sees them — both the BLE-HID-host (BTKeyboard.c) and USB-HID-host
 * builds route their 2-byte consumer reports here instead.
 *
 * We translate the common transport-control usages to the media-key
 * pseudo-ASCII codes declared in KeyboardMap.h and push them into the
 * console RX ring, so a BASIC program reads them through INKEY$ just
 * like the arrow / function keys. Only the press edge emits a code;
 * the trailing 0x0000 release is swallowed silently (we report it as
 * handled so the caller doesn't log it as an unknown report).
 * ============================================================================
 */
bool process_consumer_report(const uint8_t *report, uint16_t len)
{
	if (len != 2)
		return false; /* not a single-usage consumer report */

	uint16_t usage = (uint16_t)report[0] | ((uint16_t)report[1] << 8);
	uint8_t code;
	switch (usage)
	{
	case 0x0000:           /* all media keys released — nothing to emit */
		return true;
	case 0x00E9:           /* Volume Increment */
		code = MM_KEY_VOLUME_UP;
		break;
	case 0x00EA:           /* Volume Decrement */
		code = MM_KEY_VOLUME_DOWN;
		break;
	case 0x00E2:           /* Mute */
		code = MM_KEY_MUTE;
		break;
	case 0x00CD:           /* Play/Pause */
	case 0x00B0:           /* Play */
	case 0x00B1:           /* Pause */
		code = MM_KEY_PLAY_PAUSE;
		break;
	case 0x00B7:           /* Stop */
		code = MM_KEY_STOP;
		break;
	case 0x00B5:           /* Scan Next Track */
	case 0x00B3:           /* Fast Forward */
		code = MM_KEY_NEXT_TRACK;
		break;
	case 0x00B6:           /* Scan Previous Track */
	case 0x00B4:           /* Rewind */
		code = MM_KEY_PREV_TRACK;
		break;
	default:
		return false;      /* unrecognised — let the caller log it raw */
	}
	USR_KEYBRD_ProcessData(code);
	return true;
}

/* ============================================================================
 * Shared mouse post-decode helper.
 *
 * Both the USB-HID-host mouse path (USBKeyboard.c::process_mouse_report)
 * and the BLE-HID-host mouse path (BTKeyboard.c) need to take the
 * already-decoded delta-X / delta-Y / wheel / buttons of a HID mouse
 * report and update the nunstruct[] / nunfoundc[] arrays that the
 * DEVICE(MOUSE n, "...") BASIC function reads.
 *
 * Decoding the report bytes is transport- and device-specific and
 * stays in the caller (USB does mouse-type dispatch on HID[n-1].mouse_type;
 * BT does HID-descriptor parsing in BTKeyboard.c). This function takes
 * over once x/y/wheel/buttons are known and is identical across
 * transports.
 *
 * The static leftpress/leftstate are intentionally shared across all
 * n values -- matches the pre-refactor behaviour of process_mouse_report.
 * In practice only one mouse is in use at a time.
 * ============================================================================
 */
void process_mouse_input(int16_t x_delta,
                         int16_t y_delta,
                         int8_t wheel_delta,
                         uint8_t buttons,
                         uint8_t n)
{
    static uint64_t leftpress = 0;
    static uint8_t leftstate = 0;

    uint64_t timenow = time_us_64();
    if (timenow - leftpress > 500000)
    {
        leftstate = 0;
        nunstruct[n].Z = 0;
    }
    if (leftstate == 0 && (buttons & MOUSE_BUTTON_LEFT))
    {
        leftpress = timenow;
        leftstate = 1;
    }
    if (leftstate == 1 && !(buttons & MOUSE_BUTTON_LEFT))
    {
        leftpress = timenow;
        leftstate = 2;
    }
    if (leftstate == 2 && (buttons & MOUSE_BUTTON_LEFT))
    {
        /* Second press within 500 ms -- double-click; DEVICE(MOUSE n, "D") will read this. */
        leftpress = timenow;
        leftstate = 3;
        nunstruct[n].Z = 1;
        nunfoundc[n] = 1;
    }

    nunstruct[n].L = buttons & MOUSE_BUTTON_LEFT   ? 1 : 0;
    nunstruct[n].R = buttons & MOUSE_BUTTON_RIGHT  ? 1 : 0;
    nunstruct[n].C = buttons & MOUSE_BUTTON_MIDDLE ? 1 : 0;

    /* Accumulate position deltas, clamped to the current display
       resolution. /2 matches the pre-refactor scale factor.
       Headless builds (PICOMITEBTH with no display configured) leave
       HRes/VRes at 0 -- fall back to a synthetic 1024-pixel range so
       DEVICE(MOUSE n, "X"/"Y") still yields useful values. */
    int x_max = (HRes > 0) ? HRes : 1024;
    int y_max = (VRes > 0) ? VRes : 1024;

    nunstruct[n].ax += x_delta / 2;
    if (nunstruct[n].ax >= x_max) nunstruct[n].ax = x_max - 1;
    if (nunstruct[n].ax < 0)      nunstruct[n].ax = 0;

    nunstruct[n].ay += y_delta / 2;
    if (nunstruct[n].ay >= y_max) nunstruct[n].ay = y_max - 1;
    if (nunstruct[n].ay < 0)      nunstruct[n].ay = 0;

    nunstruct[n].az += wheel_delta;

    /* Edge-detect button changes for the interrupt path. */
    if (nunstruct[n].x0 != (buttons & 0b111))
    {
        nunfoundc[n] = 1;
    }
    nunstruct[n].x0 = buttons & 0b111;

    /* Character-cell projection, used by the DEVICE(MOUSE) text-grid
       accessors. Falls back gracefully if no font is selected. */
    int cw = FontTable[gui_font >> 4][0] * (gui_font & 0b1111);
    int ch = FontTable[gui_font >> 4][1] * (gui_font & 0b1111);
    if (cw > 0) nunstruct[n].x1 = nunstruct[n].ax / cw;
    if (ch > 0) nunstruct[n].y1 = nunstruct[n].ay / ch;
}
