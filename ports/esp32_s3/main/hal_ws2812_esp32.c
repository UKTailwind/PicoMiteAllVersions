/*
 * ESP32-S3 WS2812/SK6812 backend using RMT TX.
 */

#include <stddef.h>
#include <stdint.h>

#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/hal_ws2812.h"

#define WS2812_RMT_RESOLUTION_HZ 10000000u

typedef struct {
    uint16_t t0h;
    uint16_t t0l;
    uint16_t t1h;
    uint16_t t1l;
    uint16_t reset_us;
} ws2812_timing_t;

static uint16_t ws2812_ticks(uint32_t ns) {
    return (uint16_t)((ns * (WS2812_RMT_RESOLUTION_HZ / 1000000u) + 999u) / 1000u);
}

static ws2812_timing_t ws2812_timing_for(hal_ws2812_type_t type) {
    switch (type) {
    case HAL_WS2812_ORIGINAL:
        return (ws2812_timing_t){ws2812_ticks(350), ws2812_ticks(800),
                                 ws2812_ticks(700), ws2812_ticks(600), 50};
    case HAL_WS2812_SK6812:
    case HAL_WS2812_SK6812W:
        return (ws2812_timing_t){ws2812_ticks(300), ws2812_ticks(900),
                                 ws2812_ticks(600), ws2812_ticks(600), 80};
    case HAL_WS2812_B:
    default:
        return (ws2812_timing_t){ws2812_ticks(400), ws2812_ticks(850),
                                 ws2812_ticks(800), ws2812_ticks(450), 280};
    }
}

int hal_ws2812_write(uint32_t gpio, hal_ws2812_type_t type,
                     const uint8_t * wire_bytes, size_t wire_len) {
    ws2812_timing_t timing = ws2812_timing_for(type);
    rmt_channel_handle_t channel = NULL;
    rmt_encoder_handle_t encoder = NULL;
    rmt_tx_channel_config_t channel_config = {
        .gpio_num = (gpio_num_t)gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2812_RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    rmt_bytes_encoder_config_t encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = timing.t0h,
            .level1 = 0,
            .duration1 = timing.t0l,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = timing.t1h,
            .level1 = 0,
            .duration1 = timing.t1l,
        },
        .flags.msb_first = 1,
    };
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    esp_err_t err;

    if (!wire_bytes || wire_len == 0)
        return -1;

    err = rmt_new_tx_channel(&channel_config, &channel);
    if (err != ESP_OK)
        return -1;
    err = rmt_new_bytes_encoder(&encoder_config, &encoder);
    if (err == ESP_OK)
        err = rmt_enable(channel);
    if (err == ESP_OK)
        err = rmt_transmit(channel, encoder, wire_bytes, wire_len, &tx_config);
    if (err == ESP_OK)
        err = rmt_tx_wait_all_done(channel, pdMS_TO_TICKS(100));
    if (timing.reset_us)
        vTaskDelay(pdMS_TO_TICKS((timing.reset_us + 999u) / 1000u));

    if (channel)
        rmt_disable(channel);
    if (channel)
        rmt_del_channel(channel);
    if (encoder)
        rmt_del_encoder(encoder);

    return err == ESP_OK ? 0 : -1;
}
