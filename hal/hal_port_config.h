/*
 * hal/hal_port_config.h — compile-time port constants.
 *
 * Absorbs hardware-constant differences between rp2040 and rp2350 into
 * named macros so core files can reference them without #ifdef rp2350.
 *
 * Each constant is defined once here, conditioned on the target macro.
 * Core files include this header (via MMBasic_Includes.h or directly)
 * and use the HAL_PORT_* names.
 */
#ifndef HAL_PORT_CONFIG_H
#define HAL_PORT_CONFIG_H

#ifdef rp2350
#define HAL_PORT_PWM_SLICE_COUNT   12
#define HAL_PORT_PWM_MAX_SLICE     11
#define HAL_PORT_GPIO_COUNT        48
#define HAL_PORT_PIO_COUNT         3
#define HAL_PORT_HAS_PIO2          1
#define HAL_PORT_HAS_FAST_TIMER    1
#define HAL_PORT_HAS_INT5          1
#define HAL_PORT_PULLDOWN_NEEDS_RESET 1
#else
#define HAL_PORT_PWM_SLICE_COUNT   8
#define HAL_PORT_PWM_MAX_SLICE     7
#define HAL_PORT_GPIO_COUNT        30
#define HAL_PORT_PIO_COUNT         2
#define HAL_PORT_HAS_PIO2          0
#define HAL_PORT_HAS_FAST_TIMER    0
#define HAL_PORT_HAS_INT5          0
#define HAL_PORT_PULLDOWN_NEEDS_RESET 0
#endif

/*
 * Flash placement: __not_in_flash_func forces a function body into RAM.
 * On VGA rp2040 and WEB rp2040 builds, flash-cache pressure is high so
 * we leave functions in flash.  All rp2350 builds and SPI-LCD rp2040
 * builds keep the hot GPIO paths in RAM.
 */
#if defined(PICOMITEWEB) || (defined(PICOMITEVGA) && !defined(rp2350))
#define HAL_PORT_RAM_FUNC(name) name
#else
#define HAL_PORT_RAM_FUNC(name) __not_in_flash_func(name)
#endif

#endif /* HAL_PORT_CONFIG_H */
