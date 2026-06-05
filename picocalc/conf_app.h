#ifndef CONF_APP_H
#define CONF_APP_H

#define SLAVE_ADDRESS 0x1F
#define FIFO_SIZE 31

#define INT_DURATION_MS 1

#ifndef CONFIG_PMU_SDA
#define CONFIG_PMU_SDA PB11
#endif

#ifndef CONFIG_PMU_SCL
#define CONFIG_PMU_SCL PB10
#endif

#ifndef CONFIG_PMU_IRQ
#define CONFIG_PMU_IRQ PC9
#endif

#define LOW_BAT_VAL 20
#define LCD_BACKLIGHT_STEP 10

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1 << (bit)))
#define bitClear(value, bit) ((value) &= ~(1 << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet((value), (bit)) : bitClear((value), (bit)))

#endif
