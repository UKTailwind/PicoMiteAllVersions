/*
 * Freenove FNK0104B FT6336U touch helper.
 */

#ifndef ESP32_FT6336U_TOUCH_H
#define ESP32_FT6336U_TOUCH_H

void esp32_ft6336u_touch_init(void);
int esp32_ft6336u_touch_is_ready(void);
int esp32_ft6336u_touch_read(int index, int * x, int * y);
int esp32_ft6336u_touch_down(void);

#endif /* ESP32_FT6336U_TOUCH_H */
