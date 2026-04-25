/*
 * hal/hal_port_assert.h — guard against silent eval-to-zero of HAL_PORT_HAS_*.
 *
 * `#if HAL_PORT_HAS_FOO` evaluates to 0 when HAL_PORT_HAS_FOO is undefined,
 * which means a translation unit that forgets to pull in port_config.h
 * silently loses every gated block. Including this header at the top of any
 * TU that uses HAL_PORT_HAS_* in preprocessor directives turns the missing
 * include into a build error.
 *
 * port_config.h sets HAL_PORT_CONFIG_INCLUDED=1; if it isn't reachable here,
 * the #error fires.
 */
#ifndef HAL_PORT_ASSERT_H
#define HAL_PORT_ASSERT_H

#ifndef HAL_PORT_CONFIG_INCLUDED
#error "port_config.h must be included before any #if HAL_PORT_HAS_* directive. Add #include \"port_config.h\" (or include an umbrella header that pulls it in) above the directive."
#endif

#endif /* HAL_PORT_ASSERT_H */
