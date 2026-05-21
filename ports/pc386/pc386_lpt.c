/*
 * ports/pc386/pc386_lpt.c — LPT1 parallel-port GPIO driver.
 *
 *   SETPIN n, DIN|DOUT|OFF     — configure direction
 *   PIN n, value               — drive output
 *   PIN(n)                     — read input/output value
 *
 * Pin numbering matches the parallel-port nibble layout exposed by the
 * vm_sys_pin layer (NBRPINS = number of LPT GPIOs available). MMBasic's
 * legacy "Ext" / PinSet[Bit] glue maps onto the same vm_sys_pin layer.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_pin.h"
#include "vm_sys_pin.h"

#ifdef PinRead
#undef PinRead
#endif

int pc386_parse_lpt_pin(unsigned char *arg) {
    unsigned char *p = arg;
    skipspace(p);
    if ((p[0] == 'G' || p[0] == 'g') && (p[1] == 'P' || p[1] == 'p'))
        p += 2;
    int pin = getinteger(p);
    if (pin < 1 || pin > NBRPINS) error("Invalid pin");
    return pin;
}

void cmd_pin(void) {
    int pin = pc386_parse_lpt_pin(cmdline);
    while (*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
    if (!*cmdline) error("Invalid syntax");
    cmdline++;
    if (!*cmdline) error("Invalid syntax");
    vm_sys_pin_write(pin, getinteger(cmdline));
}

void cmd_setpin(void) {
    int pin;
    int mode = -1;
    getargs(&cmdline, 5, (unsigned char *)",");
    if (argc % 2 == 0 || argc < 3) error("Argument count");
    pin = pc386_parse_lpt_pin(argv[0]);

    if (checkstring(argv[2], (unsigned char *)"OFF") || checkstring(argv[2], (unsigned char *)"0"))
        mode = VM_PIN_MODE_OFF;
    else if (checkstring(argv[2], (unsigned char *)"DIN"))
        mode = VM_PIN_MODE_DIN;
    else if (checkstring(argv[2], (unsigned char *)"DOUT"))
        mode = VM_PIN_MODE_DOUT;
    else
        error("Unsupported SETPIN mode");

    if (argc >= 5 && *argv[4]) error("Unsupported SETPIN option");
    vm_sys_pin_setpin(pin, mode, VM_PIN_OPT_NONE);
}

void fun_pin(void) {
    iret = vm_sys_pin_read(pc386_parse_lpt_pin(ep));
    targ = T_INT;
}

/* ------------------------------------------------------------------ */
/* Legacy "Ext" / PinSet[Bit] glue used by older MMBasic call sites.  */
/* All routed through the same vm_sys_pin layer.                       */
/* ------------------------------------------------------------------ */

int codemap(int pin) {
    if (pin < 1 || pin > NBRPINS) error("Invalid pin");
    return pin;
}

int codecheck(unsigned char *line) {
    if ((line[0] == 'G' || line[0] == 'g') && (line[1] == 'P' || line[1] == 'p'))
        return 0;
    return 4;
}

int IsInvalidPin(int pin) {
    return pin < 1 || pin > NBRPINS;
}

void ExtCfg(int pin, int cfg, int option) {
    if (IsInvalidPin(pin)) error("Invalid pin");
    if (option) error("Unsupported SETPIN option");
    switch (cfg) {
        case EXT_NOT_CONFIG: vm_sys_pin_setpin(pin, VM_PIN_MODE_OFF,  VM_PIN_OPT_NONE); break;
        case EXT_DIG_IN:     vm_sys_pin_setpin(pin, VM_PIN_MODE_DIN,  VM_PIN_OPT_NONE); break;
        case EXT_DIG_OUT:    vm_sys_pin_setpin(pin, VM_PIN_MODE_DOUT, VM_PIN_OPT_NONE); break;
        default: error("Unsupported SETPIN mode");
    }
    ExtCurrentConfig[pin] = cfg;
}

void ExtSet(int pin, int val) {
    vm_sys_pin_write(pin, val);
    if (!IsInvalidPin(pin) && ExtCurrentConfig[pin] == EXT_NOT_CONFIG)
        ExtCurrentConfig[pin] = EXT_DIG_OUT;
}

int64_t ExtInp(int pin) { return vm_sys_pin_read(pin); }

int PinRead(int pin) { return (int)ExtInp(pin); }
int GetPinBit(int pin) { return (int)ExtInp(pin); }

volatile unsigned int GetPinStatus(int pin) {
    if (IsInvalidPin(pin)) return 0;
    return (unsigned int)ExtCurrentConfig[pin];
}

void PinSetBit(int pin, unsigned int offset) {
    if (IsInvalidPin(pin)) error("Invalid pin");
    switch ((int)offset) {
        case LATSET:  ExtSet(pin, 1); break;
        case LATCLR:  ExtSet(pin, 0); break;
        case LATINV:  hal_pin_toggle((uint32_t)pin); break;
        case TRISSET: ExtCfg(pin, EXT_DIG_IN, 0); break;
        case TRISCLR: ExtCfg(pin, EXT_DIG_OUT, 0); break;
        case CNPUSET: case CNPUCLR: case CNPDSET: case CNPDCLR: break;
        default: error("Unsupported pin operation");
    }
}
