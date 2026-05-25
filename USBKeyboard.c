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

/* process_kbd_report is declared in KeyboardMap.h. */

void USB_bus_reset(void)
{
	hw_set_bits(&usb_hw->phy_direct_override, USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS |
												  USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS);
	hw_set_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DM_OE_BITS | USB_USBPHY_DIRECT_TX_DP_OE_BITS);
	uint32_t save = usb_hw->phy_direct;
	hw_clear_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DM_BITS | USB_USBPHY_DIRECT_TX_DP_BITS);
	uSec(10000);
	usb_hw->phy_direct = save;
	hw_clear_bits(&usb_hw->phy_direct_override, USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS);
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
		if (HID[i].active == false || HID[i].report_requested)
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
		error("USB device limit reached");
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
		error("USB device limit reached");

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
			/*			MMPrintString("Unknown Device Connected on channel ");PInt(slot+1);
						MMPrintString(" (pid=&H");PIntH(pid);
						MMPrintString(", vid=&H");PIntH(vid);MMPrintString(")");
						MMPrintString("\r\n> ");
						HID[slot].Device_address = dev_addr;
						HID[slot].Device_instance = instance;
						HID[slot].report_timer=-(10+(slot+2)*500);
						HID[slot].active=false;
						HID[slot].report_rate=20; //mSec between reports
						HID[slot].Device_type=UNKNOWN;
						HID[slot].active=true;
						HID[slot].report_requested=false;*/
			/*			for(int i=0;i< desc_len;i+=2){
							putConsole('0',0);
							putConsole('x',0);
							PIntH(desc_report[i]);
							putConsole(' ',0);
							putConsole('0',0);
							putConsole('x',0);
							PIntH(desc_report[i+1]);
							PRet();
						}*/
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
		if (is_sony_ds4(dev_addr))
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
