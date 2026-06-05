/*
 * shared/net/mm_net_mqtt_wire.h - small MQTT 3.1.1 packet helpers.
 */

#ifndef MM_NET_MQTT_WIRE_H
#define MM_NET_MQTT_WIRE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t mm_net_mqtt_encode_remaining_length(uint8_t out[4], size_t value);
uint8_t * mm_net_mqtt_write_utf8(uint8_t * p, const char * text);
int mm_net_mqtt_read_utf8(const uint8_t * body, size_t body_len, size_t * pos,
                          char * out, size_t out_len);
int mm_net_mqtt_decode_publish(uint8_t header, const uint8_t * body,
                               size_t body_len, char * topic,
                               size_t topic_len,
                               const uint8_t ** payload,
                               size_t * payload_len);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_MQTT_WIRE_H */
