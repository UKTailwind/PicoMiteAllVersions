#ifndef ESP32_TCP_SERVER_H
#define ESP32_TCP_SERVER_H

#include <stdint.h>

int esp32_tcp_interrupt_pending(void);
void esp32_tcp_server_poll(void);
void esp32_tcp_server_stop(void);
int esp32_tcp_server_open(uint16_t port);
void esp32_tcp_server_clear_requests(void);
int esp32_tcp_server_max_connections(void);
int esp32_tcp_server_request_pending(int pcb);
const char *esp32_tcp_server_path(int pcb);
int esp32_tcp_cmd(unsigned char *tp);
int esp32_transmit_cmd(unsigned char *tp);

#endif /* ESP32_TCP_SERVER_H */
