/*
 * shared/net/mm_net_mqtt_hal_cmd.c - MQTT BASIC commands over hal_net.
 */

#include <string.h>
#include <stdio.h>

#include "MMBasic_Includes.h"
#include "shared/net/mm_net_interrupts.h"
#include "shared/net/mm_net_mqtt_cmd.h"
#include "shared/net/mm_net_mqtt_hal_cmd.h"
#include "shared/net/mm_net_state.h"

extern bool optionsuppressstatus;
extern int InterruptUsed;

static int mqtt_is_connected(const mm_net_mqtt_hal_context_t *ctx) {
    return ctx->connected ? *ctx->connected : (*ctx->client != 0);
}

static void mqtt_set_connected(const mm_net_mqtt_hal_context_t *ctx, int value) {
    if (ctx->connected) *ctx->connected = value;
}

int mm_net_mqtt_hal_cmd(unsigned char *line,
                        const mm_net_mqtt_hal_context_t *ctx) {
    unsigned char *tp;

    if (!ctx || !ctx->client) error("Internal error");

    if ((tp = checkstring(line, (unsigned char *)"MQTT CONNECT"))) {
        mm_net_mqtt_connect_args_t parsed;
        mm_net_mqtt_parse_connect(tp, &parsed);
        if (mqtt_is_connected(ctx)) error("Already connected");

        if (parsed.has_interrupt) {
            MQTTInterrupt = parsed.interrupt;
            InterruptUsed = true;
        } else {
            MQTTInterrupt = NULL;
        }
        MQTTComplete = false;
        mm_net_state_clear_messages();

        if (ctx->ensure_net) ctx->ensure_net();
        if (!optionsuppressstatus) {
            char buff[STRINGSIZE];
            snprintf(buff, sizeof(buff), "Connecting to %s port %u\r\n",
                     parsed.host, (unsigned)parsed.port);
            MMPrintString(buff);
        }

        if (hal_net_mqtt_connect(parsed.host, (uint16_t)parsed.port,
                                 parsed.user, parsed.pass, ctx->client_id,
                                 5000, ctx->client) != HAL_NET_OK) {
            *ctx->client = 0;
            mqtt_set_connected(ctx, 0);
            error("Failed to connect");
        }
        mqtt_set_connected(ctx, 1);
        return 1;
    }

    if ((tp = checkstring(line, (unsigned char *)"MQTT PUBLISH"))) {
        if (!mqtt_is_connected(ctx)) error("No connection");
        mm_net_mqtt_publish_args_t parsed;
        mm_net_mqtt_parse_publish(tp, &parsed);
        if (hal_net_mqtt_publish(*ctx->client, parsed.topic, parsed.message,
                                 strlen(parsed.message), parsed.qos,
                                 parsed.retain) != HAL_NET_OK)
            error("Failed to publish");
        return 1;
    }

    if ((tp = checkstring(line, (unsigned char *)"MQTT SUBSCRIBE"))) {
        if (!mqtt_is_connected(ctx)) error("No connection");
        mm_net_mqtt_subscribe_args_t parsed;
        mm_net_mqtt_parse_subscribe(tp, &parsed);
        if (hal_net_mqtt_subscribe(*ctx->client, parsed.topic, parsed.qos,
                                   4000) != HAL_NET_OK)
            error("Failed to subscribe");
        if (ctx->after_subscribe) ctx->after_subscribe();
        return 1;
    }

    if ((tp = checkstring(line, (unsigned char *)"MQTT UNSUBSCRIBE"))) {
        if (!mqtt_is_connected(ctx)) error("No connection");
        mm_net_mqtt_topic_args_t parsed;
        mm_net_mqtt_parse_topic(tp, &parsed);
        if (hal_net_mqtt_unsubscribe(*ctx->client, parsed.topic, 4000) !=
            HAL_NET_OK)
            error("Failed to unsubscribe");
        return 1;
    }

    if ((tp = checkstring(line, (unsigned char *)"MQTT CLOSE"))) {
        if (*tp) error("Syntax");
        if (*ctx->client) hal_net_mqtt_close(*ctx->client);
        *ctx->client = 0;
        mqtt_set_connected(ctx, 0);
        return 1;
    }

    return 0;
}
