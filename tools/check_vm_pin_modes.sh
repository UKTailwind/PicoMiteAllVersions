#!/usr/bin/env bash
# Guard VM pin-mode helper semantics that BASIC simulator tests do not cover.

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

src="$tmpdir/check_vm_pin_modes.c"
bin="$tmpdir/check_vm_pin_modes"

cat > "$src" <<'C'
#include <stdio.h>

#include "MMBasic_Includes.h"
#include "vm_sys_pin_internal.h"

static int fail(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    return 1;
}

int main(void)
{
    int slice = -1;
    int chan = -1;

    if (vm_pin_mode_is_pwm(VM_PIN_MODE_ARAW))
        return fail("VM_PIN_MODE_ARAW must not be classified as PWM");
    if (vm_pin_pwm_mode_to_slice_chan(VM_PIN_MODE_ARAW, &slice, &chan))
        return fail("VM_PIN_MODE_ARAW must not decode to a PWM slice/channel");
    if (!vm_pin_mode_is_pwm(VM_PIN_MODE_PWM0A) ||
        !vm_pin_mode_is_pwm(VM_PIN_MODE_PWM7B) ||
        !vm_pin_mode_is_pwm(VM_PIN_MODE_PWM8A) ||
        !vm_pin_mode_is_pwm(VM_PIN_MODE_PWM11B))
        return fail("PWM boundary modes must be classified as PWM");
    if (!vm_pin_pwm_mode_to_slice_chan(VM_PIN_MODE_PWM7B, &slice, &chan) ||
        slice != 7 || chan != 1)
        return fail("PWM7B must decode to slice 7 channel B");
    if (!vm_pin_pwm_mode_to_slice_chan(VM_PIN_MODE_PWM8A, &slice, &chan) ||
        slice != 8 || chan != 0)
        return fail("PWM8A must decode to slice 8 channel A");

    return 0;
}
C

cc=${CC:-cc}
"$cc" -std=gnu11 \
    -DMMBASIC_HOST -DFF_MAX_LFN_LARGE -DBC_SIM_RP2040 \
    -include "$root/ports/host_native/host_platform.h" \
    -I"$root/ports/host_native" -I"$root/ports/pico_sdk_compat" \
    -I"$root/core/mmbasic" \
    -I"$root/runtime/vm" \
    -I"$root/shared/gfx" \
    -I"$root/shared/audio" \
    -I"$root/shared/mmbasic" \
    -I"$root/drivers/bmp_decoder" \
    -I"$root/drivers/gps" \
    -I"$root/drivers/gui_controls" \
    -I"$root/drivers/gui_touch" \
    -I"$root/drivers/i2c_bus" \
    -I"$root/drivers/onewire" \
    -I"$root/drivers/psram_heap" \
    -I"$root/drivers/serial" \
    -I"$root/drivers/spi_bus" \
    -I"$root/drivers/spi_lcd" \
    -I"$root/drivers/ssd1963" \
    -I"$root/third_party/aes" \
    -I"$root/third_party/cjson" \
    -I"$root/third_party/dr_libs" \
    -I"$root/third_party/fatfs" \
    -I"$root/third_party/hxcmod" \
    -I"$root/third_party/littlefs" \
    -I"$root/third_party/picojpeg" \
    -I"$root/third_party/regex" \
    -I"$root/third_party/upng" \
    -I"$root" \
    "$src" -o "$bin"
"$bin"
