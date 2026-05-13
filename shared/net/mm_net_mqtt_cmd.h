/*
 * shared/net/mm_net_mqtt_cmd.h - shared MQTT BASIC command parsing.
 */

#ifndef MM_NET_MQTT_CMD_H
#define MM_NET_MQTT_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *host;
    int port;
    char *user;
    char *pass;
    char *interrupt;
    int has_interrupt;
} mm_net_mqtt_connect_args_t;

typedef struct {
    char *topic;
    char *message;
    int qos;
    int retain;
} mm_net_mqtt_publish_args_t;

typedef struct {
    char *topic;
    int qos;
} mm_net_mqtt_subscribe_args_t;

typedef struct {
    char *topic;
} mm_net_mqtt_topic_args_t;

void mm_net_mqtt_parse_connect(unsigned char *arg,
                               mm_net_mqtt_connect_args_t *out);
void mm_net_mqtt_parse_publish(unsigned char *arg,
                               mm_net_mqtt_publish_args_t *out);
void mm_net_mqtt_parse_subscribe(unsigned char *arg,
                                 mm_net_mqtt_subscribe_args_t *out);
void mm_net_mqtt_parse_topic(unsigned char *arg,
                             mm_net_mqtt_topic_args_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_MQTT_CMD_H */
