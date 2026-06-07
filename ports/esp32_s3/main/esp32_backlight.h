#ifndef ESP32_BACKLIGHT_H
#define ESP32_BACKLIGHT_H

void esp32_backlight_init_default(void);
void esp32_backlight_set(int level, int frequency);
void cmd_backlight(void);

#endif /* ESP32_BACKLIGHT_H */
