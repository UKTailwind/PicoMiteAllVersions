#ifndef ESP32_TFTP_H
#define ESP32_TFTP_H

void esp32_tftp_poll(void);
void esp32_tftp_server_stop(void);
int esp32_tftp_server_open(void);
void esp32_tftp_close_session(void);

#endif /* ESP32_TFTP_H */
