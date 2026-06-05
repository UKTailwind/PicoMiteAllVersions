/*
 * shared/net/mm_net_mqtt_cmd.c - shared MQTT BASIC command parsing.
 */

#include <string.h>

#include "MMBasic_Includes.h"
#include "shared/net/mm_net_mqtt_cmd.h"

void mm_net_mqtt_parse_connect(unsigned char * arg,
                               mm_net_mqtt_connect_args_t * out) {
    memset(out, 0, sizeof(*out));
    getargs(&arg, 9, (unsigned char *)",");
    if (!(argc == 7 || argc == 9)) error("Syntax");
    out->host = (char *)getCstring(argv[0]);
    out->port = getint(argv[2], 1, 65535);
    out->user = (char *)getCstring(argv[4]);
    out->pass = (char *)getCstring(argv[6]);
    if (argc == 9) {
        out->interrupt = (char *)GetIntAddress(argv[8]);
        out->has_interrupt = 1;
    }
}

void mm_net_mqtt_parse_publish(unsigned char * arg,
                               mm_net_mqtt_publish_args_t * out) {
    memset(out, 0, sizeof(*out));
    out->qos = 1;
    out->retain = 1;
    getargs(&arg, 7, (unsigned char *)",");
    if (!(argc == 3 || argc == 5 || argc == 7)) error("Syntax");
    out->topic = (char *)getCstring(argv[0]);
    out->message = (char *)getCstring(argv[2]);
    if (argc >= 5 && *argv[4]) out->qos = getint(argv[4], 0, 2);
    if (argc == 7) out->retain = getint(argv[6], 0, 1);
}

void mm_net_mqtt_parse_subscribe(unsigned char * arg,
                                 mm_net_mqtt_subscribe_args_t * out) {
    memset(out, 0, sizeof(*out));
    getargs(&arg, 3, (unsigned char *)",");
    if (!(argc == 1 || argc == 3)) error("Syntax");
    out->topic = (char *)getCstring(argv[0]);
    if (argc == 3) out->qos = getint(argv[2], 0, 2);
}

void mm_net_mqtt_parse_topic(unsigned char * arg,
                             mm_net_mqtt_topic_args_t * out) {
    memset(out, 0, sizeof(*out));
    out->topic = (char *)getCstring(arg);
}
