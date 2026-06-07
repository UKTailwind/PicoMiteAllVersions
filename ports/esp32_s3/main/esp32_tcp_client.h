#ifndef ESP32_TCP_CLIENT_H
#define ESP32_TCP_CLIENT_H

void esp32_tcp_client_close(void);
int esp32_tcp_client_cmd(unsigned char * line);

#endif /* ESP32_TCP_CLIENT_H */
