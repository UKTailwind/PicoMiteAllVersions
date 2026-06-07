/*
 * hal_pin_esp32.c — real impl for hal/hal_pin.h on ESP32-S3.
 *
 * GPIO via driver/gpio.h, ADC via esp_adc/adc_oneshot.h. Most calls
 * map one-to-one with the IDF HAL; the BANK_* operations cover GPIO
 * 0..63 via the dedicated GPIO peripheral's bulk-set/clear registers.
 *
 * What's not covered (intentionally):
 *   - hal_pin_set_drive_mA: ESP32-S3 has fixed drive strength enums
 *     (5/10/20/40 mA) so we map mA → closest enum; finer control
 *     would need callers anyway.
 *   - hal_pin_set_slew_fast: no API on ESP32-S3 — slew is fixed.
 *   - hal_pin_irq_set_edge: skipped at this Phase; gpio_install_isr_service
 *     + gpio_isr_handler_add is straightforward but no caller exercises
 *     it on this port yet.
 */

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/hal_pin.h"

/* ---- mode + direction ---- */

void hal_pin_set_mode(uint32_t gpio, hal_pin_mode_t mode) {
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << gpio};
    switch (mode) {
    case HAL_PIN_MODE_DISABLED:
        cfg.mode = GPIO_MODE_DISABLE;
        cfg.pull_up_en = 0;
        cfg.pull_down_en = 0;
        break;
    case HAL_PIN_MODE_INPUT:
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = 0;
        cfg.pull_down_en = 0;
        break;
    case HAL_PIN_MODE_INPUT_PULLUP:
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = 1;
        cfg.pull_down_en = 0;
        break;
    case HAL_PIN_MODE_INPUT_PULLDOWN:
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = 0;
        cfg.pull_down_en = 1;
        break;
    case HAL_PIN_MODE_OUTPUT:
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = 0;
        cfg.pull_down_en = 0;
        break;
    case HAL_PIN_MODE_OPEN_DRAIN:
        cfg.mode = GPIO_MODE_OUTPUT_OD;
        cfg.pull_up_en = 0;
        cfg.pull_down_en = 0;
        break;
    case HAL_PIN_MODE_ANALOG:
        cfg.mode = GPIO_MODE_DISABLE;
        cfg.pull_up_en = 0;
        cfg.pull_down_en = 0;
        break;
    case HAL_PIN_MODE_PWM:
        /* PWM via LEDC needs ledc_channel_config — caller side. */
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = 0;
        cfg.pull_down_en = 0;
        break;
    default:
        cfg.mode = GPIO_MODE_DISABLE;
        cfg.pull_up_en = 0;
        cfg.pull_down_en = 0;
        break;
    }
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void hal_pin_set_dir(uint32_t gpio, hal_pin_dir_t dir) {
    gpio_set_direction((gpio_num_t)gpio,
                       dir == HAL_PIN_DIR_OUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
}

void hal_pin_set_pulls(uint32_t gpio, hal_pin_pull_t pull) {
    gpio_pull_mode_t m = GPIO_FLOATING;
    if (pull == HAL_PIN_PULL_UP) m = GPIO_PULLUP_ONLY;
    if (pull == HAL_PIN_PULL_DOWN) m = GPIO_PULLDOWN_ONLY;
    gpio_set_pull_mode((gpio_num_t)gpio, m);
}

void hal_pin_set_input_enabled(uint32_t gpio, bool enabled) {
    /* IDF gpio API doesn't expose input-enable separately from mode;
     * approximate by switching mode. Callers that need this knob
     * combine it with set_dir; this matches the device-side semantics. */
    if (enabled) gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_INPUT);
}

void hal_pin_pulldown_reset(int p) {
    (void)p; /* RP2040-specific dance */
}

void hal_pin_select_digital(uint32_t gpio) {
    gpio_reset_pin((gpio_num_t)gpio);
}

void hal_pin_set_function(uint32_t gpio, hal_pin_func_t func) {
    /* Most HAL_PIN_FUNC_* values map to peripheral muxing on ESP32-S3
     * via gpio_iomux_in / gpio_matrix_out. Callers supply specific
     * peripheral-bound pins explicitly via the IDF driver they're
     * using (SPI, I2C, etc.), so this can be a near-no-op for now. */
    (void)gpio;
    (void)func;
}

void hal_pin_set_drive_mA(uint32_t gpio, uint8_t mA) {
    /* ESP32-S3 supports 5 / 10 / 20 / 40 mA drive enums. Pick closest. */
    gpio_drive_cap_t cap = GPIO_DRIVE_CAP_2; /* 20 mA, default */
    if (mA <= 5)
        cap = GPIO_DRIVE_CAP_0;
    else if (mA <= 10)
        cap = GPIO_DRIVE_CAP_1;
    else if (mA <= 20)
        cap = GPIO_DRIVE_CAP_2;
    else
        cap = GPIO_DRIVE_CAP_3;
    gpio_set_drive_capability((gpio_num_t)gpio, cap);
}

void hal_pin_set_input_hysteresis(uint32_t gpio, bool enabled) {
    (void)gpio;
    (void)enabled;
}
void hal_pin_set_slew_fast(uint32_t gpio, bool fast) {
    (void)gpio;
    (void)fast;
}
void hal_pin_irq_set_edge(uint32_t gpio, uint32_t edge_mask, bool enabled) {
    (void)gpio;
    (void)edge_mask;
    (void)enabled; /* TODO: gpio_isr_handler */
}

void hal_pin_init_digital(uint32_t gpio) {
    gpio_reset_pin((gpio_num_t)gpio);
    gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_INPUT);
}

void hal_pin_deinit(uint32_t gpio) {
    gpio_reset_pin((gpio_num_t)gpio);
}

/* ---- digital read/write/toggle ---- */

bool hal_pin_read(uint32_t gpio) {
    return gpio_get_level((gpio_num_t)gpio) ? true : false;
}

void hal_pin_write(uint32_t gpio, bool high) {
    gpio_set_level((gpio_num_t)gpio, high ? 1 : 0);
}

void hal_pin_toggle(uint32_t gpio) {
    int v = gpio_get_level((gpio_num_t)gpio);
    gpio_set_level((gpio_num_t)gpio, v ? 0 : 1);
}

bool hal_pin_read_output_latch(uint32_t gpio) {
    return (hal_pin_bank_read_out_latch() & (1ULL << gpio)) != 0;
}

/* ---- bank ops ----
 * GPIO 0..31 -> GPIO_OUT_REG + GPIO_IN_REG
 * GPIO 32..63 -> GPIO_OUT1_REG + GPIO_IN1_REG
 * IDF doesn't provide a single 64-bit read, so we splice. */

#include "soc/gpio_struct.h"

uint64_t hal_pin_bank_read_all(void) {
    return ((uint64_t)GPIO.in1.val << 32) | (uint64_t)GPIO.in;
}

uint64_t hal_pin_bank_read_out_latch(void) {
    return ((uint64_t)GPIO.out1.val << 32) | (uint64_t)GPIO.out;
}

void hal_pin_bank_set_mask(uint64_t mask) {
    if (mask & 0xffffffffULL) GPIO.out_w1ts = (uint32_t)(mask & 0xffffffffULL);
    if (mask >> 32) GPIO.out1_w1ts.val = (uint32_t)(mask >> 32);
}

void hal_pin_bank_clr_mask(uint64_t mask) {
    if (mask & 0xffffffffULL) GPIO.out_w1tc = (uint32_t)(mask & 0xffffffffULL);
    if (mask >> 32) GPIO.out1_w1tc.val = (uint32_t)(mask >> 32);
}

void hal_pin_bank_xor_mask(uint64_t mask) {
    /* No atomic XOR register; do read-modify-write. Acceptable for the
     * single caller (External.c PinSetBit) since it's not in IRQ
     * context. */
    uint64_t cur = hal_pin_bank_read_out_latch();
    uint64_t want = cur ^ mask;
    /* Set bits that need to be 1, clear bits that need to be 0. */
    hal_pin_bank_set_mask(mask & want);
    hal_pin_bank_clr_mask(mask & ~want);
}

/* ---- ADC ---- */

static adc_oneshot_unit_handle_t s_adc1 = NULL;
static adc_oneshot_unit_handle_t s_adc2 = NULL;
static adc_unit_t s_current_unit = ADC_UNIT_1;
static int s_current_channel = -1;

static adc_oneshot_unit_handle_t * hal_pin_adc_unit_handle(adc_unit_t unit) {
    return unit == ADC_UNIT_2 ? &s_adc2 : &s_adc1;
}

static adc_oneshot_unit_handle_t hal_pin_adc_ensure_unit(adc_unit_t unit) {
    adc_oneshot_unit_handle_t * handle = hal_pin_adc_unit_handle(unit);
    if (!*handle) {
        adc_oneshot_unit_init_cfg_t cfg = {.unit_id = unit};
        adc_oneshot_new_unit(&cfg, handle);
    }
    return *handle;
}

void hal_pin_adc_init(void) {
    hal_pin_adc_ensure_unit(ADC_UNIT_1);
}

void hal_pin_adc_select(uint32_t adc_channel) {
    adc_unit_t unit = ADC_UNIT_1;
    if (adc_channel >= 10) {
        unit = ADC_UNIT_2;
        adc_channel -= 10;
    }
    adc_oneshot_unit_handle_t handle = hal_pin_adc_ensure_unit(unit);
    adc_oneshot_chan_cfg_t cfg = {
        .atten = ADC_ATTEN_DB_12, /* 0..3.3 V full scale */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(handle, (adc_channel_t)adc_channel, &cfg);
    s_current_unit = unit;
    s_current_channel = (int)adc_channel;
}

void hal_pin_adc_set_temp_sensor(bool enabled) {
    /* ESP32-S3 has a temperature sensor via temperature_sensor.h —
     * separate from ADC. Wire later if a caller needs it. */
    (void)enabled;
}

uint16_t hal_pin_adc_read(void) {
    adc_oneshot_unit_handle_t handle;
    if (s_current_channel < 0) return 0;
    handle = *hal_pin_adc_unit_handle(s_current_unit);
    if (!handle) return 0;
    int raw = 0;
    adc_oneshot_read(handle, (adc_channel_t)s_current_channel, &raw);
    return (uint16_t)(raw & 0xfff); /* 12-bit on ESP32-S3 */
}
