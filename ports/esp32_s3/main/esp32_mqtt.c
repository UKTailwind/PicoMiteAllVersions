#include <string.h>

#include "hal/hal_net.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "shared/net/mm_net_mqtt_hal_cmd.h"
#include "shared/net/mm_net_state.h"
#include "esp32_mqtt.h"

extern volatile bool MQTTComplete;

static hal_net_mqtt_client_t s_mqtt_client;
static int s_mqtt_connected;

void esp32_mqtt_poll(void) {
    if (!s_mqtt_client) return;

    char topic[MAXSTRLEN + 1];
    char payload[MAXSTRLEN + 1];
    size_t payload_len = 0;
    int rc = hal_net_mqtt_recv_event(s_mqtt_client, topic, sizeof topic,
                                     payload, MAXSTRLEN, &payload_len);
    if (rc == HAL_NET_OK) {
        mm_net_state_set_mstring(MM_NET_STATE_TOPIC, topic, strlen(topic));
        mm_net_state_set_mstring(MM_NET_STATE_MESSAGE, payload, payload_len);
        MQTTComplete = true;
    }
}

static void esp32_mqtt_close(void) {
    if (!s_mqtt_client) return;
    hal_net_mqtt_close(s_mqtt_client);
    s_mqtt_client = 0;
    s_mqtt_connected = 0;
}

void closeMQTT(void) {
    esp32_mqtt_close();
}

int esp32_mqtt_cmd(unsigned char * line) {
    const mm_net_mqtt_hal_context_t ctx = {
        .client = &s_mqtt_client,
        .connected = &s_mqtt_connected,
        .client_id = NULL,
        .ensure_net = NULL,
        .after_subscribe = NULL,
    };
    return mm_net_mqtt_hal_cmd(line, &ctx);
}
