#ifndef ESP32_UDP_H
#define ESP32_UDP_H

#include <stdint.h>

int esp32_udp_interrupt_pending(void);
void esp32_udp_poll(void);
void esp32_udp_server_stop(void);
int esp32_udp_server_open(uint16_t port);
int esp32_udp_cmd(unsigned char * tp);

#endif /* ESP32_UDP_H */
