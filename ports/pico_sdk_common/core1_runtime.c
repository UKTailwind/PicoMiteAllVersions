/*
 * ports/pico_sdk_common/core1_runtime.c — single owner of core1stack[].
 *
 * core1stack[] is the SDK-managed core1 stack used by whichever subsystem
 * the active port runs on the second core: SPI-LCD merge pipeline
 * (display_merge_pico), VGA QVGA scanout (vga_qvga_modes::QVgaCore), HDMI
 * DVI scanout (hdmi_scanout::HDMICore). WEB never launches core1, but
 * still needs the canary word at core1stack[0] because MMBasic.c reads it
 * unconditionally to detect CPU2 stack overflow.
 *
 * Combining HDMI scanout with WiFi (a future port shape) used to fail at
 * link time because both consumers defined core1stack[]. Centralising the
 * definition here, sized per port via HAL_PORT_CORE1_STACK_WORDS, makes
 * any feature-flag combination link cleanly.
 */
#include <stdint.h>
#include "port_config.h"

uint32_t core1stack[HAL_PORT_CORE1_STACK_WORDS] = { [0] = 0x12345678 };
