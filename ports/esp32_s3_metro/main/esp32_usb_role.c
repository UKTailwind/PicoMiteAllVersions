/*
 * esp32_usb_role.c - persisted runtime USB role selection.
 *
 * OPTION USB SERIAL/KEYBOARD stores the desired role in Option.USBRole and
 * reboots. At boot the port initialises either USB Serial/JTAG or USB HID
 * host. Holding BOOT (GPIO0) during reset forces serial for that boot only.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"

static int s_effective_role = USB_ROLE_SERIAL;
static int s_forced_serial;

static int valid_role(int role) {
    return role == USB_ROLE_SERIAL || role == USB_ROLE_KEYBOARD;
}

static void select_usb_serial_jtag_phy(void) {
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
}

void esp32_usb_role_resolve_boot(void) {
    if (!valid_role(Option.USBRole)) Option.USBRole = USB_ROLE_SERIAL;

    const gpio_config_t boot_cfg = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&boot_cfg);
    vTaskDelay(pdMS_TO_TICKS(10));

    s_forced_serial = (gpio_get_level(GPIO_NUM_0) == 0);
    s_effective_role = s_forced_serial ? USB_ROLE_SERIAL : Option.USBRole;
    if (s_effective_role == USB_ROLE_SERIAL) select_usb_serial_jtag_phy();
}

int esp32_usb_role_is_serial(void) {
    return s_effective_role != USB_ROLE_KEYBOARD;
}

int esp32_usb_role_is_keyboard(void) {
    return s_effective_role == USB_ROLE_KEYBOARD;
}

void esp32_usb_role_print_options(void) {
    MMPrintString("OPTION USB ");
    MMPrintString(Option.USBRole == USB_ROLE_KEYBOARD ? "KEYBOARD" : "SERIAL");
    MMPrintString("\r\n");
}

int esp32_usb_role_option_setter(unsigned char * cmdline) {
    unsigned char * tp = checkstring(cmdline, (unsigned char *)"USB");
    if (!tp) return 0;
    if (CurrentLinePtr) error("Invalid in a program");

    if (checkstring(tp, (unsigned char *)"STATUS")) {
        extern void esp32_usb_keyboard_print_status(void);
        MMPrintString("Saved USB role: ");
        MMPrintString(Option.USBRole == USB_ROLE_KEYBOARD ? "KEYBOARD\r\n" : "SERIAL\r\n");
        MMPrintString("Effective USB role: ");
        MMPrintString(esp32_usb_role_is_keyboard() ? "KEYBOARD\r\n" : "SERIAL\r\n");
        if (s_forced_serial) MMPrintString("BOOT forced serial for this boot\r\n");
        esp32_usb_keyboard_print_status();
        return 1;
    }

    int role = -1;
    if (checkstring(tp, (unsigned char *)"SERIAL")) {
        role = USB_ROLE_SERIAL;
    } else if (checkstring(tp, (unsigned char *)"KEYBOARD")) {
        role = USB_ROLE_KEYBOARD;
    } else {
        error("Syntax");
    }

    Option.USBRole = (unsigned char)role;
    SaveOptions();
    if (role == USB_ROLE_KEYBOARD) {
        MMPrintString("Hold BOOT during reset to force USB SERIAL for one boot\r\n");
    } else {
        select_usb_serial_jtag_phy();
    }
    MMPrintString("Restarting\r\n");
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
    return 1;
}

int esp32_usb_role_forced_serial(void) {
    return s_forced_serial;
}

void esp32_usb_role_prepare_keyboard_host(void) {
    if (usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_uninstall();
    }
}

void esp32_usb_role_prepare_serial_device(void) {
    select_usb_serial_jtag_phy();
}
