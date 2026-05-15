/*
 * ports/esp32_s3_metro/main/esp32_platform.h — force-included before all
 * MMBasic core sources for the ESP32-S3 build.
 *
 * Defines the ESP32 build target tag, stubs the Pico SDK section/attribute macros
 * scattered through the device-port code paths (no-ops on Xtensa), and
 * provides the small set of typedefs the core expects to be available
 * before any MMBasic header is parsed.
 *
 * Pico SDK compatibility headers are temporary neutral shims while the
 * remaining shared code is migrated to HAL. Core/shared code no longer needs
 * pico/stdlib.h.
 */

#ifndef MMBASIC_ESP32_PLATFORM_H
#define MMBASIC_ESP32_PLATFORM_H

#define PICOMITE        1
#define MMBASIC_ESP32   1

/* Pico SDK section attributes — no-ops on Xtensa. */
#define __not_in_flash_func(x) x
#define __not_in_flash(x)
#define __uninitialized_ram(x) x
#define __in_flash(x)
#define __scratch_x(x)
#define __scratch_y(x)

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifndef uint
typedef unsigned int uint;
#endif

#ifndef M_TWOPI
#define M_TWOPI (2.0 * 3.14159265358979323846)
#endif

#define PSRAMpin 0

/* FatFs volume parsing: MMBasic core constructs DOS-style paths like
 * "B:/foo" via hal_path_with_drive() before calling f_findfirst /
 * f_opendir. ffconf.h's default (FF_STR_VOLUME_ID=2 with VolumeStr
 * entries "B:","C:") only matches Unix-style "/B:/foo", so a DOS-style
 * path fails with FR_INVALID_DRIVE ("logical drive number is invalid")
 * — every FILES "B:" / OPEN "B:/..." call dies before touching the SD
 * card. Override to DOS-style (Mode 1) with bare "B","C" volume IDs:
 * the matcher reads the letter from VolumeStr[i], the colon from the
 * path, and the strings tt and tp meet exactly past the colon. See
 * ff.c::get_ldnumber for the matching logic. */
#define FF_STR_VOLUME_ID 1
#define FF_VOLUME_STRS   "B","C"

#endif /* MMBASIC_ESP32_PLATFORM_H */
