/*
 * ports/pico_sdk_common/picocalc_features_real.c — real impls of the
 * port_picocalc_* hooks for the PicoCalc board. Linked when the
 * port's port_sources.cmake selects the keypad-MCU profile.
 *
 * MM_Misc.c calls these unconditionally so cmd_option / fun_mminfo /
 * fun_inkey paths stay preprocessor-clean.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "drivers/i2c_picocalc_kbd/i2ckbd.h"
#include "picocalc/conf_app.h"

void port_picocalc_set_keyboard_backlight(int level)
{
    Option.KEYBOARDBL = level;
    init_i2c_kbd();
    (void)set_kbd_backlight(level);
}

int port_picocalc_battery_pct(void)
{
    init_i2c_kbd();
    int b = read_battery() >> 8;
    bitClear(b, 7);
    return b;
}

int port_picocalc_is_charging(void)
{
    init_i2c_kbd();
    int b = read_battery() >> 8;
    int c = bitRead(b, 7);
    bitClear(b, 7);
    (void)b;
    return c;
}

void port_picocalc_factory_reset_options(void)
{
    ResetOptions(false);
    Option.ColourCode = 1;
    Option.SYSTEM_CLK = 14;
    Option.SYSTEM_MOSI = 15;
    Option.SYSTEM_MISO = 16;
    Option.SYSTEM_I2C_SDA = 9;
    Option.SYSTEM_I2C_SCL = 10;
    Option.SYSTEM_I2C_SLOW = 1;  /* 10 kHz for PicoCalc */
    Option.AUDIO_L = 31;
    Option.AUDIO_R = 32;
    Option.AUDIO_SLICE = 5;
    Option.AUDIO_CLK_PIN = 0;
    Option.AUDIO_MOSI_PIN = 0;
    Option.AUDIO_DCS_PIN = 0;
    Option.AUDIO_DREQ_PIN = 0;
    Option.AUDIO_RESET_PIN = 0;
    Option.DISPLAY_TYPE = ST7796SP;
    Option.DISPLAY_BL = 0;  /* stm32 controls the backlight */
    Option.DISPLAY_ORIENTATION = PORTRAIT;
    Option.LCD_CD = 19;
    Option.LCD_Reset = 20;
    Option.LCD_CS = 17;
    Option.BGR = 1;
    Option.BackLightLevel = 20;  /* default 20, sync with i2c keyboard */
    Option.TOUCH_CS = 0;
    Option.TOUCH_IRQ = 0;
    Option.DefaultFC = GREEN;
    Option.DefaultFont = 0x01;
    Option.ColourCode = 1;
    Option.KeyboardConfig = CONFIG_I2C;
    Option.CombinedCS = 0;
    Option.SD_CS = 22;
    Option.SD_CLK_PIN = 24;
    Option.SD_MOSI_PIN = 25;
    Option.SD_MISO_PIN = 21;
    Option.DISPLAY_CONSOLE = 1;
    Option.SerialConsole = 1;
    Option.SerialTX = 1;
    Option.SerialRX = 2;
    SaveOptions();
    printoptions();
    uSec(100000);
    _excep_code = RESET_COMMAND;
    SoftReset();
}
