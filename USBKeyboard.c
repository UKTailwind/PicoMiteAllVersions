/***********************************************************************************************************************
PicoMite MMBasic

USBKeyboard.c

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

/*
This routine is based on a technique and code presented by Lucio Di Jasio in his excellent book
	"Programming 32-bit Microcontrollers in C - Exploring the PIC32".

	Thanks to Muller
	Fabrice(France),
	Alberto Leibovich(Argentina) and the other contributors who provided the code for
the non US keyboard layouts

****************************************************************************************************************************/
/**
 * @file USBKeyboard.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for the MMBasic gamepad and mouse commands
 */
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "tusb.h"
#include "hardware/structs/usb.h"
#ifdef USBKEYBOARD
#include "Audio.h"
#include "Connect.h"
#include "Remove.h"
#endif /* USBKEYBOARD */
/* Shared keymap + report decoder (KeyboardMap.c). The international
   keymap tables, APP_MapKeyToUsage, USR_KEYBRD_ProcessData,
   process_kbd_report and the caps/num/scroll lock + KeyDown[] state
   used to live in this file; they now live in KeyboardMap.c so the
   BLE-HID-host build can share them. */
#include "KeyboardMap.h"
extern volatile int ConsoleRxBufHead;
extern volatile int ConsoleRxBufTail;
int justset = 0;
/* USBcode (last-pressed-key scancode, read by the MM.INFO(USB) BASIC
   function) and repeattime (used by APP_MapKeyToUsage's auto-repeat
   logic) now live in KeyboardMap.c so the BLE-HID-host build sees
   them too — process_kbd_report writes both. */
// extern char ConsoleRxBuf[];
/* USB-host-only state: count of attached USB devices. Tracked by the
   tuh_mount_cb / tuh_umount_cb callbacks below; checked when accepting
   new mounts to enforce the 4-device limit. Definition lives here
   because Current_USB_devices is referenced only from USBKeyboard.c. */
uint8_t Current_USB_devices = 0;
static void process_mouse_report(hid_mouse_report_t const *report, uint8_t n);
// key codes that must be tracked for up/down state
#define CTRL 0x14 // left and right generate the same code
#define L_SHFT 0x12
#define R_SHFT 0x59
#define CAPS 0x58
#define NUML 0x77
// static uint8_t ds4_dev_addr = 0;
// static uint8_t ds4_instance = 0;
extern const char *KBrdList[];

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+
/*
BIT 0 Button R/R1
BIT 1 Button start/options
BIT 2 Button home
BIT 3 Button select/share
BIT 4 Button L/L1
BIT 5 Button down cursor
BIT 6 Button right cursor
BIT 7 Button up cursor
BIT 8 Button left cursor
BIT 9 Right shoulder button 2/R2
BIT 10 Button x/triangle
BIT 11 Button a/circle
BIT 12 Button y/square
BIT 13 Button b/cross
BIT 14 Left should button 2/L2
BIT 15 Touchpad
*/
struct s_Buttons
{
	uint8_t index; // which report element relates to this bit set to 0xFF if bit not used
				   // code can be a bit number 0-7 for positive if pressed
				   // 128-135 for negative if pressed
				   // 64 for value less than 64 if pressed
				   // 192 for value greater than 192 if pressed
	uint8_t code;
};
struct s_Gamepad
{
	uint16_t vid;
	uint16_t pid;
	struct s_Buttons b_R;
	struct s_Buttons b_START;
	struct s_Buttons b_HOME;
	struct s_Buttons b_SELECT;
	struct s_Buttons b_L;
	struct s_Buttons b_DOWN;
	struct s_Buttons b_RIGHT;
	struct s_Buttons b_UP;
	struct s_Buttons b_LEFT;
	struct s_Buttons b_R2;
	struct s_Buttons b_X;
	struct s_Buttons b_A;
	struct s_Buttons b_Y;
	struct s_Buttons b_B;
	struct s_Buttons b_L2;
	struct s_Buttons b_TOUCH;
};

const struct s_Gamepad Gamepads[] = {

	{.vid = 0x0810, .pid = 0xE501, .b_R = {6, 1}, .b_START = {6, 5}, .b_HOME = {0xFF, 0}, .b_SELECT = {6, 4}, .b_L = {6, 0}, .b_DOWN = {4, 192}, .b_RIGHT = {3, 192}, .b_UP = {4, 64}, .b_LEFT = {3, 64}, .b_R2 = {0xFF, 0}, .b_X = {5, 4}, .b_A = {5, 5}, .b_Y = {5, 7}, .b_B = {5, 6}, .b_L2 = {0xFF, 0}, .b_TOUCH = {0xFF, 0}},
	{.vid = 0x79, .pid = 0x11, .b_R = {6, 1}, .b_START = {6, 5}, .b_HOME = {255, 0}, .b_SELECT = {6, 4}, .b_L = {6, 0}, .b_DOWN = {4, 192}, .b_RIGHT = {3, 192}, .b_UP = {4, 64}, .b_LEFT = {3, 64}, .b_R2 = {255, 0}, .b_X = {5, 4}, .b_A = {5, 5}, .b_Y = {5, 7}, .b_B = {5, 6}, .b_L2 = {255, 0}, .b_TOUCH = {255, 0}},
	{.vid = 0x081F, .pid = 0xE401, .b_R = {6, 1}, .b_START = {6, 5}, .b_HOME = {0xFF, 0}, .b_SELECT = {6, 4}, .b_L = {6, 0}, .b_DOWN = {1, 192}, .b_RIGHT = {0, 192}, .b_UP = {1, 64}, .b_LEFT = {0, 64}, .b_R2 = {0xFF, 0}, .b_X = {5, 4}, .b_A = {5, 5}, .b_Y = {5, 7}, .b_B = {5, 6}, .b_L2 = {0xFF, 0}, .b_TOUCH = {0xFF, 0}},
	{.vid = 0x1C59, .pid = 0x26, .b_R = {6, 1}, .b_START = {6, 3}, .b_HOME = {255, 0}, .b_SELECT = {6, 2}, .b_L = {6, 0}, .b_DOWN = {1, 192}, .b_RIGHT = {0, 192}, .b_UP = {1, 64}, .b_LEFT = {0, 64}, .b_R2 = {255, 0}, .b_X = {5, 7}, .b_A = {5, 6}, .b_Y = {5, 4}, .b_B = {5, 5}, .b_L2 = {255, 0}, .b_TOUCH = {255, 0}},
	{.vid = 0x6A3, .pid = 0x107, .b_R = {3, 7}, .b_START = {3, 5}, .b_HOME = {255, 0}, .b_SELECT = {3, 4}, .b_L = {3, 6}, .b_DOWN = {1, 192}, .b_RIGHT = {0, 192}, .b_UP = {1, 64}, .b_LEFT = {0, 64}, .b_R2 = {3, 7}, .b_X = {3, 1}, .b_A = {3, 3}, .b_Y = {3, 0}, .b_B = {3, 2}, .b_L2 = {255, 0}, .b_TOUCH = {255, 0}},
	{.vid = 0x11FF, .pid = 0x3331, .b_R = {6, 3}, .b_START = {6, 1}, .b_HOME = {6, 6}, .b_SELECT = {6, 0}, .b_L = {6, 2}, .b_DOWN = {1, 192}, .b_RIGHT = {0, 192}, .b_UP = {1, 64}, .b_LEFT = {0, 64}, .b_R2 = {6, 5}, .b_X = {5, 4}, .b_A = {5, 6}, .b_Y = {5, 5}, .b_B = {5, 7}, .b_L2 = {6, 4}, .b_TOUCH = {6, 7}},
	{.vid = 0x0583, .pid = 0x2060, .b_R = {0x02, 0x05}, .b_START = {0x02, 0x07}, .b_HOME = {0xFF, 0x00}, .b_SELECT = {0x02, 0x06}, .b_L = {0x02, 0x04}, .b_DOWN = {0x01, 0xC0}, .b_RIGHT = {0x00, 0xC0}, .b_UP = {0x01, 0x40}, .b_LEFT = {0x00, 0x40}, .b_R2 = {0x03, 0x05}, .b_X = {0x02, 0x02}, .b_A = {0x02, 0x00}, .b_Y = {0x02, 0x03}, .b_B = {0x02, 0x01}, .b_L2 = {0x03, 0x04}, .b_TOUCH = {0xFF, 0x00}},
	{0, 0, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
};

struct s_Gamepad MyGamepad = {0};
static bool monitor = false, nooutput = false;
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
#define USE_ANSI_ESCAPE 0

#define MAX_REPORT 4
volatile struct s_HID HID[4] = {0};

/* USB multi-touch -> generic "touch panel" integration.
   usb_touch_present is true between mount and umount of a touch device;
   usb_touch_active goes true on every report whose contact 0 has tip==1.
   usb_touch_x / usb_touch_y always hold contact 0's screen-scaled
   coordinates. The timer-callback edge detector (PicoMite.c) reads these
   alongside TOUCH_DOWN so the GUI control machinery doesn't care whether
   the input came from a resistive/capacitive panel or a USB touchscreen.
   GetTouch() in GUI.c returns them when the resistive panel is absent.

   usb_touch_last_us tracks the wall-clock of the most recent report
   that set usb_touch_active = true. The 1 ms timer callback in
   PicoMite.c uses it as a no-report watchdog: some touch controllers
   (notably the one used in the Pimoroni Pico Plus 2W test rig) fail
   to send a release report (count=0 / tip=0) under specific timing
   patterns, leaving usb_touch_active stuck true. Without the
   watchdog ProcessTouch's static `repeat` for spinners then never
   clears and the GUI machinery thinks the touch is held forever. */
volatile bool usb_touch_present = false;
volatile bool usb_touch_active = false;
volatile int16_t usb_touch_x = 0;
volatile int16_t usb_touch_y = 0;
volatile uint64_t usb_touch_last_us = 0;

/* Contact 1 — exposed via TOUCH(X2) / TOUCH(Y2), mirroring the
   Option.TOUCH_CAP capacitive multi-touch API. usb_touch_active2 is
   true only when contact 1 is present AND has tip=1. */
volatile bool usb_touch_active2 = false;
volatile int16_t usb_touch_x2 = 0;
volatile int16_t usb_touch_y2 = 0;

/* All live contacts (tip=1), compacted to the front, exposed via
   TOUCH(XN n) / TOUCH(YN n) for n = 1..usb_touch_count. Contacts 0 and 1
   duplicate usb_touch_x/y and usb_touch_x2/y2 so the legacy accessors are
   unaffected. Refreshed wholesale on every decoded report by touch_publish. */
volatile uint8_t usb_touch_count = 0;
volatile int16_t usb_touch_xn[MAX_TOUCH_CONTACTS] = {0};
volatile int16_t usb_touch_yn[MAX_TOUCH_CONTACTS] = {0};
typedef struct TU_ATTR_PACKED
{
	uint8_t x, y, z, rz; // joystick

	struct
	{
		uint8_t dpad : 4;	  // (hat format, 0x08 is released, 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW)
		uint8_t square : 1;	  // west
		uint8_t cross : 1;	  // south
		uint8_t circle : 1;	  // east
		uint8_t triangle : 1; // north
	};

	struct
	{
		uint8_t l1 : 1;
		uint8_t r1 : 1;
		uint8_t l2 : 1;
		uint8_t r2 : 1;
		uint8_t share : 1;
		uint8_t option : 1;
		uint8_t l3 : 1;
		uint8_t r3 : 1;
	};

	struct
	{
		uint8_t ps : 1;		 // playstation button
		uint8_t tpad : 1;	 // track pad click
		uint8_t counter : 6; // +1 each report
	};

	uint8_t l2_trigger; // 0 released, 0xff fully pressed
	uint8_t r2_trigger; // as above

	uint16_t timestamp;
	uint8_t battery;
	//
	int16_t gyro[3];  // x, y, z;
	int16_t accel[3]; // x, y, z

	// there is still lots more info

} sony_ds4_report_t;

typedef struct TU_ATTR_PACKED
{
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
#define PS4 128
#define PS3 129
#define SNES 130
#define XBOX 131
#define UNKNOWN 132
static inline bool is_xbox(uint8_t dev_addr)
{
	uint16_t vid, pid;
	tuh_vid_pid_get(dev_addr, &vid, &pid);
	return ((vid == 0x11c0 && pid == 0x5500)	// EasySMX Wireless, u, Android mode (u)
			|| (vid == 0x11c1 && pid == 0x9101) // EasySMX Wireless, c, PC Mode, D-input, emulation
												//           || (vid == 0x057e && pid == 0x2009)
			|| (vid == 0x2F24 && pid == 0x0048));
}
/*static inline bool is_specific(uint8_t dev_addr)
{
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  return ( (vid == 0x810 && pid == 0xE501)    // EasySMX Wireless, u, Android mode (u)
		 );
}*/
static inline bool is_generic(uint8_t dev_addr)
{
	if (monitor)
		return true;
	uint16_t vid, pid, i = 0;
	tuh_vid_pid_get(dev_addr, &vid, &pid);
	if (MyGamepad.pid == pid && MyGamepad.vid == vid)
		return true; // user specified decode
	while (Gamepads[i].pid)
	{
		if (Gamepads[i].pid == pid && Gamepads[i].vid == vid)
			return true;
		i++;
	}
	return false;
}
void process_xbox(uint8_t const *report, uint16_t len, uint8_t n)
{
	// PInt(len);
	/*for (int i=0;i<len;i++) {
		PInt(i);
		PIntHC(report[i]);
	}
	PRet();*/
	nunstruct[n].type = XBOX;
	uint16_t b = 0;
	if (len == 9)
	{
		if (report[0] & 0x10)
			b |= 0x0400; // Button y/triangle
		if (report[0] & 0x02)
			b |= 0x0800; // Button b/circle
		if (report[0] & 0x08)
			b |= 0x1000; // Button x/square
		if (report[0] & 0x01)
			b |= 0x2000; // Button a/cross
		if (report[1] & 0x08)
			b |= 0x0002; // Button start -> start?
		if (report[1] & 0x04)
			b |= 0x0008; // Button home -> xbox/PS?
		if (report[1] & 0x10)
			b |= 0x0004; // Button select -> back/share?
		if (report[0] & 0x80)
			b |= 0x0001; // Button R/R1
		if (report[0] & 0x40)
			b |= 0x0010; // Button L/L1
		if (report[2] == 0x4)
			b |= 0x20; // Button down cursor
		if (report[2] == 0x2)
			b |= 0x40; // Button right cursor
		if (report[2] == 0x0)
			b |= 0x80; // Button up cursor
		if (report[2] == 0x6)
			b |= 0x100; // Button left cursor
		nunstruct[n].ax = report[3];
		nunstruct[n].ay = report[4];
		nunstruct[n].Z = report[5];
		nunstruct[n].C = report[6];
		nunstruct[n].L = report[8];
		nunstruct[n].R = report[7];
	}
	else
	{
		// TODO
	}
	if ((b ^ nunstruct[n].x0) & nunstruct[n].x1)
	{
		nunfoundc[n] = 1;
	}
	nunstruct[n].x0 = b;
}
// Each HID instance can has multiple reports
/*static struct
{
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
} hid_info[CFG_TUH_HID];*/
/**
 * Analyze HID report descriptor to determine mouse type
 *
 * @param desc_report Pointer to HID report descriptor
 * @param desc_len Length of descriptor in bytes
 * @param info Pointer to mouse_info_t structure to fill
 * @return mouse_report_type_t indicating the mouse type
 */
mouse_report_type_t analyze_mouse_descriptor(const uint8_t *desc_report, uint16_t desc_len, mouse_info_t *info)
{
	if (!desc_report || !info || desc_len == 0)
	{
		return MOUSE_TYPE_UNKNOWN;
	}

	memset(info, 0, sizeof(mouse_info_t));

	uint8_t report_size = 0;
	uint8_t report_count = 0;
	//	uint8_t current_usage = 0;
	bool found_x = false;
	bool found_y = false;
	uint8_t x_bits = 0;
	uint8_t y_bits = 0;
	uint8_t button_count = 0;
	uint8_t bit_position = 0;

	for (uint16_t i = 0; i < desc_len;)
	{
		uint8_t bSize = desc_report[i] & 0x03;
		uint8_t bType = (desc_report[i] >> 2) & 0x03;
		uint8_t bTag = (desc_report[i] >> 4) & 0x0F;

		i++;

		uint32_t data = 0;
		for (int j = 0; j < bSize; j++)
		{
			if (i + j < desc_len)
			{
				data |= (desc_report[i + j] << (j * 8));
			}
		}
		i += bSize;

		// Check for Report ID (Global item, tag 8)
		if (bType == 1 && bTag == 8)
		{ // Global Report ID
			info->uses_report_id = true;
			info->report_id = data;
		}

		if (bType == 1)
		{
			if (bTag == 7)
			{
				report_size = data;
			}
			else if (bTag == 9)
			{
				report_count = data;
			}
		}
		else if (bType == 2)
		{
			if (bTag == 0)
			{
				//				current_usage = data;

				if (data == 0x30)
				{
					found_x = true;
				}
				else if (data == 0x31)
				{
					found_y = true;
				}
				else if (data == 0x38)
				{
					info->has_wheel = true;
					info->wheel_byte_offset = bit_position / 8;
				}
				else if (data == 0x3C)
				{
					info->has_pan = true;
				}
			}
			else if (bTag == 2)
			{
				if (data >= 0x01 && data <= 0x20)
				{
					button_count = data;
				}
			}
		}
		else if (bType == 0)
		{
			if (bTag == 8)
			{
				if (found_x && !x_bits)
				{
					x_bits = report_size;
					found_x = false;
				}
				if (found_y && !y_bits)
				{
					y_bits = report_size;
					found_y = false;
				}

				bit_position += (report_size * report_count);
			}
		}
	}

	info->x_bits = x_bits;
	info->y_bits = y_bits;
	info->button_count = button_count;

	// If report ID is used, add 1 byte to the report length
	info->report_length = (bit_position + 7) / 8;
	if (info->uses_report_id)
	{
		info->report_length += 1;
	}

	if (x_bits == 8 && y_bits == 8)
	{
		info->type = MOUSE_TYPE_STANDARD_8BIT;
		return MOUSE_TYPE_STANDARD_8BIT;
	}
	else if (x_bits == 12 && y_bits == 12)
	{
		info->type = MOUSE_TYPE_HIGHRES_12BIT;
		return MOUSE_TYPE_HIGHRES_12BIT;
	}
	else if (x_bits == 16 && y_bits == 16)
	{
		info->type = MOUSE_TYPE_GAMING_16BIT;
		return MOUSE_TYPE_GAMING_16BIT;
	}

	info->type = MOUSE_TYPE_UNKNOWN;
	return MOUSE_TYPE_UNKNOWN;
}
/**
 * Print mouse info for debugging
 */
void print_mouse_info(const mouse_info_t *info)
{
	const char *type_names[] = {
		"Unknown",
		"Standard 8-bit",
		"High-res 12-bit",
		"Gaming 16-bit"};

	if (!CurrentLinePtr)
	{
		MMPrintString("Mouse Type: ");
		MMPrintString((char *)type_names[info->type]);
		PRet();

		MMPrintString("  X/Y bits: ");
		PInt(info->x_bits);
		MMPrintString("/");
		PInt(info->y_bits);
		PRet();

		MMPrintString("  Buttons: ");
		PInt(info->button_count);
		PRet();

		MMPrintString("  Report length: ");
		PInt(info->report_length);
		MMPrintString(" bytes");
		PRet();

		MMPrintString("  Has wheel: ");
		MMPrintString(info->has_wheel ? "Yes" : "No");
		if (info->has_wheel)
		{
			MMPrintString(" (byte ");
			PInt(info->wheel_byte_offset);
			MMPrintString(")");
		}
		PRet();

		MMPrintString("  Has pan: ");
		MMPrintString(info->has_pan ? "Yes" : "No");
		PRet();
	}
}
/**
 * Simple wrapper that just returns the mouse type
 */
mouse_report_type_t get_mouse_type(const uint8_t *desc_report, uint16_t desc_len)
{
	mouse_info_t info;
	return analyze_mouse_descriptor(desc_report, desc_len, &info);
}

/* ============================================================================
 * USB multi-touch HID — descriptor parser + report analyser
 * ============================================================================
 * Targets the Windows-Precision-Touchscreen-style descriptor layout that
 * essentially every multi-touch USB controller ships with: a Touch
 * Screen application collection (Digitizer page 0x0D / usage 0x04)
 * containing N Finger logical collections (usage 0x22), each declaring
 * Tip Switch (0x42), In Range (0x32), Contact Identifier (0x51), X
 * (Generic Desktop 0x30), Y (0x31). A top-level Contact Count (0x54)
 * carries the per-report live-contact count.
 *
 * The parser walks the descriptor once at enumeration to capture
 * field offsets; the analyser then extracts contact data from each
 * input report by bit-offset rather than re-parsing.
 */

/* Bit-aligned read from a byte buffer. bit_count up to 32. */
static uint32_t touch_read_bits(const uint8_t *bytes, uint16_t bit_offset, uint8_t bit_count)
{
	uint32_t result = 0;
	for (uint8_t i = 0; i < bit_count; i++)
	{
		uint16_t b = bit_offset + i;
		if (bytes[b >> 3] & (1u << (b & 7)))
		{
			result |= (1u << i);
		}
	}
	return result;
}

/**
 * Walk a HID report descriptor looking for the Windows Touch layout.
 * Returns true and fills *info if the descriptor describes a touch
 * screen with at least one Finger collection; otherwise returns false
 * and leaves *info zeroed.
 */
bool analyze_touch_descriptor(const uint8_t *desc_report, uint16_t desc_len, touch_info_t *info)
{
	if (!desc_report || !info || desc_len == 0)
		return false;

	memset(info, 0, sizeof(*info));

	/* Parser state. */
	uint8_t usage_page = 0;
	uint16_t last_usage = 0;   /* most recent Local Usage item */
	uint8_t report_size = 0;   /* Global Report Size */
	uint8_t report_count = 0;  /* Global Report Count */
	int32_t logical_max = 0;   /* Global Logical Maximum (last value seen) */
	uint16_t bit_position = 0; /* bit offset within current Report ID's report */
	int collection_depth = 0;

	/* Report-ID tracking. A typical Precision Touchscreen descriptor
	   declares multiple Report IDs (ID 1 for touch Input, ID 4 for
	   Feature mode-select, etc.). Each Report ID has its own report
	   transmitted independently with the ID byte prepended. We need
	   to track which ID contains the Finger collections and only
	   accumulate bit positions / capture field offsets while parsing
	   THAT report's items. */
	int current_report_id = 0; /* 0 = no Report ID encountered yet */
	int finger_report_id = -1; /* ID we want to capture, locked at first Finger */

	/* Finger-collection tracking. */
	bool in_finger = false;
	int finger_start_depth = -1;
	uint16_t finger_start_bit = 0;
	uint8_t finger_count = 0;
	bool saw_touchscreen_root = false;

	/* Pointer (Generic Desktop Mouse/Pointer) fallback-collection tracking.
	   ptr_uq is a small usage queue so an X/Y pair declared as one Input
	   with a usage array (09 30 09 31 ... Report Count 2) is distributed
	   across the two 16-bit fields — the common mouse idiom. Reset on every
	   Main item; only populated while inside the pointer collection. */
	bool in_pointer = false;
	int pointer_start_depth = -1;
	int pointer_report_id = -1;
	bool got_pointer_button = false;
	uint16_t ptr_uq[8];
	uint8_t ptr_uq_n = 0;

	/* Device Configuration collection (usage 0x0E) — hosts the Input Mode
	   (Device Mode 0x52) Feature used to switch the panel to multi-touch. */
	bool in_device_config = false;
	int device_config_depth = -1;

	for (uint16_t i = 0; i < desc_len;)
	{
		uint8_t prefix = desc_report[i++];
		uint8_t bSize = prefix & 0x03;
		uint8_t bType = (prefix >> 2) & 0x03;
		uint8_t bTag = (prefix >> 4) & 0x0F;

		uint32_t data = 0;
		for (uint8_t j = 0; j < bSize && (i + j) < desc_len; j++)
			data |= ((uint32_t)desc_report[i + j]) << (j * 8);
		i += bSize;

		if (bType == 1) /* Global */
		{
			switch (bTag)
			{
			case 0:
				usage_page = data & 0xFF;
				break;
			case 2:
				logical_max = (int32_t)data;
				break; /* Logical Maximum */
			case 7:
				report_size = data & 0xFF;
				break; /* Report Size */
			case 8:	   /* Report ID */
				info->uses_report_id = true;
				current_report_id = data & 0xFF;
				/* New Report ID starts a fresh report — reset the
				   bit-position counter. Each Report ID's report is
				   transmitted independently. */
				bit_position = 0;
				break;
			case 9:
				report_count = data & 0xFF;
				break; /* Report Count */
			}
		}
		else if (bType == 2) /* Local */
		{
			if (bTag == 0)
			{
				last_usage = data & 0xFFFF; /* Usage */
				if (in_pointer && ptr_uq_n < (uint8_t)(sizeof(ptr_uq) / sizeof(ptr_uq[0])))
					ptr_uq[ptr_uq_n++] = last_usage;
			}
		}
		else if (bType == 0) /* Main */
		{
			if (bTag == 10) /* Collection */
			{
				collection_depth++;
				/* The application collection tells us this is a touch
				   screen. Required for valid=true at the end. */
				if (collection_depth == 1 && usage_page == 0x0D && last_usage == 0x04)
					saw_touchscreen_root = true;
				/* Generic Desktop Mouse(0x02)/Pointer(0x01) application
				   collection — the single-finger pointer fallback some
				   digitizers stream instead of the multitouch report.
				   Capture its layout in parallel; the report ID is declared
				   just inside it. */
				if (collection_depth == 1 && usage_page == 0x01 &&
					(last_usage == 0x01 || last_usage == 0x02) && !in_pointer)
				{
					in_pointer = true;
					pointer_start_depth = collection_depth;
				}
				/* Device Configuration collection — carries the Input Mode
				   (Device Mode) Feature whose Report ID we need to write. */
				if (usage_page == 0x0D && last_usage == 0x0E && !in_device_config)
				{
					in_device_config = true;
					device_config_depth = collection_depth;
				}
				/* Finger collection — capture start bit, then descend
				   so per-contact fields populate relative offsets.
				   Lock finger_report_id to the Report ID that hosts
				   the first Finger we see; ignore Fingers in other
				   Report IDs (e.g. Feature-report duplicates). */
				if (usage_page == 0x0D && last_usage == 0x22 && !in_finger)
				{
					if (finger_report_id == -1)
					{
						finger_report_id = current_report_id;
						info->report_id = (uint8_t)current_report_id;
					}
					if (current_report_id == finger_report_id)
					{
						in_finger = true;
						finger_start_depth = collection_depth;
						finger_start_bit = bit_position;
					}
				}
			}
			else if (bTag == 12) /* End Collection */
			{
				if (in_finger && collection_depth == finger_start_depth)
				{
					if (finger_count == 0)
					{
						info->contact_stride_bits =
							bit_position - finger_start_bit;
						info->first_contact_bit_offset = finger_start_bit;
					}
					finger_count++;
					in_finger = false;
				}
				if (in_pointer && collection_depth == pointer_start_depth)
					in_pointer = false;
				if (in_device_config && collection_depth == device_config_depth)
					in_device_config = false;
				collection_depth--;
			}
			else if (bTag == 8) /* Input */
			{
				uint16_t field_bits = (uint16_t)report_size * report_count;

				/* Only capture field offsets when we're in the Report
				   ID that owns the Finger collections. Before any
				   Finger has been seen we capture everything (the
				   first Finger we encounter will lock the ID). */
				bool capture = (finger_report_id == -1) || (current_report_id == finger_report_id);

				if (capture && in_finger && finger_count == 0)
				{
					/* Capture field offsets relative to start of contact
					   block. Only record the first finger's layout — the
					   stride takes care of the rest. */
					uint16_t rel = bit_position - finger_start_bit;
					if (usage_page == 0x0D)
					{
						if (last_usage == 0x42)
							info->tip_switch_bit_offset = rel;
						else if (last_usage == 0x32)
							info->in_range_bit_offset = rel;
						else if (last_usage == 0x51)
						{
							info->contact_id_bit_offset = rel;
							info->contact_id_bits = report_size;
						}
					}
					else if (usage_page == 0x01) /* Generic Desktop */
					{
						if (last_usage == 0x30)
						{
							info->x_bit_offset = rel;
							info->x_bits = report_size;
							info->x_logical_max = logical_max;
						}
						else if (last_usage == 0x31)
						{
							info->y_bit_offset = rel;
							info->y_bits = report_size;
							info->y_logical_max = logical_max;
						}
					}
				}
				else if (capture && !in_finger)
				{
					/* Top-level (within touch report): Contact Count
					   is the only field we need; Scan Time, button
					   indicators etc. are ignored. */
					if (usage_page == 0x0D && last_usage == 0x54)
					{
						info->contact_count_bit_offset = bit_position;
						info->contact_count_bits = report_size;
					}
				}

				/* Pointer fallback capture (runs independently of the
				   finger 'capture' flag — the Mouse collection has its own
				   report ID, so bit_position has already been reset for it).
				   Button 1 is the LSB of the first Button field; X/Y are the
				   ABSOLUTE Generic Desktop axes (skip relative deltas — a
				   relative mouse has no screen mapping). X and Y may share
				   one Input via a usage array, so walk the per-field usages
				   from the queue and place each by its own bit offset. */
				if (in_pointer)
				{
					if (pointer_report_id == -1)
						pointer_report_id = current_report_id;
					if (usage_page == 0x09 && !got_pointer_button)
					{
						info->pointer_button_bit_offset = bit_position;
						got_pointer_button = true;
					}
					else if (usage_page == 0x01 && !(data & 0x04)) /* Absolute */
					{
						for (uint8_t f = 0; f < report_count; f++)
						{
							uint16_t u = (f < ptr_uq_n) ? ptr_uq[f]
														: (ptr_uq_n ? ptr_uq[ptr_uq_n - 1] : last_usage);
							uint16_t off = bit_position + (uint16_t)f * report_size;
							if (u == 0x30)
							{
								info->pointer_x_bit_offset = off;
								info->pointer_x_bits = report_size;
								info->pointer_x_logical_max = logical_max;
							}
							else if (u == 0x31)
							{
								info->pointer_y_bit_offset = off;
								info->pointer_y_bits = report_size;
								info->pointer_y_logical_max = logical_max;
							}
						}
					}
				}

				bit_position += field_bits;
				/* Local usage items are 'consumed' by the next Main item. */
				last_usage = 0;
			}
			else if (bTag == 9) /* Output / Feature */
			{
				/* Still need to consume bit_position for Feature/Output
				   reports that share the same Report ID — but only
				   Input bits matter for the live report layout. */
				last_usage = 0;
			}
			else if (bTag == 11) /* Feature */
			{
				/* The Feature inside the Device Configuration collection is
				   the Input Mode (Device Mode) selector. Capture its Report
				   ID so we can write it to enable multi-touch. */
				if (in_device_config && !info->has_input_mode)
				{
					info->has_input_mode = true;
					info->input_mode_report_id = (uint8_t)current_report_id;
				}
				/* Windows bring-up Feature reports, captured by usage so the
				   GET_FEATURE handshake isn't tied to one panel's report IDs:
				   Contact Count Maximum (0x55, digitizer page) and the
				   Microsoft certification blob (0xC5, distinctive enough to
				   match on usage alone since the vendor page is truncated). */
				if (usage_page == 0x0D && last_usage == 0x55 && !info->contact_count_max_report_id)
					info->contact_count_max_report_id = (uint8_t)current_report_id;
				if (last_usage == 0xC5 && !info->cert_blob_report_id)
					info->cert_blob_report_id = (uint8_t)current_report_id;
				last_usage = 0;
			}
			/* Every Main item consumes the pending Local usages. */
			ptr_uq_n = 0;
		}
	}

	/* Total Input report length for the Finger-bearing Report ID,
	   rounded up to a byte, plus the report-ID byte if present.
	   bit_position holds whatever the last Report ID's accumulated
	   length was — if the descriptor ended on a non-finger report, we
	   may have a stale value. Recompute from first_contact_bit_offset
	   + max_contacts * stride + post-contacts fields (just the
	   contact-count bit) as a safer estimate. */
	if (finger_count > 0)
	{
		uint16_t after_contacts = info->first_contact_bit_offset + finger_count * info->contact_stride_bits;
		uint16_t after_cc = info->contact_count_bit_offset + info->contact_count_bits;
		uint16_t end_bits = (after_contacts > after_cc) ? after_contacts : after_cc;
		info->report_length_bytes = (end_bits + 7) / 8;
		if (info->uses_report_id)
			info->report_length_bytes += 1;
	}
	info->max_contacts = finger_count;

	/* Finalise the pointer fallback. Require its own report ID (distinct
	   from the multitouch one) plus absolute X/Y — a relative mouse has no
	   logical max and can't be scaled to the screen, so skip those. */
	if (pointer_report_id >= 0 && pointer_report_id != finger_report_id &&
		got_pointer_button && info->pointer_x_bits > 0 && info->pointer_y_bits > 0 &&
		info->pointer_x_logical_max > 0 && info->pointer_y_logical_max > 0)
	{
		info->has_pointer_fallback = true;
		info->pointer_report_id = (uint8_t)pointer_report_id;
		uint16_t bx = info->pointer_button_bit_offset + 1;
		uint16_t ex = info->pointer_x_bit_offset + info->pointer_x_bits;
		uint16_t ey = info->pointer_y_bit_offset + info->pointer_y_bits;
		uint16_t end_bits = bx;
		if (ex > end_bits)
			end_bits = ex;
		if (ey > end_bits)
			end_bits = ey;
		info->pointer_report_length_bytes = (end_bits + 7) / 8;
		if (info->uses_report_id)
			info->pointer_report_length_bytes += 1;
	}

	info->valid = (saw_touchscreen_root && finger_count > 0 && info->x_bits > 0 && info->y_bits > 0);
	return info->valid;
}

/* Scale a raw device-logical axis value to current display coords, with
   the same clip-to-range / fall-back-when-uninitialised rules used for the
   multitouch contacts. res is HRes or VRes. */
static int16_t touch_scale_axis(uint32_t raw, int32_t logical_max, int res)
{
	if (logical_max > 0 && res > 0)
	{
		if (raw > (uint32_t)logical_max)
			raw = (uint32_t)logical_max;
		int32_t v = (int32_t)((raw * (uint32_t)res) / (uint32_t)logical_max);
		if (v >= res)
			v = res - 1;
		return (int16_t)v;
	}
	return (int16_t)raw;
}

/* Publish a decoded report to the generic touch-panel state watched by the
   PicoMite.c edge detector (TouchDown/TouchUp) and GetTouch() in GUI.c, and
   run the pinch / long-press gesture machine. Shared by the multitouch and
   pointer-fallback decode paths. Uses out->count + out->contacts[]. */
static void touch_publish(touch_report_t *out)
{
	/* A contact reported with tip=0 (hover) does NOT count as "pressed".
	   Stamp the timestamp on EVERY report (including count=0 releases) so
	   the watchdog only fires when reports actually stop arriving. */
	usb_touch_last_us = time_us_64();
	if (out->count > 0 && out->contacts[0].tip)
	{
		usb_touch_x = out->contacts[0].x;
		usb_touch_y = out->contacts[0].y;
		usb_touch_active = true;
	}
	else
	{
		usb_touch_active = false;
	}
	/* Contact 1 mirrors the Option.TOUCH_CAP X2/Y2 API. */
	if (out->count >= 2 && out->contacts[1].tip)
	{
		usb_touch_x2 = out->contacts[1].x;
		usb_touch_y2 = out->contacts[1].y;
		usb_touch_active2 = true;
	}
	else
	{
		usb_touch_active2 = false;
	}

	/* Publish every live contact for TOUCH(XN n)/TOUCH(YN n). out->count is
	   the tip=1 contacts compacted to the front, so xn[0..count-1] are valid.
	   Set the count last so a BASIC reader never sees coords from a contact
	   that the count says is absent. */
	{
		uint8_t nc = out->count;
		if (nc > MAX_TOUCH_CONTACTS)
			nc = MAX_TOUCH_CONTACTS;
		for (uint8_t k = 0; k < nc; k++)
		{
			usb_touch_xn[k] = out->contacts[k].x;
			usb_touch_yn[k] = out->contacts[k].y;
		}
		usb_touch_count = nc;
	}

#ifdef GUICONTROLS
	/* Two-finger pinch + long-press gesture analysis. The gesture state
	   machine lives in Draw.c under #ifdef GUICONTROLS, so builds without
	   GUI controls (e.g. VGAUSB) don't link these symbols — skip the
	   calls. Raw usb_touch_x/y is still available to BASIC. Pinch is "live"
	   while BOTH contacts have tip=1; usb_touch_x/y still hold the last
	   position even after active goes false, so the end-position read is
	   valid right at the transition. */
	{
		static bool pinch_was_both = false;
		bool now_both = usb_touch_active && usb_touch_active2;
		if (now_both && !pinch_was_both)
		{
			touch_gesture_pinch_start(usb_touch_x, usb_touch_y,
									  usb_touch_x2, usb_touch_y2);
		}
		else if (!now_both && pinch_was_both)
		{
			touch_gesture_pinch_end(usb_touch_x, usb_touch_y,
									usb_touch_x2, usb_touch_y2);
		}
		pinch_was_both = now_both;
	}
	/* Long-press detection during the hold. Runs on every report so a
	   long-press fires the moment the held threshold is crossed, without
	   waiting for the lift. Skipped when contact 1 is also active. */
	touch_gesture_tick(usb_touch_x, usb_touch_y,
					   usb_touch_active && !usb_touch_active2);
#endif /* GUICONTROLS */
}

/* Decode a single-contact pointer-fallback report (dual-mode digitizer in
   mouse mode). `bytes` is the payload with the report-ID byte already
   stripped. Button 1 = tip; absolute X/Y scale like a contact. */
static bool process_pointer_report(const uint8_t *bytes, uint16_t payload_len,
								   const touch_info_t *info, touch_report_t *out)
{
	uint32_t need = (uint32_t)info->pointer_button_bit_offset + 1;
	uint32_t nx = (uint32_t)info->pointer_x_bit_offset + info->pointer_x_bits;
	uint32_t ny = (uint32_t)info->pointer_y_bit_offset + info->pointer_y_bits;
	if (nx > need)
		need = nx;
	if (ny > need)
		need = ny;
	if ((uint32_t)payload_len * 8u < need)
	{
		out->count = 0;
		return false;
	}

	bool tip = touch_read_bits(bytes, info->pointer_button_bit_offset, 1) != 0;
	touch_contact_t *tc = &out->contacts[0];
	tc->tip = tip;
	tc->in_range = tip;
	tc->id = 0;
	uint32_t raw_x = touch_read_bits(bytes, info->pointer_x_bit_offset, info->pointer_x_bits);
	uint32_t raw_y = touch_read_bits(bytes, info->pointer_y_bit_offset, info->pointer_y_bits);
	tc->x = touch_scale_axis(raw_x, info->pointer_x_logical_max, HRes);
	tc->y = touch_scale_axis(raw_y, info->pointer_y_logical_max, VRes);
	out->count = tip ? 1 : 0;

	touch_publish(out);
	return true;
}

/**
 * Decode an Input report into a touch_report_t, using offsets captured
 * by analyze_touch_descriptor(). `report` is the raw bytes as TinyUSB
 * delivers them (which include the report-ID byte iff the device
 * uses report IDs). Returns true if the report was decoded.
 */
bool process_touch_report(const uint8_t *report, uint16_t len,
						  const touch_info_t *info, touch_report_t *out)
{
	if (!report || !info || !out || !info->valid)
		return false;

	const uint8_t *bytes = report;
	uint16_t payload_len = len;

	/* If the device uses report IDs, the first byte is the report ID.
	   A dual-mode digitizer streams its Mouse-collection report ID for a
	   single finger — route that to the pointer-fallback decoder. Reports
	   for any other report ID (config / feature mirrors) are ignored. */
	if (info->uses_report_id)
	{
		if (payload_len < 1)
		{
			out->count = 0;
			return false;
		}
		if (info->has_pointer_fallback && bytes[0] == info->pointer_report_id)
			return process_pointer_report(bytes + 1, payload_len - 1, info, out);
		if (bytes[0] != info->report_id)
		{
			out->count = 0;
			return false;
		}
		bytes++;
		payload_len--;
	}

	/* Reject a report too short to contain the contact-count field. A
	   device whose report exceeds CFG_TUH_HID_EPIN_BUFSIZE arrives
	   fragmented across callbacks; a leading fragment that omits the
	   contact-count byte would otherwise decode as count=0 and silently
	   swallow the touch. Better to drop the fragment than misread it. */
	if ((uint32_t)payload_len * 8u <
		(uint32_t)info->contact_count_bit_offset + info->contact_count_bits)
	{
		out->count = 0;
		return false;
	}

	/* The Contact Count field tells us how many contact slots the report
	   carries — but on many panels it counts contacts still being TRACKED,
	   including fingers that have lifted (reported with tip=0) and aren't
	   released yet. So it doesn't fall as fingers lift. We therefore use it
	   only to bound the slot scan (so a fixed footer past the live slots
	   isn't misread as a contact) and report out->count as the number of
	   slots actually TOUCHING (tip=1), compacted to the front so contact 0/1
	   are the live fingers regardless of which hardware slot they occupy. */
	uint8_t scan = touch_read_bits(bytes,
								   info->contact_count_bit_offset,
								   info->contact_count_bits);
	if (scan > info->max_contacts)
		scan = info->max_contacts;
	if (scan > MAX_TOUCH_CONTACTS)
		scan = MAX_TOUCH_CONTACTS;

	uint8_t live = 0;
	for (uint8_t c = 0; c < scan; c++)
	{
		uint16_t base = info->first_contact_bit_offset + (uint16_t)c * info->contact_stride_bits;
		if (touch_read_bits(bytes, base + info->tip_switch_bit_offset, 1) == 0)
			continue; /* lifted / hovering contact — not a live touch */
		touch_contact_t *tc = &out->contacts[live++];
		tc->tip = true;
		tc->in_range = touch_read_bits(bytes, base + info->in_range_bit_offset, 1) != 0;
		tc->id = (uint8_t)touch_read_bits(bytes, base + info->contact_id_bit_offset,
										  info->contact_id_bits);
		uint32_t raw_x = touch_read_bits(bytes, base + info->x_bit_offset, info->x_bits);
		uint32_t raw_y = touch_read_bits(bytes, base + info->y_bit_offset, info->y_bits);
		/* Scale device-logical coords (0..x_logical_max / 0..y_logical_max)
		   to the current display-mode coords (HRes / VRes). Devices
		   typically report values 0..(logical_max-1) inclusive, so
		   dividing by logical_max produces a value 0..HRes-1 / 0..VRes-1
		   that aligns with screen-pixel addressing used by everything
		   else (DrawPixel, BLIT, GUI hit-tests). On mode change (e.g.
		   SCREEN 2 on HDMIBTH drops HRes from 1024 to 256) the next
		   report decodes against the new dimensions automatically.
		   Clip raw to logical_max so a device briefly reporting beyond
		   its declared range can't produce scaled > HRes-1. If the
		   display isn't initialised yet (HRes/VRes==0), fall back to
		   the raw value. */
		if (info->x_logical_max > 0 && HRes > 0)
		{
			if (raw_x > (uint32_t)info->x_logical_max)
				raw_x = info->x_logical_max;
			tc->x = (int16_t)((raw_x * (uint32_t)HRes) / (uint32_t)info->x_logical_max);
			if (tc->x >= HRes)
				tc->x = HRes - 1;
		}
		else
		{
			tc->x = (int16_t)raw_x;
		}
		if (info->y_logical_max > 0 && VRes > 0)
		{
			if (raw_y > (uint32_t)info->y_logical_max)
				raw_y = info->y_logical_max;
			tc->y = (int16_t)((raw_y * (uint32_t)VRes) / (uint32_t)info->y_logical_max);
			if (tc->y >= VRes)
				tc->y = VRes - 1;
		}
		else
		{
			tc->y = (int16_t)raw_y;
		}
	}
	out->count = live; /* number of contacts actually touching (tip=1) */

	touch_publish(out);
	return true;
}

/**
 * Print touch_info for debugging at enumeration. Mirrors print_mouse_info.
 */
void print_touch_info(const touch_info_t *info)
{
	if (CurrentLinePtr)
		return;
	MMPrintString("Multi-touch Screen detected\r\n");
	MMPrintString("  Max contacts: ");
	PInt(info->max_contacts);
	PRet();
	MMPrintString("  X range: 0..");
	PInt(info->x_logical_max);
	MMPrintString(" (");
	PInt(info->x_bits);
	MMPrintString(" bits)\r\n");
	MMPrintString("  Y range: 0..");
	PInt(info->y_logical_max);
	MMPrintString(" (");
	PInt(info->y_bits);
	MMPrintString(" bits)\r\n");
	MMPrintString("  Report length: ");
	PInt(info->report_length_bytes);
	MMPrintString(" bytes");
	if (info->uses_report_id)
	{
		MMPrintString(" (Report ID ");
		PInt(info->report_id);
		MMPrintString(")");
	}
	PRet();
}

/* ========================================================================
   TOUCH FIELD-DIAGNOSIS TRACING — currently FORCE-ENABLED for the touch
   debugging round. Comment out the #define below (or delete these 3 lines)
   to return to release behaviour where the dumps compile to nothing.
   Equivalent to building with -DTOUCH_DEBUG.
   ======================================================================== */
#ifndef TOUCH_DEBUG
// #define TOUCH_DEBUG
#endif

/* Touch enumeration/report diagnostics. Build with -DTOUCH_DEBUG to
   enable; otherwise the dump calls compile to nothing. Kept in place so
   the next problem panel can be traced without re-deriving the hooks. */
#ifdef TOUCH_DEBUG

/**
 * Diagnostic dump for a problem touch panel. Prints the raw HID report
 * descriptor (hex, 16 bytes/line with byte offsets) followed by every
 * field analyze_touch_descriptor() extracted from it. Paste the hex into
 * any HID descriptor decoder (e.g. eleccelerator.com/usbdescreqparser) to
 * hand-verify the real Finger layout against what the parser concluded —
 * a mismatch in the bit offsets or report_id explains "detected but no
 * touch". Self-guards on CurrentLinePtr to match the enumeration prints.
 */
void dump_touch_descriptor(const uint8_t *desc, uint16_t len, const touch_info_t *info)
{
	char s[16];
	if (CurrentLinePtr)
		return;
	MMPrintString("---- Touch HID report descriptor (");
	PInt(len);
	MMPrintString(" bytes) ----\r\n");
	for (uint16_t i = 0; i < len; i++)
	{
		if ((i & 0x0F) == 0)
		{
			sprintf(s, "%03X: ", i);
			MMPrintString(s);
		}
		sprintf(s, "%02X ", desc[i]);
		MMPrintString(s);
		if ((i & 0x0F) == 0x0F)
			PRet();
	}
	if (len & 0x0F)
		PRet();
	MMPrintString("---- Parsed touch_info ----\r\n");
	MMPrintString("  uses_report_id=");
	PInt(info->uses_report_id);
	MMPrintString(" report_id=");
	PInt(info->report_id);
	MMPrintString(" report_length_bytes=");
	PInt(info->report_length_bytes);
	PRet();
	MMPrintString("  max_contacts=");
	PInt(info->max_contacts);
	MMPrintString(" first_contact_bit_offset=");
	PInt(info->first_contact_bit_offset);
	MMPrintString(" contact_stride_bits=");
	PInt(info->contact_stride_bits);
	PRet();
	MMPrintString("  contact_count bit_offset=");
	PInt(info->contact_count_bit_offset);
	MMPrintString(" bits=");
	PInt(info->contact_count_bits);
	PRet();
	MMPrintString("  tip_switch_bit_offset=");
	PInt(info->tip_switch_bit_offset);
	MMPrintString(" in_range_bit_offset=");
	PInt(info->in_range_bit_offset);
	PRet();
	MMPrintString("  contact_id bit_offset=");
	PInt(info->contact_id_bit_offset);
	MMPrintString(" bits=");
	PInt(info->contact_id_bits);
	PRet();
	MMPrintString("  X bit_offset=");
	PInt(info->x_bit_offset);
	MMPrintString(" bits=");
	PInt(info->x_bits);
	MMPrintString(" logical_max=");
	PInt(info->x_logical_max);
	PRet();
	MMPrintString("  Y bit_offset=");
	PInt(info->y_bit_offset);
	MMPrintString(" bits=");
	PInt(info->y_bits);
	MMPrintString(" logical_max=");
	PInt(info->y_logical_max);
	PRet();
	MMPrintString("  input_mode=");
	PInt(info->has_input_mode);
	if (info->has_input_mode)
	{
		MMPrintString(" report_id=");
		PInt(info->input_mode_report_id);
	}
	MMPrintString(" bringup: cc_max_id=");
	PInt(info->contact_count_max_report_id);
	MMPrintString(" cert_id=");
	PInt(info->cert_blob_report_id);
	PRet();
	MMPrintString("  pointer_fallback=");
	PInt(info->has_pointer_fallback);
	if (info->has_pointer_fallback)
	{
		MMPrintString(" report_id=");
		PInt(info->pointer_report_id);
		MMPrintString(" button@");
		PInt(info->pointer_button_bit_offset);
		MMPrintString(" X@");
		PInt(info->pointer_x_bit_offset);
		MMPrintString("/");
		PInt(info->pointer_x_bits);
		MMPrintString("b max=");
		PInt(info->pointer_x_logical_max);
		MMPrintString(" Y@");
		PInt(info->pointer_y_bit_offset);
		MMPrintString("/");
		PInt(info->pointer_y_bits);
		MMPrintString("b max=");
		PInt(info->pointer_y_logical_max);
	}
	PRet();
	MMPrintString("---------------------------\r\n");
}

/**
 * Diagnostic dump of the first few Input reports a touch device sends —
 * called AFTER process_touch_report so it can show the raw bytes AND what
 * the decoder made of them on one line-pair. The crucial readouts:
 *   [len]  vs  "expect NB"  → if len < expected, the report is arriving
 *          FRAGMENTED across callbacks (e.g. [64] then a short tail). The
 *          EPIN buffer size doesn't fix that if the host controller hands
 *          up one packet per transfer — software reassembly is then needed.
 *   count / tip / x / y / active → whether the decode actually produced a
 *          live contact (count>0, tip=1, active=1) or silently dropped it.
 * Runs for the life of the connection but PRINTS at most ~6 lines/sec
 * (time-throttled) so a held finger doesn't flood the console — the goal
 * here is to see WHEN a stream stops, not every report. Each printed line is
 * stamped with `T+<ms>` since connect and `#<seq>` (count of ALL reports
 * received, not just printed), so a stall shows as the seq freezing and the
 * lines ceasing at a known time. resets each connection.
 */
#define TOUCH_RAW_DUMP_LIMIT 100000 /* safety cap (~minutes); throttle is the real limiter */
static int touch_raw_dump_count = 0;
static uint64_t touch_dump_last_us = 0;
static uint64_t touch_dump_t0_us = 0;
void reset_touch_raw_dump(void)
{
	touch_raw_dump_count = 0;
	touch_dump_last_us = 0;
	touch_dump_t0_us = 0;
}
void dump_touch_report(const touch_info_t *info, const uint8_t *report,
					   uint16_t len, const touch_report_t *out)
{
	char s[32];
	(void)info;
	if (CurrentLinePtr || touch_raw_dump_count >= TOUCH_RAW_DUMP_LIMIT)
		return;
	uint64_t now = time_us_64();
	if (touch_dump_t0_us == 0)
		touch_dump_t0_us = now;
	touch_raw_dump_count++; /* count every report */
	/* Throttle printing — but never stop, so the stall point is visible. */
	if (touch_dump_last_us != 0 && (now - touch_dump_last_us) < 150000ULL)
		return;
	touch_dump_last_us = now;
	sprintf(s, "T+%lums #%d [", (unsigned long)((now - touch_dump_t0_us) / 1000),
			touch_raw_dump_count);
	MMPrintString(s);
	PInt(len);
	MMPrintString("] ");
	for (uint16_t i = 0; i < len && i < 64; i++)
	{
		sprintf(s, "%02X ", report[i]);
		MMPrintString(s);
	}
	MMPrintString("-> count=");
	PInt(out->count);
	if (out->count > 0)
	{
		MMPrintString(" tip=");
		PInt(out->contacts[0].tip);
		MMPrintString(" x=");
		PInt(out->contacts[0].x);
		MMPrintString(" y=");
		PInt(out->contacts[0].y);
	}
	MMPrintString(" active=");
	PInt(usb_touch_active);
	PRet();
}

#else /* !TOUCH_DEBUG — compile the dump calls out entirely */
#define dump_touch_descriptor(desc, len, info) ((void)0)
#define reset_touch_raw_dump() ((void)0)
#define dump_touch_report(info, report, len, out) ((void)0)
#endif /* TOUCH_DEBUG */

/**
 * Expected total length (bytes, including the report-ID byte) of the Input
 * report identified by the leading byte `rid`. Used by the fragment
 * reassembler to know when a multi-packet report is complete. Returns 0
 * for an unrecognised report ID — the caller then treats the chunk as a
 * self-contained report (process it as-is).
 */
static uint16_t touch_expected_len(const touch_info_t *info, uint8_t rid)
{
	if (!info->uses_report_id)
		return info->report_length_bytes;
	if (rid == info->report_id)
		return info->report_length_bytes;
	if (info->has_pointer_fallback && rid == info->pointer_report_id)
		return info->pointer_report_length_bytes;
	return 0;
}

/**
 * Frame touch Input reports out of the raw interrupt-IN byte stream and
 * decode each complete one. Needed because the RP2350 USB host doesn't hand
 * us one tidy report per callback:
 *
 *   - Long reports are SPLIT: it completes the transfer one USB packet
 *     (≤ wMaxPacketSize, 64 bytes) at a time regardless of
 *     CFG_TUH_HID_EPIN_BUFSIZE, so a 10-contact 84-byte report arrives as
 *     64 + 20. A leading fragment lacks the contact-count byte → decoded 0.
 *   - Fast panels COALESCE: several short reports land in one callback
 *     (e.g. the ILI2132 delivering two 6-byte pointer reports as one 12-byte
 *     chunk), sometimes with a couple of stray bytes between them so the
 *     reports don't sit on clean boundaries. Decoding only the first lost
 *     samples and left usb_touch_active stale (a rejected fragment returns
 *     before touch_publish() runs).
 *
 * Both are handled by accumulating bytes in a stream buffer and repeatedly
 * pulling reports off the front: the leading byte is a report ID whose total
 * length touch_expected_len() knows. With enough bytes we decode and advance;
 * a known ID with too few bytes waits for the next callback (the split case);
 * an unrecognised leading byte means we're mis-aligned, so skip one byte to
 * resync (the stray-byte case). A single static buffer serves the normally
 * single active touch panel; a different device resets it. Always-on.
 */
static void touch_reassemble(uint8_t dev_addr, uint8_t instance,
							 const uint8_t *report, uint16_t len,
							 const touch_info_t *info, touch_report_t *out)
{
	static uint8_t buf[256];
	static uint16_t fill = 0;
	static uint8_t owner_addr = 0xFF, owner_inst = 0xFF;

	/* A different device (or first ever call) restarts the stream. */
	if (owner_addr != dev_addr || owner_inst != instance)
	{
		owner_addr = dev_addr;
		owner_inst = instance;
		fill = 0;
	}

	/* Append. If the buffer would overflow we've stalled on undecodable
	   data — drop it and resync on the fresh chunk rather than wedge. */
	if ((uint32_t)fill + len > sizeof(buf))
		fill = 0;
	uint16_t copy = (len > sizeof(buf)) ? (uint16_t)sizeof(buf) : len;
	memcpy(buf + fill, report, copy);
	fill += copy;

	/* Pull complete reports off the front. */
	uint16_t pos = 0;
	while (pos < fill)
	{
		uint16_t exp = touch_expected_len(info, buf[pos]);
		if (exp == 0)
		{
			pos++; /* not a known report ID → mis-aligned, resync */
			continue;
		}
		if (fill - pos < exp)
			break; /* known report, not all here yet → await next callback */
		process_touch_report(buf + pos, exp, info, out);
		pos += exp;
	}

	/* Keep the unconsumed tail for next time. */
	if (pos > 0)
	{
		if (pos < fill)
			memmove(buf, buf + pos, fill - pos);
		fill -= pos;
	}

	/* A report that fits one USB packet (<= wMaxPacketSize, 64) always
	   arrives whole in a single callback — so any leftover after extracting
	   the whole reports is padding or stray bytes and MUST be dropped. If we
	   carried it, a payload byte that happens to equal a report ID (these
	   62-byte reports contain 0x04 in their footer) could anchor the parser
	   one report out of phase and, because that byte is a "valid" report ID,
	   it would never resync — every report then decodes as garbage/count=0.
	   Only a genuinely fragmented report (length > 64, still awaiting later
	   packets) is legitimately carried across callbacks. */
	if (fill > 0)
	{
		uint16_t exp0 = touch_expected_len(info, buf[0]);
		if (!(exp0 > 64 && fill < exp0))
			fill = 0;
	}
}

/* TOUCH Device_type — same numbering scheme as PS4/PS3/SNES/XBOX/UNKNOWN. */
#ifndef TOUCH
#define TOUCH 133
#endif
/* TOUCHMOUSE: a standalone absolute-pointer interface (e.g. a composite
   touch monitor's "mouse" interface that reports touch as absolute X/Y).
   Processed exactly like TOUCH (report routed through process_pointer_report)
   but kept a distinct type so its connect/disconnect messages are its own. */
#ifndef TOUCHMOUSE
#define TOUCHMOUSE 134
#endif

/* process_kbd_report is declared in KeyboardMap.h. */

/*
 * Force a real SE0 bus reset on the root USB port.
 *
 * hcd_port_reset() in the pico-sdk TinyUSB driver is a no-op stub, so driving
 * the PHY output directly is the only way to assert a USB reset on RP2040 /
 * RP2350. This matters for externally-powered hubs: they never lose VBUS when
 * MMBasic reboots, so the hub keeps the USB address and configured state it
 * held in the previous session and ignores the re-enumeration from address 0
 * (the whole device tree "vanishes"). An SE0 reset forces a directly-attached
 * hub back to Default state; TinyUSB then cascades the reset to the devices
 * behind it as it re-enumerates each downstream port.
 *
 * The controller must already be initialised (tuh_init / rp2040_usb_init) so
 * the PHY is powered and muxed before this is called.
 */
#define USB_BUS_RESET_PHY_OVERRIDE_EN (USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS | \
									   USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS)
void USB_bus_reset(void)
{
	// Take manual control of the DP/DM output drivers.
	hw_set_bits(&usb_hw->phy_direct_override, USB_BUS_RESET_PHY_OVERRIDE_EN);
	// Enable the output drivers, then pull both lines low = SE0 = bus reset.
	hw_set_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DM_OE_BITS | USB_USBPHY_DIRECT_TX_DP_OE_BITS);
	hw_clear_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DM_BITS | USB_USBPHY_DIRECT_TX_DP_BITS);

	// Hold the reset. USB spec minimum is 10 ms; drive ~20 ms so that slow /
	// cheap hubs sample it reliably.
	uSec(20000);

	// Release: hand DP/DM back to the SIE. Clear ALL four override-enable bits
	// (the previous version cleared only the two DM bits, leaving DP under
	// manual control after return - a likely cause of the old unreliability).
	hw_clear_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DM_OE_BITS | USB_USBPHY_DIRECT_TX_DP_OE_BITS);
	hw_clear_bits(&usb_hw->phy_direct_override, USB_BUS_RESET_PHY_OVERRIDE_EN);
}
void clearrepeat(void)
{
	keytimer = 0;
	repeattime = Option.RepeatStart;
	memset(KeyDown, 0, sizeof(KeyDown));
}
bool diff_than_2(uint8_t x, uint8_t y)
{
	return (x - y > 4) || (y - x > 4);
}
static inline bool is_sony_ds4(uint8_t dev_addr)
{
	uint16_t vid, pid;
	tuh_vid_pid_get(dev_addr, &vid, &pid);
	return ((vid == 0x054c && (pid == 0x09cc || pid == 0x05c4)) // Sony DualShock4
			|| (vid == 0x0f0d && pid == 0x005e)					// Hori FC4
			|| (vid == 0x0f0d && pid == 0x00ee)					// Hori PS4 Mini (PS4-099U)
			|| (vid == 0x1f4f && pid == 0x1002)					// ASW GG xrd controller
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
	return ((vid == 0x054c && (pid == 0x0268)) // Sony DualShock3
	);
}
bool diff_report(sony_ds4_report_t const *rpt1, sony_ds4_report_t const *rpt2)
{
	bool result;

	// x, y, z, rz must different than 2 to be counted
	result = diff_than_2(rpt1->x, rpt2->x) || diff_than_2(rpt1->y, rpt2->y) ||
			 diff_than_2(rpt1->z, rpt2->z) || diff_than_2(rpt1->rz, rpt2->rz);

	// check the rest with mem compare
	result |= memcmp(&rpt1->rz + 1, &rpt2->rz + 1, sizeof(sony_ds4_report_t) - 6);

	return result;
}
/*
BIT 0 Button R/R1
BIT 1 Button start/options
BIT 2 Button home
BIT 3 Button select/share
BIT 4 Button L/L1
BIT 5 Button down cursor
BIT 6 Button right cursor
BIT 7 Button up cursor
BIT 8 Button left cursor
BIT 9 Right shoulder button 2/R2
BIT 10 Button x/triangle
BIT 11 Button a/circle
BIT 12 Button y/square
BIT 13 Button b/cross
BIT 14 Left should button 2/L2
BIT 15 Touchpad
*/
#define p_R 1
#define p_START (1 << 1)
#define p_HOME (1 << 2)
#define p_SELECT (1 << 3)
#define p_L (1 << 4)
#define p_DOWN (1 << 5)
#define p_RIGHT (1 << 6)
#define p_UP (1 << 7)
#define p_LEFT (1 << 8)
#define p_R2 (1 << 9)
#define p_X (1 << 10)
#define p_A (1 << 11)
#define p_Y (1 << 12)
#define p_B (1 << 13)
#define p_L2 (1 << 14)
#define p_TOUCH (1 << 15)
/*void process_buffalo_gamepad(uint8_t const* report, uint16_t len, uint8_t n){
	nunstruct[n].type=SNES;
		uint16_t b=0;
		if(report[1] < 0x40)b|=b_UP;
		if(report[1] > 0xC0)b|=b_DOWN;
		if(report[0] < 0x40)b|=b_LEFT;
		if(report[0] > 0xC0)b|=b_RIGHT;
		if(report[2] & 0x01)b|=b_A;
		if(report[2] & 0x02)b|=b_B;
		if(report[2] & 0x04)b|=b_X;
		if(report[2] & 0x08)b|=b_Y;
		if(report[2] & 0x10)b|=b_L;
		if(report[2] & 0x20)b|=b_R;
//		if(report[6] & 0x10)b|=b_SELECT;
//		if(report[6] & 0x20)b|=b_START;
		if((b ^ nunstruct[n].x0) & nunstruct[n].x1){
			nunfoundc[n]=1;
		}
		nunstruct[n].x0=b;
}
void process_specific_gamepad(uint8_t const* report, uint16_t len, uint8_t n){
	nunstruct[n].type=SNES;
		uint16_t b=0;
		if(report[4] < 0x40)b|=b_UP;
		if(report[4] > 0xC0)b|=b_DOWN;
		if(report[3] < 0x40)b|=b_LEFT;
		if(report[3] > 0xC0)b|=b_RIGHT;
		if(report[5] & 0x20)b|=b_A;
		if(report[5] & 0x40)b|=b_B;
		if(report[5] & 0x10)b|=b_X;
		if(report[5] & 0x80)b|=b_Y;
		if(report[6] & 0x01)b|=b_L;
		if(report[6] & 0x02)b|=b_R;
		if(report[6] & 0x10)b|=b_SELECT;
		if(report[6] & 0x20)b|=b_START;
		if((b ^ nunstruct[n].x0) & nunstruct[n].x1){
			nunfoundc[n]=1;
		}
		nunstruct[n].x0=b;
}*/
void checkpush(uint8_t const *report, uint16_t len, struct s_Buttons button, uint16_t set, uint16_t *b)
{
	if (button.index == 0xFF)
		return;
	if (button.code == 192)
	{
		if (report[button.index] > 192)
			*b |= set;
	}
	else if (button.code == 64)
	{
		if (report[button.index] < 64)
			*b |= set;
	}
	else if (button.code < 8)
	{
		if (report[button.index] & (1 << button.code))
			*b |= set;
	}
	else if (button.code > 128 && button.code < 136)
	{
		if ((report[button.index] & (1 << button.code)) == 0)
			*b |= set;
	}
	else
		error("Internal data error");
}
void PIntHN(unsigned long long int n, int l)
{
	char s[128];
	for (int i = 0; i < 128; i++)
		s[i] = '0';
	IntToStr(&s[64], (int64_t)n, 16);
	MMPrintString(&s[64 - (l - strlen(&s[64]))]);
}

void process_generic_gamepad(uint8_t const *report, uint16_t len, uint8_t n)
{
	if (monitor && !nooutput)
	{
		static uint8_t lastreport[64] = {0};
		if (memcmp(report, lastreport, len))
		{
			PIntHN(HID[n - 1].vid, 4);
			putConsole(',', 0);
			PIntHN(HID[n - 1].pid, 4);
			PRet();
			PIntHN(lastreport[0], 2);
			for (int i = 1; i < len; i++)
			{
				putConsole(',', 0);
				PIntHN(lastreport[i], 2);
			}
			PRet();
			PIntHN(report[0], 2);
			for (int i = 1; i < len; i++)
			{
				putConsole(',', 0);
				PIntHN(report[i], 2);
			}
			PRet();
		}
		memcpy(lastreport, report, len);
		return;
	}
	if (monitor && nooutput)
		return;
	uint16_t b = 0;
	int i = 0;
	struct s_Gamepad Gamepad;
	if (MyGamepad.pid == HID[n - 1].pid && MyGamepad.vid == HID[n - 1].vid)
	{
		memcpy(&Gamepad, &MyGamepad, sizeof(struct s_Gamepad));
		goto process; // user specified decode
	}
	while (Gamepads[i].pid)
	{
		if (Gamepads[i].pid == HID[n - 1].pid && Gamepads[i].vid == HID[n - 1].vid)
			break;
		i++;
	}
	if (Gamepads[i].pid == 0)
		return;
	memcpy(&Gamepad, &Gamepads[i], sizeof(struct s_Gamepad));
process:;
	nunstruct[n].type = SNES;
	checkpush(report, len, Gamepad.b_A, p_A, &b);
	checkpush(report, len, Gamepad.b_B, p_B, &b);
	checkpush(report, len, Gamepad.b_DOWN, p_DOWN, &b);
	checkpush(report, len, Gamepad.b_HOME, p_HOME, &b);
	checkpush(report, len, Gamepad.b_L2, p_L2, &b);
	checkpush(report, len, Gamepad.b_L, p_L, &b);
	checkpush(report, len, Gamepad.b_LEFT, p_LEFT, &b);
	checkpush(report, len, Gamepad.b_R2, p_R2, &b);
	checkpush(report, len, Gamepad.b_R, p_R, &b);
	checkpush(report, len, Gamepad.b_RIGHT, p_RIGHT, &b);
	checkpush(report, len, Gamepad.b_SELECT, p_SELECT, &b);
	checkpush(report, len, Gamepad.b_START, p_START, &b);
	checkpush(report, len, Gamepad.b_TOUCH, p_TOUCH, &b);
	checkpush(report, len, Gamepad.b_UP, p_UP, &b);
	checkpush(report, len, Gamepad.b_X, p_X, &b);
	checkpush(report, len, Gamepad.b_Y, p_Y, &b);
	if ((b ^ nunstruct[n].x0) & nunstruct[n].x1)
	{
		nunfoundc[n] = 1;
	}
	nunstruct[n].x0 = b;
}
void process_sony_ds3(uint8_t const *report, uint16_t len, uint8_t n)
{
	nunstruct[n].type = PS3;
	uint16_t b = 0;
	if (report[3] & 0x08)
		b |= 1;
	if (report[2] & 0x08)
		b |= 1 << 1;
	if (report[4] & 0x01)
		b |= 1 << 2;
	if (report[2] & 0x01)
		b |= 1 << 3;
	if (report[3] & 0x04)
		b |= 1 << 4;
	if (report[2] & 0x40)
		b |= 1 << 5;
	if (report[2] & 0x20)
		b |= 1 << 6;
	if (report[2] & 0x10)
		b |= 1 << 7;
	if (report[2] & 0x80)
		b |= 1 << 8;
	if (report[3] & 0x02)
		b |= 1 << 9;
	if (report[3] & 0x10)
		b |= 1 << 10;
	if (report[3] & 0x20)
		b |= 1 << 11;
	if (report[3] & 0x80)
		b |= 1 << 12;
	if (report[3] & 0x40)
		b |= 1 << 13;
	if (report[3] & 0x01)
		b |= 1 << 14;
	nunstruct[n].ax = report[6];
	nunstruct[n].ay = report[7];
	nunstruct[n].Z = report[8];
	nunstruct[n].C = report[9];
	nunstruct[n].L = report[18];
	nunstruct[n].R = report[19];
	if ((b ^ nunstruct[n].x0) & nunstruct[n].x1)
	{
		nunfoundc[n] = 1;
	}
	nunstruct[n].x0 = b;
}
void process_sony_ds4(uint8_t const *report, uint16_t len, uint8_t n)
{
	// const char* dpad_str[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW", "none" };

	// previous report used to compare for changes

	uint8_t const report_id = report[0];
	report++;
	len--;

	// all buttons state is stored in ID 1
	if (report_id == 1)
	{
		sony_ds4_report_t ds4_report;
		memcpy(&ds4_report, report, sizeof(ds4_report));
		nunstruct[n].type = PS4;
		uint16_t b = 0;
		if (ds4_report.r1)
			b |= 1;
		if (ds4_report.option)
			b |= 1 << 1;
		if (ds4_report.ps)
			b |= 1 << 2;
		if (ds4_report.share)
			b |= 1 << 3;
		if (ds4_report.l1)
			b |= 1 << 4;
		if (ds4_report.dpad == 5)
			b |= 1 << 5;
		if (ds4_report.dpad == 3)
			b |= 3 << 5;
		if (ds4_report.dpad == 2)
			b |= 1 << 6;
		if (ds4_report.dpad == 1)
			b |= 3 << 6;
		if (ds4_report.dpad == 0)
			b |= 1 << 7;
		if (ds4_report.dpad == 6)
			b |= 1 << 8;
		if (ds4_report.dpad == 7)
			b |= ((1 << 8) | (1 << 5));
		if (ds4_report.r2)
			b |= 1 << 9;
		if (ds4_report.triangle)
			b |= 1 << 10;
		if (ds4_report.circle)
			b |= 1 << 11;
		if (ds4_report.square)
			b |= 1 << 12;
		if (ds4_report.cross)
			b |= 1 << 13;
		if (ds4_report.l2)
			b |= 1 << 14;
		if (ds4_report.tpad)
			b |= 1 << 15;
		if (ds4_report.dpad == 4)
			b |= 1 << 5;
		nunstruct[n].ax = ds4_report.x;
		nunstruct[n].ay = ds4_report.y;
		nunstruct[n].Z = ds4_report.z;
		nunstruct[n].C = ds4_report.rz;
		nunstruct[n].L = ds4_report.l2_trigger;
		nunstruct[n].R = ds4_report.r2_trigger;
		memcpy((void *)nunstruct[n].gyro, ds4_report.gyro, 6 * sizeof(uint16_t));
		if ((b ^ nunstruct[n].x0) & nunstruct[n].x1)
		{
			nunfoundc[n] = 1;
		}
		nunstruct[n].x0 = b;
	}
}
/* Scratch buffer for the touch digitizer-init GET_FEATURE reads. Sized for
   the largest one (the 256-byte certification blob, report ID 6). The data
   is only needed so the device sees the read happen — we don't parse it. */
static uint8_t touch_feature_buf[256];

/* Completion of a GET_FEATURE control transfer. Used to sequence the touch
   digitizer-init handshake (see hid_app_task): advance to the next read, or
   mark it done. Independent of report polling. */
void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t instance,
									uint8_t report_id, uint8_t report_type, uint16_t len)
{
	for (int i = 0; i < 4; i++)
	{
		if (HID[i].Device_address == dev_addr && HID[i].Device_instance == instance &&
			HID[i].Device_type == TOUCH)
		{
#ifdef TOUCH_DEBUG
			/* Dump the raw read value BEFORE we modify it. */
			if (!CurrentLinePtr)
			{
				MMPrintString("Touch GET_FEATURE id=");
				PInt(report_id);
				MMPrintString(" len=");
				PInt(len);
				if (len >= 2)
				{
					MMPrintString(" data=");
					PIntH(touch_feature_buf[0]);
					MMPrintString(",");
					PIntH(touch_feature_buf[1]);
				}
				PRet();
			}
#endif
			if (HID[i].touch_init_step == 2)
				HID[i].touch_init_step = 3; /* got blob → read contact-count-max */
			else if (HID[i].touch_init_step == 4)
				HID[i].touch_init_step = 0; /* handshake complete */
			else if (HID[i].touch_init_step == 12)
			{
				/* Read-modify-write Input Mode: we just read the current
				   [Device Mode, Device Identifier]; force Device Mode to 0x02
				   (touchscreen) but keep the identifier the panel returned,
				   then write it back (step 13). */
				touch_feature_buf[0] = 0x02;
				HID[i].touch_init_step = 13;
			}
			break;
		}
	}
}

/* Completion of a SET_FEATURE control transfer — used to clear the touch
   Input Mode write's in-flight state. */
void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance,
									uint8_t report_id, uint8_t report_type, uint16_t len)
{
	for (int i = 0; i < 4; i++)
	{
		if (HID[i].Device_address == dev_addr && HID[i].Device_instance == instance &&
			HID[i].Device_type == TOUCH && HID[i].touch_init_step == 11)
		{
			HID[i].touch_init_step = 0; /* Input Mode write done */
#ifdef TOUCH_DEBUG
			if (!CurrentLinePtr)
			{
				MMPrintString("Touch SET_FEATURE InputMode id=");
				PInt(report_id);
				MMPrintString(" len=");
				PInt(len);
				PRet();
			}
#endif
			break;
		}
	}
}

/* Periodic touch handshake re-assertion. 0 = disabled (A/B test whether the
   panels actually revert after the one-shot bring-up). 1 = re-assert every 2 s. */
#define TOUCH_KEEPALIVE 0

void hid_app_task(void)
{
	static uint64_t timer;
	uint64_t timenow = time_us_64();
	if (!USBenabled)
		return;
	if (KeyDown[0] && keytimer > repeattime)
	{
		uint8_t c = KeyDown[0];
		if (!(c == 0 || c == PDOWN || c == PUP || c == CTRLKEY('P') || c == CTRLKEY('L') || c == 25 || c == F5 || c == F4 || c == CTRLKEY('T') /* || (markmode && (c==DEL || c==CTRLKEY(']')))*/))
		{
			USR_KEYBRD_ProcessData(c);
		}
		//			if(c==PDOWN || c==PUP || c==CTRLKEY('P') || c==CTRLKEY('L') || c==25 || c==F5  || c==F4 || c==CTRLKEY('T')/* || (markmode && (c==DEL || c==CTRLKEY(']')))*/)mmemset(&last_k_info,0,sizeof(HID_KEYBD_Info_TypeDef));
		keytimer = 0;
		repeattime = Option.RepeatRate;
	}
	for (int i = 0; i < 4; i++)
	{
		if (HID[i].active == false)
			continue;
		/* Keep-alive: some panels (ILI2132) were seen to stay in digitizer
		   mode only a few seconds after the bring-up handshake, then revert.
		   Re-asserting the handshake every 2 s prevents that. Currently
		   DISABLED for an A/B test — set TOUCH_KEEPALIVE to 1 (defined above
		   hid_app_task) to re-enable. If the panel turns out to revert, the
		   preferred fix is a reactive version (re-assert only on a stall). */
#if TOUCH_KEEPALIVE
		if (HID[i].Device_type == TOUCH && HID[i].touch_init_step == 0 &&
			(HID[i].touch_info.has_input_mode || HID[i].touch_info.has_pointer_fallback) &&
			(timenow - HID[i].touch_init_us) > 2000000ULL)
		{
			HID[i].touch_init_us = timenow;
			HID[i].touch_init_step = HID[i].touch_info.has_input_mode ? 10 : 1;
		}
#endif
		/* Touch digitizer-init handshake. Two mechanisms, by step value:
			 steps 10/13 -> STANDARD: read-modify-write Input Mode (Device
			   Mode) to 0x02 to switch a Windows-compliant panel into
			   multi-touch. Read (10) the current [mode, identifier], force
			   mode=0x02 keeping the identifier, write back (13). The read
			   first matches Linux hid-multitouch's FORCE_GET_FEATURE: some
			   panels reject a blind write that zeroes the identifier.
			 steps 1/3 -> GET_FEATURE the Windows bring-up reports (cert
			   blob, then contact-count-max) at report IDs captured from the
			   descriptor, for dual-mode panels that lack Input Mode (ILI2132).
		   All use EP0 — if it can't be issued this cycle (control pipe busy)
		   we retry next time; in-flight steps (2/4/11/12) are cleared only by
		   the completion callbacks. A panel that STALLs the request simply
		   stops here without affecting report polling below. */
		if (HID[i].Device_type == TOUCH && HID[i].touch_init_step == 10)
		{
			/* Read the current Input Mode feature ([Device Mode, Device
			   Identifier]) into the buffer; the GET completion forces
			   mode=0x02 and advances to the write (step 13). */
			if (tuh_hid_get_report(HID[i].Device_address, HID[i].Device_instance,
								   HID[i].touch_info.input_mode_report_id,
								   HID_REPORT_TYPE_FEATURE, touch_feature_buf, 2))
				HID[i].touch_init_step = 12; /* GET in-flight */
		}
		else if (HID[i].Device_type == TOUCH && HID[i].touch_init_step == 13)
		{
			/* Write the modified Input Mode (Device Mode = 0x02, identifier
			   preserved by the GET completion). */
			if (tuh_hid_set_report(HID[i].Device_address, HID[i].Device_instance,
								   HID[i].touch_info.input_mode_report_id,
								   HID_REPORT_TYPE_FEATURE, touch_feature_buf, 2))
				HID[i].touch_init_step = 11; /* SET in-flight */
		}
		else if (HID[i].Device_type == TOUCH &&
				 (HID[i].touch_init_step == 1 || HID[i].touch_init_step == 3))
		{
			/* step 1 -> cert blob, step 3 -> contact-count-max; report IDs
			   come from the descriptor. A report absent on this panel is
			   skipped (advance the step) rather than read at a guessed ID. */
			uint8_t rid = (HID[i].touch_init_step == 1)
							  ? HID[i].touch_info.cert_blob_report_id
							  : HID[i].touch_info.contact_count_max_report_id;
			if (rid == 0)
				/* absent → jump past the in-flight state: 1->3, 3->0(done) */
				HID[i].touch_init_step = (HID[i].touch_init_step == 1) ? 3 : 0;
			else if (tuh_hid_get_report(HID[i].Device_address, HID[i].Device_instance,
										rid, HID_REPORT_TYPE_FEATURE, touch_feature_buf,
										sizeof(touch_feature_buf)))
				HID[i].touch_init_step++; /* -> in-flight (2 or 4) */
		}
		if (HID[i].report_requested)
			continue;
		if (HID[i].report_timer >= HID[i].report_rate)
		{
			if (HID[i].Device_type == HID_ITF_PROTOCOL_KEYBOARD && HID[i].notfirsttime == 0)
			{
				HID[i].notfirsttime = 1;
				tuh_hid_set_report(HID[i].Device_address, HID[i].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[i].sendlights, 1);
			}
			HID[i].report_requested = true;
			if (!tuh_hid_receive_report(HID[i].Device_address, HID[i].Device_instance))
			{
				// Allow retry on next poll cycle instead of permanent deactivation
				HID[i].report_requested = false;
				HID[i].report_timer = 0;
			}
		}
		if (HID[i].Device_type == PS4 && timenow - timer > 50000)
		{
			timer = timenow;
			sony_ds4_output_report_t output_report = {0};
			output_report.set_rumble = 1;
			output_report.motor_left = HID[i].motorleft;
			output_report.motor_right = HID[i].motorright;
			output_report.set_led = 1;
			output_report.lightbar_red = HID[i].r;
			output_report.lightbar_blue = HID[i].b;
			output_report.lightbar_green = HID[i].g;
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
int FindFreeSlot(uint8_t itf_protocol)
{
	if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD && !HID[0].active)
		return 0;
	if (itf_protocol == HID_ITF_PROTOCOL_MOUSE && !HID[1].active)
		return 1;
	if (itf_protocol == HID_ITF_PROTOCOL_NONE)
	{
		if (!HID[2].active)
			return 2;
		if (!HID[3].active)
			return 3;
	}
	for (int i = 3; i >= 0; i--)
	{
		if (!HID[i].active)
			return i;
	}
	return -1;
}
// static struct
//{
//   uint8_t report_count;
//   tuh_hid_report_info_t report_info[MAX_REPORT];
// }hid_info[CFG_TUH_HID];

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
	__dsb();
	uint16_t pid, vid;
	uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
	//  PInt(itf_protocol);
	tuh_vid_pid_get(dev_addr, &vid, &pid);
	int slot = FindFreeSlot(itf_protocol);
	if (slot == -1)
		return; /* all slots in use by devices we service — skip this
				   interface quietly (composite panels expose extra vendor
				   interfaces we don't use; don't error from a USB callback) */
	HID[slot].vid = vid;
	HID[slot].pid = pid;
	//  char buff[STRINGSIZE];
	//  PIntHC(vid);PIntHC(pid);PRet();
	//  sprintf(buff,"HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
	/* Start HID Interface */
	if (Option.USBKeyboard == CONFIG_UK)
		keylayout = UKkeyValue;
	else if (Option.USBKeyboard == CONFIG_US)
		keylayout = USkeyValue;
	else if (Option.USBKeyboard == CONFIG_GR)
		keylayout = DEkeyValue;
	else if (Option.USBKeyboard == CONFIG_FR)
		keylayout = FRkeyValue;
	else if (Option.USBKeyboard == CONFIG_ES)
		keylayout = ESkeyValue;
	else if (Option.USBKeyboard == CONFIG_BE)
		keylayout = BEkeyValue;
	if (Current_USB_devices == 4)
		return; /* full — skip quietly rather than error mid-enumeration */

	// Interface protocol (hid_interface_protocol_enum_t)
	//  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };

	//  sprintf(buff,"HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);
	if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
	{
		//		char buff[128];
		//		hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
		//			sprintf(buff,"HID has %u reports \r\n", hid_info[instance].report_count);
		//			MMPrintString(buff);
		HID[slot].Device_address = dev_addr;
		HID[slot].Device_instance = instance;
		HID[slot].Device_type = HID_ITF_PROTOCOL_KEYBOARD;
		caps_lock = Option.capslock;
		num_lock = Option.numlock;
		if (num_lock)
			HID[slot].sendlights |= (uint8_t)1;
		if (caps_lock)
			HID[slot].sendlights |= (uint8_t)2;
		HID[slot].report_rate = 20; // mSec between report
		HID[slot].report_timer = -(10 + (slot + 2) * 500);
		HID[slot].active = true;
		HID[slot].report_requested = false;
		if (!CurrentLinePtr)
		{
			MMPrintString((char *)KBrdList[(int)Option.USBKeyboard]);
			MMPrintString(" USB Keyboard Connected on channel ");
			PInt(slot + 1);
			MMPrintString("\r\n> ");
		}
		//		tuh_hid_set_report(HID[slot].Device_address, HID[slot].Device_instance, 0, HID_REPORT_TYPE_OUTPUT, (void *)&HID[n].sendlights,1);
		PlayMemWav(ezyZip_wav, EZYZIP_WAV_SIZE);
		Current_USB_devices++;
		return;
	}
	// By default host stack will use activate boot protocol on supported interface.
	// Therefore for this simple example, we only need to parse generic report descriptor (with built-in parser)
	if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
	{
		HID[slot].Device_address = dev_addr;
		HID[slot].Device_instance = instance;
		HID[slot].Device_type = HID_ITF_PROTOCOL_MOUSE;
		HID[slot].report_rate = 20; // mSec between reports
		HID[slot].report_timer = -(10 + (slot + 2) * 500);
		HID[slot].active = true;
		HID[slot].report_requested = false;
		// Switch from boot protocol to report protocol to enable wheel support
		// Analyze the mouse descriptor
		if (Option.mousespeed > 0.0)
		{
			mouse_info_t mouse_info;
			if (desc_len > 0 && desc_report != NULL)
			{
				mouse_report_type_t mouse_type = analyze_mouse_descriptor(desc_report, desc_len, &mouse_info);

				// Store the mouse type for later use
				HID[slot].mouse_type = mouse_type;
				memcpy((void *)&HID[slot].mouse_info, &mouse_info, sizeof(mouse_info_t));

				// Print info
				if (!CurrentLinePtr)
				{
					print_mouse_info(&mouse_info);
				}
			}
			tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
		}
		else
			HID[slot].mouse_type = MOUSE_TYPE_STANDARD_8BIT;
		if (!CurrentLinePtr)
		{
			MMPrintString("USB Mouse Connected on channel ");
			PInt(slot + 1);
			MMPrintString("\r\n> ");
		}
		PlayMemWav(ezyZip_wav, EZYZIP_WAV_SIZE);
		Current_USB_devices++;
		return;
	}
	if (itf_protocol == HID_ITF_PROTOCOL_NONE)
	{
		/* Multi-touch USB digitizers enumerate as HID_ITF_PROTOCOL_NONE
		   with a Touch Screen application collection in their report
		   descriptor. Test for that BEFORE the gamepad VID/PID matches —
		   touch screens don't match any of those and would otherwise
		   fall through to is_generic and be misidentified as a SNES
		   gamepad. Touch is routed to slot 3 (channel 4) by default;
		   if channel 4 is already taken, fall back to whatever
		   FindFreeSlot picked. */
		{
			touch_info_t tinfo;
			bool is_digitizer = (desc_report && desc_len > 0 &&
								 analyze_touch_descriptor(desc_report, desc_len, &tinfo));
			if (is_digitizer)
			{
				int touch_slot = (HID[3].active && slot != 3) ? slot : 3;
				HID[touch_slot].Device_address = dev_addr;
				HID[touch_slot].Device_instance = instance;
				HID[touch_slot].Device_type = TOUCH;
				memcpy((void *)&HID[touch_slot].touch_info, &tinfo,
					   sizeof(touch_info_t));
				memset((void *)&HID[touch_slot].touch_report, 0,
					   sizeof(touch_report_t));
				/* Diagnostic: dump the descriptor + parsed layout, and arm
				   the raw-report dump for this connection. */
				dump_touch_descriptor(desc_report, desc_len, &tinfo);
				reset_touch_raw_dump();
				HID[touch_slot].report_rate = 5; /* fast polling — fingers move */
				HID[touch_slot].report_timer = -(10 + (touch_slot + 2) * 500);
				HID[touch_slot].active = true;
				HID[touch_slot].report_requested = false;
				/* Drive generic touch-panel state from this device. */
				usb_touch_present = true;
				usb_touch_active = false;
				usb_touch_active2 = false;
				/* Switch out of boot protocol so we get the multi-touch
				   report layout instead of the BIOS-mode boot-mouse
				   compatibility report (some touch screens default to
				   that for BIOS / pre-boot environments). */
				tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
				/* Dual-mode panels (mouse + digitizer collections) may sit
				   in mouse-compat mode until the host performs the Windows
				   touch bring-up: GET_FEATURE on the digitizer feature
				   reports. Arm that handshake — hid_app_task() issues the
				   reads, sequenced via tuh_hid_get_report_complete_cb(). It
				   runs independently of report polling, so a STALL on an
				   absent feature can't stop touch working. */
				HID[touch_slot].touch_init_step =
					tinfo.has_input_mode ? 10 : (tinfo.has_pointer_fallback ? 1 : 0);
				HID[touch_slot].touch_init_us = time_us_64();
				if (!CurrentLinePtr)
				{
					MMPrintString("Multi-touch Connected on channel ");
					PInt(touch_slot + 1);
					MMPrintString(" (");
					PInt(tinfo.max_contacts);
					MMPrintString(" contacts, ");
					PInt(tinfo.x_logical_max);
					MMPrintString("x");
					PInt(tinfo.y_logical_max);
					MMPrintString(")\r\n> ");
				}
				PlayMemWav(ezyZip_wav, EZYZIP_WAV_SIZE);
				Current_USB_devices++;
				return;
			}
			/* Standalone absolute-pointer interface — e.g. a composite touch
			   monitor's "mouse" interface that reports touch as absolute X/Y.
			   analyze_touch_descriptor() returned false (no Finger
			   collections) but still captured the pointer layout. Route it
			   into the touch pipeline as a single-touch source: report
			   processing reuses process_pointer_report() via TOUCHMOUSE. */
			if (desc_report && desc_len > 0 && tinfo.has_pointer_fallback)
			{
				tinfo.valid = true; /* pointer-only, but a usable touch source */
				HID[slot].Device_address = dev_addr;
				HID[slot].Device_instance = instance;
				HID[slot].Device_type = TOUCHMOUSE;
				memcpy((void *)&HID[slot].touch_info, &tinfo, sizeof(touch_info_t));
				memset((void *)&HID[slot].touch_report, 0, sizeof(touch_report_t));
				dump_touch_descriptor(desc_report, desc_len, &tinfo);
				reset_touch_raw_dump();
				HID[slot].report_rate = 5;
				HID[slot].report_timer = -(10 + (slot + 2) * 500);
				HID[slot].active = true;
				HID[slot].report_requested = false;
				HID[slot].touch_init_step = 0; /* no digitizer handshake */
				usb_touch_present = true;
				usb_touch_active = false;
				usb_touch_active2 = false;
				tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
				if (!CurrentLinePtr)
				{
					MMPrintString("USB Touchscreen Connected on channel ");
					PInt(slot + 1);
					MMPrintString(" (");
					PInt(tinfo.pointer_x_logical_max);
					MMPrintString("x");
					PInt(tinfo.pointer_y_logical_max);
					MMPrintString(")\r\n> ");
				}
				PlayMemWav(ezyZip_wav, EZYZIP_WAV_SIZE);
				Current_USB_devices++;
				return;
			}
		}
		//		hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
		// Sony DualShock 4 [CUH-ZCT2x]
		if (is_sony_ds4(dev_addr))
		{
			if (!CurrentLinePtr)
			{
				MMPrintString("PS4 Controller Connected on channel ");
				PInt(slot + 1);
				MMPrintString("\r\n> ");
			}
			HID[slot].Device_address = dev_addr;
			HID[slot].Device_instance = instance;
			HID[slot].Device_type = PS4;
			HID[slot].report_rate = 20; // mSec between reports
			HID[slot].report_timer = -(10 + (slot + 2) * 500);
			HID[slot].active = true;
			HID[slot].report_requested = false;
			HID[slot].motorleft = 0;
			HID[slot].motorright = 0;
		}
		else if (is_sony_ds3(dev_addr))
		{
			if (!CurrentLinePtr)
			{
				MMPrintString("PS3 Controller Connected on channel ");
				PInt(slot + 1);
				MMPrintString("\r\n> ");
			}
			HID[slot].Device_address = dev_addr;
			HID[slot].Device_instance = instance;
			HID[slot].Device_type = PS3;
			HID[slot].report_rate = 20; // mSec between reports
			HID[slot].report_timer = -(10 + (slot + 2) * 500);
			HID[slot].active = true;
			HID[slot].report_requested = false;
		}
		else if (is_xbox(dev_addr))
		{
			if (!CurrentLinePtr)
			{
				MMPrintString("XBox Controller Connected on channel ");
				PInt(slot + 1);
				MMPrintString(" (pid=&H");
				PIntH(pid);
				MMPrintString(", vid=&H");
				PIntH(vid);
				MMPrintString(")");
				MMPrintString("\r\n> ");
			}
			HID[slot].Device_address = dev_addr;
			HID[slot].Device_instance = instance;
			HID[slot].Device_type = XBOX;
			HID[slot].report_rate = 20; // mSec between reports
			HID[slot].report_timer = -(10 + (slot + 2) * 500);
			HID[slot].active = true;
			HID[slot].report_requested = false;
		}
		else if (is_generic(dev_addr))
		{
			if (!CurrentLinePtr || monitor)
			{
				MMPrintString("Generic Gamepad Connected on channel ");
				PInt(slot + 1);
			}
			if (!CurrentLinePtr)
				MMPrintString("\r\n> ");
			else
				PRet();
			HID[slot].Device_address = dev_addr;
			HID[slot].Device_instance = instance;
			HID[slot].report_timer = -(10 + (slot + 2) * 500);
			HID[slot].active = false;
			HID[slot].report_rate = 20; // mSec between reports
			HID[slot].Device_type = SNES;
			HID[slot].active = true;
			HID[slot].report_requested = false;
		}
		else
		{
			/* Unrecognised interface (e.g. a composite touch monitor's
			   vendor interface). We never service it, so skip it entirely:
			   don't claim its slot, don't count it toward the device limit,
			   don't play the connect sound. This keeps slots free for the
			   keyboard / mouse / digitizer that follow on the same device. */
			return;
		}
	}
	PlayMemWav(ezyZip_wav, EZYZIP_WAV_SIZE);
	Current_USB_devices++;
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	__dsb();
	int i;
	for (i = 0; i < 4; i++)
	{
		//		PInt(i);PIntHC(HID[i].Device_type);PRet();
		if (instance == HID[i].Device_instance && dev_addr == HID[i].Device_address && HID[i].Device_type == HID_ITF_PROTOCOL_KEYBOARD)
		{
			if (!CurrentLinePtr)
				MMPrintString("USB Keyboard Disconnected\r\n> ");
			break;
		}
		else if (instance == HID[i].Device_instance && dev_addr == HID[i].Device_address && HID[i].Device_type == HID_ITF_PROTOCOL_MOUSE)
		{
			if (!CurrentLinePtr)
				MMPrintString("USB Mouse Disconnected\r\n> ");
			break;
		}
		else if (instance == HID[i].Device_instance && dev_addr == HID[i].Device_address && HID[i].Device_type == PS4)
		{
			if (!CurrentLinePtr)
				MMPrintString("PS4 Controller Disconnected\r\n> ");
			break;
		}
		else if (instance == HID[i].Device_instance && dev_addr == HID[i].Device_address && HID[i].Device_type == PS3)
		{
			if (!CurrentLinePtr)
				MMPrintString("PS3 Controller Disconnected\r\n> ");
			break;
		}
		else if (instance == HID[i].Device_instance && dev_addr == HID[i].Device_address && HID[i].Device_type == XBOX)
		{
			if (!CurrentLinePtr)
				MMPrintString("XBox Controller Disconnected\r\n> ");
			break;
		}
		else if (instance == HID[i].Device_instance && dev_addr == HID[i].Device_address && HID[i].Device_type == SNES)
		{
			if (!CurrentLinePtr)
				MMPrintString("Generic Gamepad Disconnected\r\n> ");
			break;
		}
		else if (instance == HID[i].Device_instance && dev_addr == HID[i].Device_address && HID[i].Device_type == TOUCH)
		{
			usb_touch_present = false;
			usb_touch_active = false;
			usb_touch_active2 = false;
			if (!CurrentLinePtr)
				MMPrintString("Multi-touch Disconnected\r\n> ");
			break;
		}
		else if (instance == HID[i].Device_instance && dev_addr == HID[i].Device_address && HID[i].Device_type == TOUCHMOUSE)
		{
			usb_touch_present = false;
			usb_touch_active = false;
			usb_touch_active2 = false;
			if (!CurrentLinePtr)
				MMPrintString("USB Touchscreen Disconnected\r\n> ");
			break;
		}
		else if (instance == HID[i].Device_instance && dev_addr == HID[i].Device_address && HID[i].Device_type == UNKNOWN)
		{
			if (!CurrentLinePtr)
				MMPrintString("Unknown Device Disconnected\r\n> ");
			break;
		}
	}
	if (i < 4)
	{
		PlayMemWav(remove_wav, REMOVE_WAV_SIZE);
		memset((void *)&HID[i], 0, sizeof(struct s_HID));
		HID[i].report_requested = true;
		Current_USB_devices--;
	}
	//  sprintf(buff,"HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
}
// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
	__dsb();
	int n = -1;
	uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
	for (int i = 0; i < 4; i++)
		if (instance == HID[i].Device_instance && dev_addr == HID[i].Device_address)
		{
			n = i;
			break;
		}
	if (n < 0)
		return; // no matching device found, discard report
	memcpy((void *)&HID[n].report[1], report, (len > 64 ? 64 : len));
	HID[n].report[0] = (len > 64 ? 64 : len);
	switch (itf_protocol)
	{
	case HID_ITF_PROTOCOL_KEYBOARD:
		//		MMPrintString("HID receive boot keyboard report\r\n");
		process_kbd_report((hid_keyboard_report_t const *)report, n);
		break;

	case HID_ITF_PROTOCOL_MOUSE:
		process_mouse_report((hid_mouse_report_t const *)report, n + 1);
		break;

	default:
		//		MMPrintString("HID receive boot gamepad report\r\n");
		if (HID[n].Device_type == TOUCH || HID[n].Device_type == TOUCHMOUSE)
		{
			/* Reassembles fragmented reports, then decodes + dumps the
			   complete report (the host hands up one 64-byte packet per
			   callback, so 84-byte reports arrive as 64 + tail). */
			touch_reassemble(dev_addr, instance, report, len,
							 (const touch_info_t *)&HID[n].touch_info,
							 (touch_report_t *)&HID[n].touch_report);
			/* Dump the RAW callback (actual length + full bytes) so framing
			   vs content problems are visible; decode result reflects this
			   report for the normal one-report-per-callback case. */
			dump_touch_report((const touch_info_t *)&HID[n].touch_info, report, len,
							  (const touch_report_t *)&HID[n].touch_report);
		}
		else if (is_sony_ds4(dev_addr))
		{
			process_sony_ds4(report, len, n + 1);
		}
		else if (is_sony_ds3(dev_addr))
		{
			process_sony_ds3(report, len, n + 1);
		}
		else if (is_xbox(dev_addr))
		{
			process_xbox(report, len, n + 1);
			/*		} else if ( is_specific(dev_addr) ){
						process_specific_gamepad(report, len, n+1);*/
		}
		else
		{
			process_generic_gamepad(report, len, n + 1);
		}
	}
	HID[n].report_requested = false;
	HID[n].report_timer = 0;
}

//--------------------------------------------------------------------+
// Keyboard
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// Mouse
//--------------------------------------------------------------------+

void cursor_movement(int8_t x, int8_t y, int8_t wheel)
{
#if USE_ANSI_ESCAPE
	// Move X using ansi escape
	if (x < 0)
	{
		sprintf(buff, ANSI_CURSOR_BACKWARD(% d), (-x)); // move left
	}
	else if (x > 0)
	{
		sprintf(buff, ANSI_CURSOR_FORWARD(% d), x); // move right
	}

	// Move Y using ansi escape
	if (y < 0)
	{
		sprintf(buff, ANSI_CURSOR_UP(% d), (-y)); // move up
	}
	else if (y > 0)
	{
		sprintf(buff, ANSI_CURSOR_DOWN(% d), y); // move down
	}

	// Scroll using ansi escape
	if (wheel < 0)
	{
		sprintf(buff, ANSI_SCROLL_UP(% d), (-wheel)); // scroll up
	}
	else if (wheel > 0)
	{
		sprintf(buff, ANSI_SCROLL_DOWN(% d), wheel); // scroll down
	}

	sprintf(buff, "\r\n");
#else
	char buff[STRINGSIZE];
	sprintf(buff, "(%d %d %d)\r\n", x, y, wheel);
#endif
}

static void process_mouse_report(hid_mouse_report_t const *report, uint8_t n)
{
	/* Double-click tracking moved into process_mouse_input(). */
	// Skip report ID if present
	if (HID[n - 1].mouse_info.uses_report_id)
	{
		uint8_t *p = (uint8_t *)report;
		p++; // Skip the report ID byte
		report = (hid_mouse_report_t *)p;
	}
	int16_t x_delta, y_delta;
	int8_t wheel_delta;
	uint8_t buttons;

	// Use the detected mouse type
	switch (HID[n - 1].mouse_type)
	{
	case MOUSE_TYPE_STANDARD_8BIT:
		// Standard 4-byte mouse
		buttons = report->buttons;
		x_delta = (int16_t)((float)report->x / (Option.mousespeed == 0.0f ? 1.0f : Option.mousespeed));
		y_delta = (int16_t)((float)report->y / (Option.mousespeed == 0.0f ? 1.0f : Option.mousespeed));
		wheel_delta = report->wheel;
		break;

	case MOUSE_TYPE_HIGHRES_12BIT:
		// Your 5-byte mouse with 12-bit X/Y
		{
			hid_mouse_report_12bit_t const *r = (hid_mouse_report_12bit_t const *)report;
			buttons = r->buttons;

			int16_t x_12 = r->data[0] | ((r->data[1] & 0x0F) << 8);
			if (x_12 & 0x0800)
				x_12 |= 0xF000;
			x_delta = (int16_t)((float)x_12 / Option.mousespeed);
			int16_t y_12 = ((r->data[1] & 0xF0) >> 4) | (r->data[2] << 4);
			if (y_12 & 0x0800)
				y_12 |= 0xF000;
			y_delta = (int16_t)((float)y_12 / Option.mousespeed);
			wheel_delta = r->wheel;
		}
		break;

	case MOUSE_TYPE_GAMING_16BIT:
		// Gaming mouse with 16-bit X/Y
		{
			hid_gaming_mouse_report_t const *r = (hid_gaming_mouse_report_t const *)report;
			buttons = r->buttons & 0xFF;
			x_delta = (int16_t)((float)(r->data[0] | (r->data[1] << 8)) / Option.mousespeed); // Scale down
			y_delta = (int16_t)((float)(r->data[2] | (r->data[3] << 8)) / Option.mousespeed); // Scale down
			wheel_delta = r->wheel;
		}
		break;

	default:
		return; // Unknown type
	}
	/* Hand off to the shared post-decode helper in KeyboardMap.c --
	   identical body of work for the BLE-HID-host build, just with a
	   different source of x/y/buttons. */
	process_mouse_input(x_delta, y_delta, wheel_delta, buttons, n);
}

//------------- cursor movement -------------//
//  cursor_movement(report->x, report->y, report->wheel);
//}
/*  @endcond */

void cmd_gamepad(void)
{
	unsigned char *tp = NULL;
	int n;
	if ((tp = checkstring(cmdline, (unsigned char *)"INTERRUPT ENABLE")))
	{
		getcsargs(&tp, 5);
		if (!(argc == 3 || argc == 5))
			SyntaxError();
		;
		n = getint(argv[0], 1, 4);
		nunInterruptc[n] = (char *)GetIntAddress(argv[2]); // get the interrupt location
		InterruptUsed = true;
		nunstruct[n].x1 = 0b1111111111111111;
		if (argc == 5)
			nunstruct[n].x1 = getint(argv[4], 0, 0b1111111111111111);
		return;
	}
	else if ((tp = checkstring(cmdline, (unsigned char *)"MONITOR SILENT")))
	{
		monitor = true;
		nooutput = true;
	}
	else if ((tp = checkstring(cmdline, (unsigned char *)"MONITOR")))
	{
		monitor = true;
	}
	else if ((tp = checkstring(cmdline, (unsigned char *)"CONFIGURE")))
	{
		getcsargs(&tp, 67);
		if (!(argc == 67))
			SyntaxError();
		;
		MyGamepad.vid = getint(argv[0], 0, 0xFFFF);
		MyGamepad.pid = getint(argv[2], 0, 0xFFFF);
		MyGamepad.b_R.index = getint(argv[4], 0, 255);
		MyGamepad.b_R.code = getint(argv[6], 0, 255);
		MyGamepad.b_START.index = getint(argv[8], 0, 255);
		MyGamepad.b_START.code = getint(argv[10], 0, 255);
		MyGamepad.b_HOME.index = getint(argv[12], 0, 255);
		MyGamepad.b_HOME.code = getint(argv[14], 0, 255);
		MyGamepad.b_SELECT.index = getint(argv[16], 0, 255);
		MyGamepad.b_SELECT.code = getint(argv[18], 0, 255);
		MyGamepad.b_L.index = getint(argv[20], 0, 255);
		MyGamepad.b_L.code = getint(argv[22], 0, 255);
		MyGamepad.b_DOWN.index = getint(argv[24], 0, 255);
		MyGamepad.b_DOWN.code = getint(argv[26], 0, 255);
		MyGamepad.b_RIGHT.index = getint(argv[28], 0, 255);
		MyGamepad.b_RIGHT.code = getint(argv[30], 0, 255);
		MyGamepad.b_UP.index = getint(argv[32], 0, 255);
		MyGamepad.b_UP.code = getint(argv[34], 0, 255);
		MyGamepad.b_LEFT.index = getint(argv[36], 0, 255);
		MyGamepad.b_LEFT.code = getint(argv[38], 0, 255);
		MyGamepad.b_R2.index = getint(argv[40], 0, 255);
		MyGamepad.b_R2.code = getint(argv[42], 0, 255);
		MyGamepad.b_X.index = getint(argv[44], 0, 255);
		MyGamepad.b_X.code = getint(argv[46], 0, 255);
		MyGamepad.b_A.index = getint(argv[48], 0, 255);
		MyGamepad.b_A.code = getint(argv[50], 0, 255);
		MyGamepad.b_Y.index = getint(argv[52], 0, 255);
		MyGamepad.b_Y.code = getint(argv[54], 0, 255);
		MyGamepad.b_B.index = getint(argv[56], 0, 255);
		MyGamepad.b_B.code = getint(argv[58], 0, 255);
		MyGamepad.b_L2.index = getint(argv[60], 0, 255);
		MyGamepad.b_L2.code = getint(argv[62], 0, 255);
		MyGamepad.b_TOUCH.index = getint(argv[64], 0, 255);
		MyGamepad.b_TOUCH.code = getint(argv[66], 0, 255);
	}
	else if ((tp = checkstring(cmdline, (unsigned char *)"HAPTIC")))
	{
		getcsargs(&tp, 5);
		if (!(argc == 5))
			SyntaxError();
		;
		n = getint(argv[0], 1, 4) - 1;
		if (HID[n].Device_type != PS4)
			error("PS4 only");
		HID[n].motorleft = getint(argv[2], 0, 255);
		HID[n].motorright = getint(argv[4], 0, 255);
	}
	else if ((tp = checkstring(cmdline, (unsigned char *)"COLOUR")))
	{
		getcsargs(&tp, 3);
		if (!(argc == 3))
			SyntaxError();
		;
		n = getint(argv[0], 1, 4) - 1;
		if (HID[n].Device_type != PS4)
			error("PS4 only");
		int colour = getint(argv[2], 0, 0xFFFFFF);
		HID[n].r = colour >> 16;
		HID[n].g = (colour >> 8) & 0xff;
		HID[n].b = colour & 0xff;
	}
	else if ((tp = checkstring(cmdline, (unsigned char *)"INTERRUPT DISABLE")))
	{
		getcsargs(&tp, 1);
		n = getint(argv[0], 1, 4);
		nunInterruptc[n] = NULL;
	}
	else
		SyntaxError();
	;
}
void cmd_mouse(void)
{
	unsigned char *tp = NULL;
	int n;
	if ((tp = checkstring(cmdline, (unsigned char *)"INTERRUPT ENABLE")))
	{
		getcsargs(&tp, 3);
		if (!(argc == 3))
			SyntaxError();
		;
		n = getint(argv[0], 1, 4);
		nunInterruptc[n] = (char *)GetIntAddress(argv[2]); // get the interrupt location
		InterruptUsed = true;
		return;
	}
	else if ((tp = checkstring(cmdline, (unsigned char *)"SET")))
	{
		getcsargs(&tp, 7);
		if (!(argc == 7 || argc == 5))
			SyntaxError();
		;
		n = getint(argv[0], 2, 2);
		nunstruct[n].ax = getint(argv[2], 0, HRes - 1);
		nunstruct[n].ay = getint(argv[4], 0, VRes - 1);
		if (argc == 7)
			nunstruct[n].az = getint(argv[6], -1000000, 1000000);
	}
	else if ((tp = checkstring(cmdline, (unsigned char *)"INTERRUPT DISABLE")))
	{
		getcsargs(&tp, 1);
		n = getint(argv[0], 1, 4);
		nunInterruptc[n] = NULL;
	}
	else
		SyntaxError();
	;
}
