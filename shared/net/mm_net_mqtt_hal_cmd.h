/*
 * shared/net/mm_net_mqtt_hal_cmd.h - MQTT BASIC commands over hal_net.
 */

#ifndef MM_NET_MQTT_HAL_CMD_H
#define MM_NET_MQTT_HAL_CMD_H

#include "hal/hal_net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_net_mqtt_client_t * client;
    int * connected;
    const char * client_id;
    void (*ensure_net)(void);
    void (*after_subscribe)(void);
} mm_net_mqtt_hal_context_t;

int mm_net_mqtt_hal_cmd(unsigned char * line,
                        const mm_net_mqtt_hal_context_t * ctx);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_MQTT_HAL_CMD_H */
