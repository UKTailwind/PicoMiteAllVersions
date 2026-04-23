/*
 * ports/pico_sdk_common/port_load_overrides.c — board-profile overrides
 * applied on every LoadOptions(). Reads from flash always lose to these
 * compile-time-fixed pin assignments.
 *
 * Currently only the PicoCalc profile has anything to set. PICOCALC is
 * the default board flag for every device CMake build (CMakeLists.txt:4-6
 * `if(NOT DEFINED PICOCALC) set(PICOCALC true)`); turning it off via
 * -DPICOCALC=false drops the overrides for non-PicoCalc PCBs. Host links
 * its own no-op stub from host_runtime.c.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void port_apply_load_overrides(void)
{
#ifdef PICOCALC
    Option.DISPLAY_TYPE = ST7796SP;
    Option.SYSTEM_CLK = 14;
    Option.SYSTEM_MOSI = 15;
    Option.SYSTEM_MISO = 16;
    Option.DISPLAY_BL = 0;  /* stm32 controls the backlight */
    Option.LCD_CD = 19;
    Option.LCD_CS = 17;
    Option.LCD_Reset = 20;
    Option.DISPLAY_ORIENTATION = PORTRAIT;
    Option.DISPLAY_CONSOLE = 1;
    Option.SerialConsole = 1;
    Option.SerialTX = 1;
    Option.SerialRX = 2;

    Option.CombinedCS = 0;
    Option.SD_CS = 22;
    Option.SD_CLK_PIN = 24;
    Option.SD_MOSI_PIN = 25;
    Option.SD_MISO_PIN = 21;

    Option.TOUCH_CS = 0;
    Option.TOUCH_IRQ = 0;

    Option.DefaultFC = GREEN;

    Option.AUDIO_L = 31;
    Option.AUDIO_R = 32;
    Option.AUDIO_SLICE = 5;

    Option.AUDIO_CLK_PIN = 0;
    Option.AUDIO_MOSI_PIN = 0;
    Option.AUDIO_DCS_PIN = 0;
    Option.AUDIO_DREQ_PIN = 0;
    Option.AUDIO_RESET_PIN = 0;

    Option.KeyboardConfig = CONFIG_I2C;
    Option.SYSTEM_I2C_SDA = 9;
    Option.SYSTEM_I2C_SCL = 10;
    Option.SYSTEM_I2C_SLOW = 1;  /* 10 kHz for PicoCalc */

    Option.DefaultFont = 0x01;

    Option.BGR = 1;
    Option.BackLightLevel = 20;  /* default 20, sync with i2c keyboard */
    Option.ColourCode = 1;
    strcpy((char *)Option.platform, "PicoCalc");
#endif
}
