# cmake/usb_role.cmake — single source of truth for the USB peripheral
# role on each port.
#
# RP2040/RP2350 USB controllers are mode-exclusive — the chip can be
# host OR device but not both, and the decision is compile-time. The
# project used to spread this decision across four places per port
# (HAL_PORT_KEYBOARD_USB_HOST macro, the keyboard source files in the
# target_sources call, tinyusb_host link deps, and Pico_enable_stdio_usb).
# It was easy to get inconsistent — a port could declare device mode
# in port_config.h, link the USB-host keyboard driver, and silently
# end up with a dead REPL on the resulting build. Boards have had to
# be reflashed via SWD to recover.
#
# usb_role(KEYBOARD | CDC) collapses all four into one call. Pick
# KEYBOARD if a USB keyboard plugs into the Pico's USB-A port (USB
# controller in host mode); pick CDC if the Pico's USB-A presents a
# CDC serial console to a host computer (USB controller in device
# mode). The latter is also the right choice when there is no USB
# keyboard and the BASIC REPL talks over USB-CDC and/or UART.
#
# Keyboard input on CDC ports comes from:
#   - the CDC pipe itself (always pumped — drivers/console_cdc/),
#   - the PS/2 matrix driver (always linked — drivers/ps2_matrix/), and
#   - whichever board-level keyboard the port adds on top
#     (e.g. drivers/i2c_picocalc_kbd/ for the PicoCalc).
#
# Whichever one has its pins / I²C address configured at runtime
# delivers keys; the others are quiescent.

function(usb_role role)
    set(_target PicoMite)

    if(role STREQUAL "KEYBOARD")
        target_sources(${_target} PRIVATE
            ${CMAKE_SOURCE_DIR}/drivers/usb_host_kbd/USBKeyboard.c
            ${CMAKE_SOURCE_DIR}/drivers/usb_host_kbd/hal_keyboard_usb.c
        )
        target_compile_definitions(${_target} PRIVATE HAL_PORT_KEYBOARD_USB_HOST=1)
        target_link_libraries(${_target} tinyusb_host tinyusb_board)
        target_include_directories(${_target} PRIVATE
            ${CMAKE_SOURCE_DIR}/usb_host_files
        )
        Pico_enable_stdio_usb(${_target} 0)

    elseif(role STREQUAL "CDC")
        target_sources(${_target} PRIVATE
            ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
            ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/hal_keyboard_ps2.c
            ${CMAKE_SOURCE_DIR}/drivers/console_cdc/console_cdc.c
            ${CMAKE_SOURCE_DIR}/drivers/ps2_mouse/mouse.c
        )
        target_compile_definitions(${_target} PRIVATE HAL_PORT_KEYBOARD_USB_HOST=0)
        Pico_enable_stdio_usb(${_target} 1)

    else()
        message(FATAL_ERROR "usb_role(): role must be KEYBOARD or CDC, got '${role}'")
    endif()
endfunction()
