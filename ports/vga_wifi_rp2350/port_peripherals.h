/*
 * ports/vga_wifi_rp2350/port_peripherals.h — VGA + WiFi rp2350. The
 * WiFi stack pulls in SSD1963 / Touch / GUI for the web-served LCD
 * shadow even though VGA scanout doesn't itself drive an SPI panel.
 */

#ifndef PORT_PERIPHERALS_H
#define PORT_PERIPHERALS_H

#include "SSD1963.h"
#include "Touch.h"
#include "GUI.h"

#endif /* PORT_PERIPHERALS_H */
