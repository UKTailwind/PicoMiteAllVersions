/*
 * ports/esp32_s3_metro/main/esp32_platform.h — force-included before all
 * MMBasic core sources for the ESP32-S3 build.
 *
 * Modeled on ports/host_native/host_platform.h. Defines the build target
 * tags MMBasic core checks, stubs the Pico SDK section/attribute macros
 * scattered through the device-port code paths (no-ops on Xtensa), and
 * provides the small set of typedefs the core expects to be available
 * before any MMBasic header is parsed.
 *
 * Pico SDK compatibility headers are temporary shims while the remaining
 * shared code is migrated to HAL. Core/shared code no longer needs
 * pico/stdlib.h; legacy hardware/ header shims still come from ports/host_native
 * until that path is retired.
 */

#ifndef MMBASIC_ESP32_PLATFORM_H
#define MMBASIC_ESP32_PLATFORM_H

#define PICOMITE        1
#define MMBASIC_HOST    1
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

/* GPS.h redeclares timegm/gmtime; rename to dodge newlib clashes (same
 * trick host_platform.h uses). */
#define timegm  mmbasic_timegm
#define gmtime  mmbasic_gmtime

#endif /* MMBASIC_ESP32_PLATFORM_H */
