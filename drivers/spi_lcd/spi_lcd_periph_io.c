/*
 * drivers/spi_lcd/spi_lcd_periph_io.c — boot-time pin reservation
 * for the non-VGA panel + touch peripherals (SSD1963 family bus,
 * SPI-LCD CD/CS/Reset, XPT2046 touch CS/IRQ/Click). Linked on the
 * four non-VGA SPI-LCD device ports (pico, pico_rp2350, web,
 * web_rp2350); VGA / HDMI / DVI-WiFi ports link the no-op stub
 * (spi_lcd_periph_io_stub.c).
 *
 * The body is the original `#else` branch of the InitReservedIO
 * `#if HAL_PORT_IS_VGA / #else` split that lived in
 * drivers/sd_spi/mmc_stm32.c. Each sub-block is internally
 * Option-gated so unused peripherals stay inert at runtime.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "SSD1963.h"
#include "Touch.h"
#include "hal/hal_periph_io.h"

extern int CurrentSPISpeed;
extern void dobacklight(void);
/* SET_SPI_CLK / HW0Clk / HW1Clk and SPI0locked / SPI1locked come in via
 * SPI-LCD.h and External.h, both pulled by Hardware_Includes.h. */

void hal_periph_reserve_io(void) {
    if (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL) {
        ExtCfg(SSD1963_DC_PIN, EXT_BOOT_RESERVED, 0);
        gpio_init(SSD1963_DC_GPPIN); gpio_put(SSD1963_DC_GPPIN, GPIO_PIN_SET); gpio_set_dir(SSD1963_DC_GPPIN, GPIO_OUT);
        if (Option.SSD_RESET != -1) {
            ExtCfg(SSD1963_RESET_PIN, EXT_BOOT_RESERVED, 0);
            gpio_init(SSD1963_RESET_GPPIN); gpio_put(SSD1963_RESET_GPPIN, GPIO_PIN_SET); gpio_set_dir(SSD1963_RESET_GPPIN, GPIO_OUT);
        }
        ExtCfg(SSD1963_WR_PIN, EXT_BOOT_RESERVED, 0);
        gpio_init(SSD1963_WR_GPPIN); gpio_put(SSD1963_WR_GPPIN, GPIO_PIN_SET); gpio_set_dir(SSD1963_WR_GPPIN, GPIO_OUT);
        ExtCfg(SSD1963_RD_PIN, EXT_BOOT_RESERVED, 0);
        gpio_init(SSD1963_RD_GPPIN); gpio_put(SSD1963_RD_GPPIN, GPIO_PIN_SET); gpio_set_dir(SSD1963_RD_GPPIN, GPIO_OUT);
        ExtCfg(SSD1963_DAT1, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT1); gpio_put(SSD1963_GPDAT1, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT1, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT1, true);
        ExtCfg(SSD1963_DAT2, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT2); gpio_put(SSD1963_GPDAT2, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT2, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT2, true);
        ExtCfg(SSD1963_DAT3, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT3); gpio_put(SSD1963_GPDAT3, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT3, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT3, true);
        ExtCfg(SSD1963_DAT4, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT4); gpio_put(SSD1963_GPDAT4, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT4, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT4, true);
        ExtCfg(SSD1963_DAT5, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT5); gpio_put(SSD1963_GPDAT5, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT5, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT5, true);
        ExtCfg(SSD1963_DAT6, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT6); gpio_put(SSD1963_GPDAT6, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT6, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT6, true);
        ExtCfg(SSD1963_DAT7, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT7); gpio_put(SSD1963_GPDAT7, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT7, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT7, true);
        ExtCfg(SSD1963_DAT8, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT8); gpio_put(SSD1963_GPDAT8, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT8, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT8, true);
        if (Option.DISPLAY_TYPE > SSD_PANEL_8) {
            ExtCfg(SSD1963_DAT9,  EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT9);  gpio_put(SSD1963_GPDAT9,  GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT9,  GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT9,  true);
            ExtCfg(SSD1963_DAT10, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT10); gpio_put(SSD1963_GPDAT10, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT10, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT10, true);
            ExtCfg(SSD1963_DAT11, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT11); gpio_put(SSD1963_GPDAT11, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT11, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT11, true);
            ExtCfg(SSD1963_DAT12, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT12); gpio_put(SSD1963_GPDAT12, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT12, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT12, true);
            ExtCfg(SSD1963_DAT13, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT13); gpio_put(SSD1963_GPDAT13, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT13, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT13, true);
            ExtCfg(SSD1963_DAT14, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT14); gpio_put(SSD1963_GPDAT14, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT14, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT14, true);
            ExtCfg(SSD1963_DAT15, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT15); gpio_put(SSD1963_GPDAT15, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT15, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT15, true);
            ExtCfg(SSD1963_DAT16, EXT_BOOT_RESERVED, 0); gpio_init(SSD1963_GPDAT16); gpio_put(SSD1963_GPDAT16, GPIO_PIN_SET); gpio_set_dir(SSD1963_GPDAT16, GPIO_OUT); gpio_set_input_enabled(SSD1963_GPDAT16, true);
        }
        dobacklight();
    }

    if (Option.LCD_CD) {
        ExtCfg(Option.LCD_CD, EXT_BOOT_RESERVED, 0);
        if (!(Option.DISPLAY_TYPE == ST7920)) ExtCfg(Option.LCD_CS, EXT_BOOT_RESERVED, 0);
        ExtCfg(Option.LCD_Reset, EXT_BOOT_RESERVED, 0);
        LCD_CD_PIN    = PinDef[Option.LCD_CD].GPno;
        LCD_CS_PIN    = PinDef[Option.LCD_CS].GPno;
        LCD_Reset_PIN = PinDef[Option.LCD_Reset].GPno;
        gpio_init(LCD_CD_PIN);
        gpio_put(LCD_CD_PIN, Option.DISPLAY_TYPE != ST7920 ? GPIO_PIN_SET : GPIO_PIN_RESET);
        gpio_set_dir(LCD_CD_PIN, GPIO_OUT);
        gpio_init(LCD_CS_PIN);
        gpio_set_drive_strength(LCD_CS_PIN, GPIO_DRIVE_STRENGTH_8MA);
        if (!(Option.DISPLAY_TYPE == ST7920)) {
            gpio_put(LCD_CS_PIN, GPIO_PIN_SET);
            gpio_set_dir(LCD_CS_PIN, GPIO_OUT);
        }
        gpio_init(LCD_Reset_PIN);
        gpio_put(LCD_Reset_PIN, GPIO_PIN_RESET);
        gpio_set_dir(LCD_Reset_PIN, GPIO_OUT);
        CurrentSPISpeed = NONE_SPI_SPEED;
        dobacklight();
    }

    if (Option.TOUCH_CS || Option.TOUCH_IRQ) {
        if (Option.TOUCH_CS) {
            ExtCfg(Option.TOUCH_CS, EXT_BOOT_RESERVED, 0);
            TOUCH_CS_PIN = PinDef[Option.TOUCH_CS].GPno;
            gpio_init(TOUCH_CS_PIN);
            gpio_set_drive_strength(TOUCH_CS_PIN, GPIO_DRIVE_STRENGTH_8MA);
            gpio_set_slew_rate(TOUCH_CS_PIN, GPIO_SLEW_RATE_SLOW);
            gpio_put(TOUCH_CS_PIN, GPIO_PIN_SET);
            if (Option.CombinedCS) gpio_set_dir(TOUCH_CS_PIN, GPIO_IN);
            else                   gpio_set_dir(TOUCH_CS_PIN, GPIO_OUT);
        }
        ExtCfg(Option.TOUCH_IRQ, EXT_BOOT_RESERVED, 0);
        TOUCH_IRQ_PIN = PinDef[Option.TOUCH_IRQ].GPno;
        gpio_init(TOUCH_IRQ_PIN);
        gpio_pull_up(TOUCH_IRQ_PIN);
        gpio_set_dir(TOUCH_IRQ_PIN, GPIO_IN);
        gpio_set_input_hysteresis_enabled(TOUCH_IRQ_PIN, true);
        if (Option.TOUCH_Click) {
            ExtCfg(Option.TOUCH_Click, EXT_BOOT_RESERVED, 0);
            TOUCH_Click_PIN = PinDef[Option.TOUCH_Click].GPno;
            gpio_init(TOUCH_Click_PIN);
            gpio_put(TOUCH_Click_PIN, GPIO_PIN_RESET);
            gpio_set_dir(TOUCH_Click_PIN, GPIO_OUT);
        }
    }

#ifdef rp2350
    /* Optional second-SPI bus dedicated to the LCD. Only the rp2350
     * PicoMite OPTION setter ("OPTION LCD SPI ...") accepts these pin
     * fields; on every other rp2350 port (web_rp2350 etc.) the OPTION
     * setter rejects it and Option.LCD_CLK stays 0 so the runtime
     * guard never fires. */
    if (Option.LCD_CLK && !(Option.LCD_CLK == Option.SYSTEM_CLK)) {
        LCD_CLK_PIN  = PinDef[Option.LCD_CLK].GPno;
        LCD_MOSI_PIN = PinDef[Option.LCD_MOSI].GPno;
        LCD_MISO_PIN = PinDef[Option.LCD_MISO].GPno;
        ExtCfg(Option.LCD_CLK,  EXT_BOOT_RESERVED, 0);
        ExtCfg(Option.LCD_MOSI, EXT_BOOT_RESERVED, 0);
        ExtCfg(Option.LCD_MISO, EXT_BOOT_RESERVED, 0);
        if (PinDef[Option.LCD_CLK].mode & SPI0SCK
            && PinDef[Option.LCD_MOSI].mode & SPI0TX
            && PinDef[Option.LCD_MISO].mode & SPI0RX) {
            SET_SPI_CLK = HW0Clk;
            SPI0locked = 1;
        } else if (PinDef[Option.LCD_CLK].mode & SPI1SCK
                   && PinDef[Option.LCD_MOSI].mode & SPI1TX
                   && PinDef[Option.LCD_MISO].mode & SPI1RX) {
            SET_SPI_CLK = HW1Clk;
            SPI1locked = 1;
        }
        gpio_init(LCD_CLK_PIN);
        gpio_set_drive_strength(LCD_CLK_PIN, GPIO_DRIVE_STRENGTH_8MA);
        gpio_put(LCD_CLK_PIN, GPIO_PIN_RESET);
        gpio_set_dir(LCD_CLK_PIN, GPIO_OUT);
        gpio_set_slew_rate(LCD_CLK_PIN, GPIO_SLEW_RATE_FAST);
        gpio_init(LCD_MOSI_PIN);
        gpio_set_drive_strength(LCD_MOSI_PIN, GPIO_DRIVE_STRENGTH_8MA);
        gpio_put(LCD_MOSI_PIN, GPIO_PIN_RESET);
        gpio_set_dir(LCD_MOSI_PIN, GPIO_OUT);
        gpio_set_slew_rate(LCD_MOSI_PIN, GPIO_SLEW_RATE_FAST);
        gpio_init(LCD_MISO_PIN);
        gpio_set_pulls(LCD_MISO_PIN, true, false);
        gpio_set_dir(LCD_MISO_PIN, GPIO_IN);
        gpio_set_input_hysteresis_enabled(LCD_MISO_PIN, true);
    }
#endif
}
