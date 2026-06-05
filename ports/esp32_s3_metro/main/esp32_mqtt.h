#ifndef ESP32_MQTT_H
#define ESP32_MQTT_H

void esp32_mqtt_poll(void);
int esp32_mqtt_cmd(unsigned char * line);
void closeMQTT(void);

#endif /* ESP32_MQTT_H */
