/*
 * ports/esp32_s3_metro/main/app_main.c
 *
 * Phase A: Adafruit Metro ESP32-S3 toolchain bring-up.
 * Blinks the onboard LED on GPIO13 at 1 Hz and prints a heartbeat
 * line over USB Serial/JTAG once per second. Confirms board,
 * toolchain, and console path before any MMBasic linkage.
 *
 * The console output goes to the chip's built-in USB Serial/JTAG
 * controller — same USB-C cable used to flash. No external probe.
 */

#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Adafruit Metro ESP32-S3 — D13 silkscreen maps to GPIO13.
 * Verify against PrettyPins / silkscreen if relocated in a future
 * Metro revision. */
#define LED_GPIO GPIO_NUM_13

void app_main(void) {
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    printf("\nMMBasic Anywhere — Metro ESP32-S3 phase-A bring-up\n");
    printf("Heartbeat starting on GPIO%d\n", LED_GPIO);

    int n = 0;
    while (1) {
        gpio_set_level(LED_GPIO, n & 1);
        printf("tick %d\n", n);
        fflush(stdout);
        n++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
