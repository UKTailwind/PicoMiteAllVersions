# cmake/usb_role.cmake — single source of truth for the USB peripheral
# role on each port.
#
# RP2040/RP2350 USB controllers are mode-exclusive: the chip is host
# OR device, not both, and the decision is compile-time.
#
# usb_role(KEYBOARD | CDC) wires up everything that decision implies
# — sources, defines, link libs, stdio routing — in one call. Pick
# KEYBOARD when a USB keyboard plugs into the Pico's USB-A port (USB
# controller in host mode). Pick CDC when the USB-A port presents a
# CDC serial console to a host computer (USB controller in device
# mode); the BASIC REPL then talks over USB-CDC and/or UART.
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
