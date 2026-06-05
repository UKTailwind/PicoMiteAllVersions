/*
 * ports/pico_sdk_common/vm_sys_time_pico.c — device impl of the
 * vm_sys_time port hook. Reads readusclock() + MMBasic's
 * TimeOffsetToUptime mechanism so the VM's DATE$ / TIME$ match the
 * values the interpreter shows for the same clock.
 */

#include <time.h>
#include <stdint.h>

#include "hal/hal_calendar.h"
#include "vm_device_support.h"

extern int64_t TimeOffsetToUptime;

int port_vm_time_get_tm(struct tm * out) {
    uint64_t now_us = readusclock();
    time_t epoch = (time_t)(now_us / 1000000ULL + TimeOffsetToUptime);
    hal_calendar_epoch_to_tm(epoch, out);
    return 1;
}
