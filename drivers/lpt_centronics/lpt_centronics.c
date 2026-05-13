#include "drivers/lpt_centronics/lpt_centronics.h"

static uint16_t lpt_base = LPT_CENTRONICS_DEFAULT_BASE;
static uint8_t data_latch = 0x00;
static uint8_t control_latch = 0x0c; /* PC printer idle: INIT high, SELECTIN asserted */
static uint8_t pin_modes[18];
static bool initialized = false;

static inline void lpt_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t lpt_inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void lpt_io_wait(void) {
    lpt_outb(0x80, 0);
}

static void ensure_init(void) {
    if (!initialized) lpt_centronics_init(LPT_CENTRONICS_DEFAULT_BASE);
}

void lpt_centronics_init(uint16_t base) {
    lpt_base = base ? base : LPT_CENTRONICS_DEFAULT_BASE;
    data_latch = 0x00;
    control_latch = 0x0c;
    for (unsigned i = 0; i < sizeof(pin_modes); i++) pin_modes[i] = LPT_PIN_MODE_OFF;
    lpt_outb(lpt_base + 0, data_latch);
    lpt_outb(lpt_base + 2, control_latch);
    initialized = true;
}

uint16_t lpt_centronics_base(void) {
    ensure_init();
    return lpt_base;
}

static bool is_data_pin(uint32_t pin) {
    return pin >= 2 && pin <= 9;
}

static bool is_status_pin(uint32_t pin) {
    return pin == 10 || pin == 11 || pin == 12 || pin == 13 || pin == 15;
}

static bool is_control_pin(uint32_t pin) {
    return pin == 1 || pin == 14 || pin == 16 || pin == 17;
}

bool lpt_centronics_pin_valid(uint32_t pin) {
    return is_data_pin(pin) || is_status_pin(pin) || is_control_pin(pin);
}

bool lpt_centronics_pin_can_input(uint32_t pin) {
    return is_status_pin(pin);
}

bool lpt_centronics_pin_can_output(uint32_t pin) {
    return is_data_pin(pin) || is_control_pin(pin);
}

static uint8_t data_mask(uint32_t pin) {
    return (uint8_t)(1u << (pin - 2));
}

static uint8_t control_mask(uint32_t pin) {
    switch (pin) {
        case 1:  return 0x01;
        case 14: return 0x02;
        case 16: return 0x04;
        case 17: return 0x08;
        default: return 0x00;
    }
}

static bool control_pin_is_inverted(uint32_t pin) {
    return pin == 1 || pin == 14 || pin == 17;
}

static bool raw_control_to_logical(uint32_t pin, bool raw_high) {
    return control_pin_is_inverted(pin) ? !raw_high : raw_high;
}

static bool logical_to_raw_control(uint32_t pin, bool logical_high) {
    return control_pin_is_inverted(pin) ? !logical_high : logical_high;
}

bool lpt_centronics_pin_set_mode(uint32_t pin, lpt_pin_mode_t mode) {
    ensure_init();
    if (!lpt_centronics_pin_valid(pin)) return false;
    if (mode == LPT_PIN_MODE_INPUT && !lpt_centronics_pin_can_input(pin)) return false;
    if (mode == LPT_PIN_MODE_OUTPUT && !lpt_centronics_pin_can_output(pin)) return false;
    pin_modes[pin] = (uint8_t)mode;
    return true;
}

lpt_pin_mode_t lpt_centronics_pin_mode(uint32_t pin) {
    ensure_init();
    if (pin >= sizeof(pin_modes)) return LPT_PIN_MODE_OFF;
    return (lpt_pin_mode_t)pin_modes[pin];
}

bool lpt_centronics_pin_read_latch(uint32_t pin) {
    ensure_init();
    if (is_data_pin(pin)) return (data_latch & data_mask(pin)) != 0;
    if (is_control_pin(pin)) {
        uint8_t mask = control_mask(pin);
        return raw_control_to_logical(pin, (control_latch & mask) != 0);
    }
    return lpt_centronics_pin_read(pin);
}

bool lpt_centronics_pin_read(uint32_t pin) {
    ensure_init();
    if (is_data_pin(pin) || is_control_pin(pin)) return lpt_centronics_pin_read_latch(pin);
    uint8_t status = lpt_inb(lpt_base + 1);
    switch (pin) {
        case 10: return (status & 0x40) != 0; /* ACK */
        case 11: return (status & 0x80) == 0; /* BUSY is inverted by the PC port */
        case 12: return (status & 0x20) != 0; /* PAPER OUT */
        case 13: return (status & 0x10) != 0; /* SELECT */
        case 15: return (status & 0x08) != 0; /* ERROR */
        default: return false;
    }
}

bool lpt_centronics_pin_write(uint32_t pin, bool high) {
    ensure_init();
    if (!lpt_centronics_pin_can_output(pin)) return false;
    if (is_data_pin(pin)) {
        uint8_t mask = data_mask(pin);
        if (high) data_latch |= mask;
        else data_latch &= (uint8_t)~mask;
        lpt_outb(lpt_base + 0, data_latch);
        return true;
    }

    uint8_t mask = control_mask(pin);
    if (logical_to_raw_control(pin, high)) control_latch |= mask;
    else control_latch &= (uint8_t)~mask;
    lpt_outb(lpt_base + 2, control_latch);
    return true;
}

bool lpt_centronics_pin_toggle(uint32_t pin) {
    return lpt_centronics_pin_write(pin, !lpt_centronics_pin_read_latch(pin));
}

static bool wait_printer_ready(void) {
    ensure_init();
    for (unsigned i = 0; i < 100000; i++) {
        if (lpt_inb(lpt_base + 1) & 0x80) return true;
        lpt_io_wait();
    }
    return false;
}

bool lpt_centronics_write_byte(uint8_t byte) {
    ensure_init();
    if (!wait_printer_ready()) return false;

    data_latch = byte;
    lpt_outb(lpt_base + 0, data_latch);
    lpt_io_wait();

    (void)lpt_centronics_pin_write(1, false); /* STROBE active low */
    lpt_io_wait();
    lpt_io_wait();
    (void)lpt_centronics_pin_write(1, true);
    lpt_io_wait();
    return true;
}

size_t lpt_centronics_write(const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t written = 0;
    if (!p) return 0;
    while (written < len) {
        if (!lpt_centronics_write_byte(p[written])) break;
        written++;
    }
    return written;
}

void lpt_centronics_flush(void) {
    ensure_init();
    lpt_io_wait();
}
