#include "MMBasic.h"
#include "Hardware_Includes.h"
#include "i2ckbd.h"

static uint8_t i2c_inited = 0;
static int ctrlheld = 0;

static i2c_inst_t *vm_kbd_bus(void) {
    if (PinDef[Option.SYSTEM_I2C_SDA].mode & I2C0SDA) return i2c0;
    return i2c1;
}

static int vm_i2c_write_read_reg(uint8_t reg, uint16_t *out, int readback) {
    i2c_inst_t *bus;
    int retval;
    unsigned char msg[2];

    if (!i2c_inited) return -1;
    bus = vm_kbd_bus();
    msg[0] = reg;
    retval = i2c_write_timeout_us(bus, I2C_KBD_ADDR, msg, 1, false, 500000);
    if (retval == PICO_ERROR_GENERIC || retval == PICO_ERROR_TIMEOUT) return -1;
    if (!readback) return 0;
    sleep_us(1000);
    retval = i2c_read_timeout_us(bus, I2C_KBD_ADDR, (unsigned char *)out, 2, false, 500000);
    if (retval == PICO_ERROR_GENERIC || retval == PICO_ERROR_TIMEOUT) return -1;
    return 0;
}

void init_i2c_kbd() {
    i2c_inst_t *bus;
    uint gpio_scl, gpio_sda;

    if (!Option.SYSTEM_I2C_SDA || !Option.SYSTEM_I2C_SCL) return;
    gpio_scl = PinDef[Option.SYSTEM_I2C_SCL].GPno;
    gpio_sda = PinDef[Option.SYSTEM_I2C_SDA].GPno;
    bus = vm_kbd_bus();

    gpio_set_function(gpio_scl, GPIO_FUNC_I2C);
    gpio_set_function(gpio_sda, GPIO_FUNC_I2C);
    i2c_init(bus, (Option.SYSTEM_I2C_SLOW ? 10000 : 400000));
    gpio_pull_up(gpio_scl);
    gpio_pull_up(gpio_sda);
    i2c_inited = 1;
}

int read_i2c_kbd() {
    uint16_t buff = 0;
    int c = -1;

    if (vm_i2c_write_read_reg(9, &buff, 1) != 0) return -1;
    if (!buff) return -1;

    if (buff == 0xA503) ctrlheld = 0;
    else if (buff == 0xA502) {
        ctrlheld = 1;
    } else if ((buff & 0xff) == 1) {
        int realc;
        c = buff >> 8;
        switch (c) {
            case 0xd4: realc = DEL; break;
            case 0xb5: realc = UP; break;
            case 0xb6: realc = DOWN; break;
            case 0xb4: realc = LEFT; break;
            case 0xb7: realc = RIGHT; break;
            case 0xd1: realc = INSERT; break;
            case 0xd2: realc = HOME; break;
            case 0xd5: realc = END; break;
            case 0xd6: realc = PUP; break;
            case 0xd7: realc = PDOWN; break;
            case 0xa1: realc = ALT; break;
            case 0x81: realc = F1; break;
            case 0x82: realc = F2; break;
            case 0x83: realc = F3; break;
            case 0x84: realc = F4; break;
            case 0x85: realc = F5; break;
            case 0x86: realc = F6; break;
            case 0x87: realc = F7; break;
            case 0x88: realc = F8; break;
            case 0x89: realc = F9; break;
            case 0x90: realc = F10; break;
            case 0xd0: realc = BreakKey; break;
            case 0xb1: realc = ESC; break;
            case 0x0a: realc = ENTER; break;
            case 0x91: realc = 0x66; break;
            case 0xa2:
            case 0xa3:
            case 0xa5:
            case 0xc1:
                return -1;
            default:
                realc = c;
                break;
        }
        c = realc;
        if (c >= 'a' && c <= 'z' && ctrlheld) c = c - 'a' + 1;
        return c;
    }
    return -1;
}

int read_battery() {
    uint16_t buff = 0;
    if (vm_i2c_write_read_reg(0x0b, &buff, 1) != 0) return -1;
    return buff ? buff : -1;
}

int set_kbd_backlight(uint8_t val) {
    i2c_inst_t *bus;
    int retval;
    uint16_t buff = 0;
    unsigned char msg[2];

    if (!i2c_inited) return -1;
    bus = vm_kbd_bus();
    msg[0] = 0x0A;
    msg[1] = val;
    bitSet(msg[0], 7);

    retval = i2c_write_timeout_us(bus, I2C_KBD_ADDR, msg, 2, false, 500000);
    if (retval == PICO_ERROR_GENERIC || retval == PICO_ERROR_TIMEOUT) return -1;
    sleep_us(1000);
    retval = i2c_read_timeout_us(bus, I2C_KBD_ADDR, (unsigned char *)&buff, 2, false, 500000);
    if (retval == PICO_ERROR_GENERIC || retval == PICO_ERROR_TIMEOUT) return -1;
    return buff ? buff : -1;
}
