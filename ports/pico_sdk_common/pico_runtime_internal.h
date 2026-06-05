#ifndef PICO_RUNTIME_INTERNAL_H
#define PICO_RUNTIME_INTERNAL_H

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "pico/bootrom.h"
#include "pico/unique_id.h"

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/exception.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/pwm.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/pads_qspi.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/timer.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"

#ifndef rp2350
#include "hardware/structs/ssi.h"
#else
#include "hardware/dma.h"
#include "hardware/regs/powman.h"
#include "hardware/regs/sysinfo.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/xip_ctrl.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include "psram.h"
#endif

#include "configuration.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bytecode.h"
#include "runtime/runtime.h"

#include "hal/hal_display_merge.h"
#include "hal/hal_flash.h"
#include "hal/hal_gui_controls.h"
#include "hal/hal_heartbeat.h"
#include "hal/hal_i2c_keypad.h"
#include "hal/hal_keyboard.h"
#include "hal/hal_main_init.h"

#define MES_SIGNON "\r" HAL_PORT_DEVICE_NAME " MMBasic Anywhere v" VERSION "\r\n"

extern void start_i2s(int pio, int sm);
extern void ProcessWeb(int mode);
extern void TelnetPutC(int c, int flush);
extern int wifi_serial_telnet_configured(void);

extern int adc_clk_div;
extern char banner[64];
extern volatile unsigned int ClassicTimer;
extern volatile unsigned int KeyCheck;
extern volatile unsigned int NunchuckTimer;
extern volatile bool processtick;
extern uint32_t restart_reason;
extern const uint8_t * flash_libmemory;
extern const uint8_t * flash_target_contents;
extern unsigned int CFuncmSec;
extern void CallCFuncmSec(void);

extern repeating_timer_t timer;
bool timer_callback(repeating_timer_t * rt);
uint64_t __not_in_flash_func(uSecFunc)(uint64_t a);
void sigbus(void);
void updatebootcount(void);

#ifndef rp2350
void __no_inline_not_in_flash_func(modclock)(uint16_t speed);
#endif

extern void bc_crash_dump_if_any(void);

#endif
