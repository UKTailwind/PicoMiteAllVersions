/*
 * hal_time_esp32.c — real impl for hal/hal_time.h on ESP32-S3.
 *
 * `esp_timer_get_time()` returns int64_t microseconds since boot,
 * monotonic, identical semantics to hal_time_us_64()'s contract.
 *
 * `hal_time_sleep_us` uses esp_rom_delay_us for short waits (<1 ms,
 * busy-wait) and vTaskDelay for longer ones (yields the FreeRTOS
 * scheduler so other tasks make progress).
 */

#include <stdint.h>
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/hal_time.h"

uint64_t hal_time_us_64(void) {
    return (uint64_t)esp_timer_get_time();
}

void hal_time_sleep_us(uint32_t us) {
    if (us < 1000) {
        esp_rom_delay_us(us);
    } else {
        TickType_t ticks = pdMS_TO_TICKS((us + 999) / 1000);
        if (ticks == 0) ticks = 1;
        vTaskDelay(ticks);
    }
}

uint32_t hal_time_ms_tick(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void hal_time_slowdown_tick(void) {
    /* No simulator slowdown on device. */
}
