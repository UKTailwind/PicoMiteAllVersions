/*
 * drivers/ps2_matrix/hal_keyboard_ps2.c — hal_keyboard real impl for
 * PS/2 + I²C-keypad ports. Linked when HAL_PORT_HAS_USB_KEYBOARD=0.
 *
 * Drives the PS/2 matrix scan (Keyboard.c::CheckKeyboard) and the
 * I²C keypad polling (I2C.c::CheckI2CKeyboard, rate-limited via
 * KeyCheck and pumped from PicoMite.c::routinechecks).
 *
 * Read paths use LocalKeyDown[] (defined in External.c, populated
 * by ports/pico_rp2350/picocalc_keypad.c on the PicoCalc port; stays
 * zero on every other PS/2 port so the slot/count readers return 0).
 *
 * USB-host-keyboard ports link drivers/usb_host_kbd/hal_keyboard_usb.c
 * instead.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_keyboard.h"
#include "hal/hal_pin.h"
#include "PS2Keyboard.h"

extern void mouse0close(void);
extern int LocalKeyDown[7];

/* Console-input pump support: KeyCheck rate-limit counter +
 * KEYCHECKTIME reset value live in PicoMite.c. */
extern volatile unsigned int KeyCheck;
#ifndef KEYCHECKTIME
#define KEYCHECKTIME 16
#endif

void hal_keyboard_service(void) {
    if (Option.KeyboardConfig) {
        CheckKeyboard();
    }
}

void hal_keyboard_clear_repeat_state(void) {
    /* PS/2 / I²C backends do not run a software repeat state machine. */
}

void hal_keyboard_init(void) {
    initKeyboard();
}

int hal_keyboard_keydown_count(void) {
    int count = 0;
    for (int i = 0; i < 6; i++) if (LocalKeyDown[i]) count++;
    return count;
}

int hal_keyboard_keydown_slot(int slot) {
    if (slot < 1 || slot > 6) return 0;
    return LocalKeyDown[slot - 1];
}

uint32_t hal_keyboard_lock_state(void) {
    return 0u;       /* no lock-key state machine */
}

int hal_keyboard_set_layout(int layout) {
    switch (layout) {
        case HAL_KBD_LAYOUT_US:
        case HAL_KBD_LAYOUT_FR:
        case HAL_KBD_LAYOUT_GR:
        case HAL_KBD_LAYOUT_IT:
        case HAL_KBD_LAYOUT_BE:
        case HAL_KBD_LAYOUT_UK:
        case HAL_KBD_LAYOUT_ES:
        case HAL_KBD_LAYOUT_BR:
        case HAL_KBD_LAYOUT_I2C:
            Option.KeyboardConfig = layout;
            return 0;
        default:
            return -1;
    }
}

void hal_keyboard_quiesce_for_reset(void) {
    /* PS/2 has no async hardware to quiesce. */
}

int hal_keyboard_usb_raw_report(int slot, unsigned char *out, int max_len) {
    (void)slot; (void)out; (void)max_len;
    return 0;        /* no USB report to read */
}

void hal_keyboard_on_external_io_clear(void) {
    OnPS2GOSUB = NULL;
    PS2code = 0;
    PS2int = false;
    if (!Option.MOUSE_CLOCK) mouse0close();
}

void hal_keyboard_on_gpio_edge(uint32_t gpio) {
    uint64_t data = hal_pin_bank_read_all();
    if (Option.KEYBOARD_CLOCK) {
        if (!(Option.KeyboardConfig == NO_KEYBOARD || Option.KeyboardConfig == CONFIG_I2C) &&
            gpio == PinDef[Option.KEYBOARD_CLOCK].GPno)
            CNInterrupt(data);
    }
    if (MOUSE_CLOCK && gpio == PinDef[MOUSE_CLOCK].GPno) MNInterrupt(data);
}

#include "tusb.h"

/* MouseTimer (unsigned) declared extern in Hardware_Includes.h;
 * nunstruct[] declared in I2C.h (already pulled in by includes
 * above via MMBasic_Includes.h). */
void hal_keyboard_timer_tick(void) {
    nunstruct[2].type++;
    MouseTimer++;
}

void hal_console_usb_cdc_putc(char c, int flush) {
    /* PS/2 ports run USB-A in device-CDC mode; output goes there
     * when no other serial console is configured. */
    if (Option.SerialConsole == 0 || Option.SerialConsole > 4) {
        if (tud_cdc_connected()) {
            putc(c, stdout);
            if (flush) {
                fflush(stdout);
            }
        }
    }
}

void hal_keyboard_routinechecks_pump(void) {
    /* Drain USB-CDC stdio (the host-side USB-device serial) when the
     * board uses USB-CDC for stdin and Telnet isn't active. */
    int c;
    if (tud_cdc_connected() && (Option.SerialConsole == 0 || Option.SerialConsole > 4) && Option.Telnet != -1) {
        while ((c = tud_cdc_read_char()) != -1) {
            ConsoleRxBuf[ConsoleRxBufHead] = c;
            if (BreakKey && ConsoleRxBuf[ConsoleRxBufHead] == BreakKey) {
                MMAbort = true;
                ConsoleRxBufHead = ConsoleRxBufTail;
            } else if (ConsoleRxBuf[ConsoleRxBufHead] == keyselect && KeyInterrupt != NULL) {
                Keycomplete = true;
            } else {
                ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;
                if (ConsoleRxBufHead == ConsoleRxBufTail) {
                    ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;
                }
            }
        }
    }
    /* I²C keyboard polling — alternates between the two read phases
     * once per KEYCHECKTIME to avoid blocking on the I²C bus. */
    static int read = 0;
    if (Option.KeyboardConfig == CONFIG_I2C && KeyCheck == 0) {
        if (read == 0) {
            CheckI2CKeyboard(0, 0);
            read = 1;
        } else {
            CheckI2CKeyboard(0, 1);
            read = 0;
        }
        KeyCheck = KEYCHECKTIME;
    }
}
