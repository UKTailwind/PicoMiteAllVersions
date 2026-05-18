#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_net.h"
#include "shared/net/mm_net_interrupts.h"
#include "shared/net/mm_net_mqtt_hal_cmd.h"
#include "shared/net/mm_net_state.h"

static hal_net_mqtt_client_t pico_mqtt_client;
static int pico_mqtt_connected;

void pico_mqtt_poll(void) {
    if (!pico_mqtt_client) return;

    char topic[256];
    uint8_t payload[256];
    size_t payload_len = 0;
    int rc = hal_net_mqtt_recv_event(pico_mqtt_client, topic, sizeof(topic),
                                     payload, sizeof(payload), &payload_len);
    if (rc != HAL_NET_OK) return;

    mm_net_state_set_mstring(MM_NET_STATE_TOPIC, topic, strlen(topic));
    mm_net_state_set_mstring(MM_NET_STATE_MESSAGE, payload, payload_len);
    MQTTComplete = true;
}

void closeMQTT(void) {
    if (!pico_mqtt_client) return;
    hal_net_mqtt_close(pico_mqtt_client);
    pico_mqtt_client = 0;
    pico_mqtt_connected = 0;
}

int cmd_mqtt(void) {
    const mm_net_mqtt_hal_context_t ctx = {
        .client = &pico_mqtt_client,
        .connected = &pico_mqtt_connected,
        .client_id = NULL,
        .ensure_net = NULL,
        .after_subscribe = NULL,
    };
    return mm_net_mqtt_hal_cmd(cmdline, &ctx);
}
