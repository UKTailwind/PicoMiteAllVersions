#ifndef ESP32_TELNET_H
#define ESP32_TELNET_H

int esp32_telnet_open(void);
void esp32_telnet_close(void);
void esp32_telnet_close_session(void);
void esp32_telnet_putc(int c, int flush);
void esp32_telnet_poll(int mode);

#endif /* ESP32_TELNET_H */
