/*
 * Freenove FNK0104B FT6336U touch helper.
 */

#ifndef ESP32_FT6336U_TOUCH_H
#define ESP32_FT6336U_TOUCH_H

void esp32_ft6336u_touch_init(void);
int esp32_ft6336u_touch_is_ready(void);
int esp32_ft6336u_touch_read(int index, int * x, int * y);
int esp32_ft6336u_touch_read_raw_mapped(int index, int * x, int * y);
int esp32_ft6336u_touch_down(void);
void esp32_ft6336u_touch_set_default_calibration(void);
void esp32_ft6336u_touch_set_identity_calibration(void);

#endif /* ESP32_FT6336U_TOUCH_H */
