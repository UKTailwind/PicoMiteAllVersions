/*
 * drivers/main_init/main_init_stub.c — no-op port_main_launch_core1
 * for WEB-class ports (web, web_rp2350) that don't dedicate core1
 * to a scanout/merge worker. The CYW43 polled stack runs entirely on
 * core0 and the stdio console handles both serial + telnet, so there
 * is no second-core entry point on these builds.
 */

#include "hal/hal_main_init.h"

void port_main_launch_core1(void) { }

void port_video_validate_boot_options(void) { }

unsigned port_video_sys_clock_khz(unsigned cpu_khz) { return cpu_khz; }

void port_video_post_clock_init(void) { }
