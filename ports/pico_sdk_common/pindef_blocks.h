/*
 * ports/pico_sdk_common/pindef_blocks.h — reusable per-port PinDef[]
 * row blocks. Each port's pin_tables.c composes its own PinDef[]
 * array literal from these macros.
 *
 * Why blocks instead of one shared table with `#if HAL_PORT_HAS_*`
 * gates: PinDef[] is port-shape data — each port has different pins
 * available depending on whether the HSTX (HDMI) peripheral or the
 * CYW43 radio claims certain GPIOs. Composing from blocks lets each
 * port own a flat unconditional array.
 */
#ifndef PINDEF_BLOCKS_H
#define PINDEF_BLOCKS_H

/* Header row + first 15 GPIO rows. Identical on every port (these
 * pins are user-accessible on every supported chip variant). */
#define PINDEF_BLOCK_HEADER_AND_GP0_15                                                        \
    {0, 99, "NULL", UNUSED, 99, 99},                                                          \
        {1, 0, "GP0", DIGITAL_IN | DIGITAL_OUT | SPI0RX | UART0TX | I2C0SDA | PWM0A, 99, 0},  \
        {2, 1, "GP1", DIGITAL_IN | DIGITAL_OUT | UART0RX | I2C0SCL | PWM0B, 99, 128},         \
        {3, 99, "GND", UNUSED, 99, 99},                                                       \
        {4, 2, "GP2", DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA | PWM1A, 99, 1},           \
        {5, 3, "GP3", DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL | PWM1B, 99, 129},          \
        {6, 4, "GP4", DIGITAL_IN | DIGITAL_OUT | SPI0RX | UART1TX | I2C0SDA | PWM2A, 99, 2},  \
        {7, 5, "GP5", DIGITAL_IN | DIGITAL_OUT | UART1RX | I2C0SCL | PWM2B, 99, 130},         \
        {8, 99, "GND", UNUSED, 99, 99},                                                       \
        {9, 6, "GP6", DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA | PWM3A, 99, 3},           \
        {10, 7, "GP7", DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL | PWM3B, 99, 131},         \
        {11, 8, "GP8", DIGITAL_IN | DIGITAL_OUT | SPI1RX | UART1TX | I2C0SDA | PWM4A, 99, 4}, \
        {12, 9, "GP9", DIGITAL_IN | DIGITAL_OUT | UART1RX | I2C0SCL | PWM4B, 99, 132},        \
        {13, 99, "GND", UNUSED, 99, 99},                                                      \
        {14, 10, "GP10", DIGITAL_IN | DIGITAL_OUT | SPI1SCK | I2C1SDA | PWM5A, 99, 5},        \
        {15, 11, "GP11", DIGITAL_IN | DIGITAL_OUT | SPI1TX | I2C1SCL | PWM5B, 99, 133}

/* Pins 16-25 — HDMI variant. The HSTX peripheral on rp2350 claims
 * GP12-GP19 for the DVI/HDMI signal pairs, so users can't OPTION
 * those GPIOs. */
#define PINDEF_BLOCK_PINS_16_25_HDMI      \
    {16, 12, "HDMI", UNUSED, 99, 99},     \
        {17, 13, "HDMI", UNUSED, 99, 99}, \
        {18, 99, "GND", UNUSED, 99, 99},  \
        {19, 14, "HDMI", UNUSED, 99, 99}, \
        {20, 15, "HDMI", UNUSED, 99, 99}, \
        {21, 16, "HDMI", UNUSED, 99, 99}, \
        {22, 17, "HDMI", UNUSED, 99, 99}, \
        {23, 99, "GND", UNUSED, 99, 99},  \
        {24, 18, "HDMI", UNUSED, 99, 99}, \
        {25, 19, "HDMI", UNUSED, 99, 99}

/* Pins 16-25 — non-HDMI variant. Users can OPTION GP12..GP19 freely. */
#define PINDEF_BLOCK_PINS_16_25_GENERIC                                                         \
    {16, 12, "GP12", DIGITAL_IN | DIGITAL_OUT | SPI1RX | UART0TX | I2C0SDA | PWM6A, 99, 6},     \
        {17, 13, "GP13", DIGITAL_IN | DIGITAL_OUT | UART0RX | I2C0SCL | PWM6B, 99, 134},        \
        {18, 99, "GND", UNUSED, 99, 99},                                                        \
        {19, 14, "GP14", DIGITAL_IN | DIGITAL_OUT | SPI1SCK | I2C1SDA | PWM7A, 99, 7},          \
        {20, 15, "GP15", DIGITAL_IN | DIGITAL_OUT | SPI1TX | I2C1SCL | PWM7B, 99, 135},         \
        {21, 16, "GP16", DIGITAL_IN | DIGITAL_OUT | SPI0RX | UART0TX | I2C0SDA | PWM0A, 99, 0}, \
        {22, 17, "GP17", DIGITAL_IN | DIGITAL_OUT | UART0RX | I2C0SCL | PWM0B, 99, 128},        \
        {23, 99, "GND", UNUSED, 99, 99},                                                        \
        {24, 18, "GP18", DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA | PWM1A, 99, 1},          \
        {25, 19, "GP19", DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL | PWM1B, 99, 129}

/* Pins 26-40 — common to every port. ADC pins 26/27/28 always
 * available. Pins 36-40 are board-level (GND/VSYS/VBUS/AGND/etc). */
#define PINDEF_BLOCK_PINS_26_40                                                                            \
    {26, 20, "GP20", DIGITAL_IN | DIGITAL_OUT | SPI0RX | UART1TX | I2C0SDA | PWM2A, 99, 2},                \
        {27, 21, "GP21", DIGITAL_IN | DIGITAL_OUT | UART1RX | I2C0SCL | PWM2B, 99, 130},                   \
        {28, 99, "GND", UNUSED, 99, 99},                                                                   \
        {29, 22, "GP22", DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA | PWM3A, 99, 3},                     \
        {30, 99, "RUN", UNUSED, 99, 99},                                                                   \
        {31, 26, "GP26", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | SPI1SCK | I2C1SDA | PWM5A, 0, 5},          \
        {32, 27, "GP27", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | SPI1TX | I2C1SCL | PWM5B, 1, 133},         \
        {33, 99, "AGND", UNUSED, 99, 99},                                                                  \
        {34, 28, "GP28", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | SPI1RX | UART0TX | I2C0SDA | PWM6A, 2, 6}, \
        {35, 99, "VREF", UNUSED, 99, 99},                                                                  \
        {36, 99, "3V3", UNUSED, 99, 99},                                                                   \
        {37, 99, "3V3E", UNUSED, 99, 99},                                                                  \
        {38, 99, "GND", UNUSED, 99, 99},                                                                   \
        {39, 99, "VSYS", UNUSED, 99, 99},                                                                  \
        {40, 99, "VBUS", UNUSED, 99, 99}

/* Pseudo pins 41-44 — GP23/24/25/29 user-accessible. Only on ports
 * without WiFi (the CYW43 radio claims those GPIOs on WiFi ports). */
#define PINDEF_BLOCK_PSEUDO_GP23_29                                                             \
    {41, 23, "GP23", DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL | PWM3B, 99, 131},             \
        {42, 24, "GP24", DIGITAL_IN | DIGITAL_OUT | SPI1RX | UART1TX | I2C0SDA | PWM4A, 99, 4}, \
        {43, 25, "GP25", DIGITAL_IN | DIGITAL_OUT | UART1RX | I2C0SCL | PWM4B, 99, 132},        \
        {44, 29, "GP29", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | UART0RX | I2C0SCL | PWM6B, 3, 134}

/* Pseudo pins 45-62 — rp2350-only extra GPIOs (GP30-GP47). Only on
 * rp2350 ports without WiFi. */
#define PINDEF_BLOCK_PSEUDO_RP2350_EXTRAS                                                                    \
    {45, 30, "GP30", DIGITAL_IN | DIGITAL_OUT | SPI1SCK | I2C1SDA | PWM7A, 99, 7},                           \
        {46, 31, "GP31", DIGITAL_IN | DIGITAL_OUT | SPI1TX | I2C1SCL | PWM7B, 99, 135},                      \
        {47, 32, "GP32", DIGITAL_IN | DIGITAL_OUT | UART0TX | SPI0RX | I2C0SDA | PWM8A, 99, 8},              \
        {48, 33, "GP33", DIGITAL_IN | DIGITAL_OUT | UART0RX | I2C0SCL | PWM8B, 99, 136},                     \
        {49, 34, "GP34", DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA | PWM9A, 99, 9},                       \
        {50, 35, "GP35", DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL | PWM9B, 99, 137},                      \
        {51, 36, "GP36", DIGITAL_IN | DIGITAL_OUT | UART1TX | SPI0RX | I2C0SDA | PWM10A, 99, 10},            \
        {52, 37, "GP37", DIGITAL_IN | DIGITAL_OUT | UART1RX | I2C0SCL | PWM10B, 99, 138},                    \
        {53, 38, "GP38", DIGITAL_IN | DIGITAL_OUT | SPI0SCK | I2C1SDA | PWM11A, 99, 11},                     \
        {54, 39, "GP39", DIGITAL_IN | DIGITAL_OUT | SPI0TX | I2C1SCL | PWM11B, 99, 139},                     \
        {55, 40, "GP40", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | UART1TX | SPI1RX | I2C0SDA | PWM8A, 0, 8},   \
        {56, 41, "GP41", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | UART1RX | I2C0SCL | PWM8B, 1, 136},          \
        {57, 42, "GP42", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | SPI1SCK | I2C1SDA | PWM9A, 2, 9},            \
        {58, 43, "GP43", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | SPI1TX | I2C1SCL | PWM9B, 3, 137},           \
        {59, 44, "GP44", DIGITAL_IN | DIGITAL_OUT | UART0TX | ANALOG_IN | SPI1RX | I2C0SDA | PWM10A, 4, 10}, \
        {60, 45, "GP45", DIGITAL_IN | DIGITAL_OUT | UART0RX | ANALOG_IN | I2C0SCL | PWM10B, 5, 138},         \
        {61, 46, "GP46", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | SPI1SCK | I2C1SDA | PWM11A, 6, 11},          \
        {62, 47, "GP47", DIGITAL_IN | DIGITAL_OUT | ANALOG_IN | SPI1TX | I2C1SCL | PWM11B, 7, 139}

#endif /* PINDEF_BLOCKS_H */
