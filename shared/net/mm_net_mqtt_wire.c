/*
 * shared/net/mm_net_mqtt_wire.c - small MQTT 3.1.1 packet helpers.
 */

#include <string.h>

#include "shared/net/mm_net_mqtt_wire.h"

size_t mm_net_mqtt_encode_remaining_length(uint8_t out[4], size_t value) {
    size_t n = 0;
    do {
        uint8_t encoded = (uint8_t)(value % 128);
        value /= 128;
        if (value) encoded |= 128;
        out[n++] = encoded;
    } while (value && n < 4);
    return n;
}

uint8_t * mm_net_mqtt_write_utf8(uint8_t * p, const char * text) {
    size_t len = text ? strlen(text) : 0;
    *p++ = (uint8_t)(len >> 8);
    *p++ = (uint8_t)len;
    if (len) {
        memcpy(p, text, len);
        p += len;
    }
    return p;
}

int mm_net_mqtt_read_utf8(const uint8_t * body, size_t body_len, size_t * pos,
                          char * out, size_t out_len) {
    if (!body || !pos || !out || out_len == 0 || *pos + 2 > body_len)
        return 0;
    size_t len = ((size_t)body[*pos] << 8) | body[*pos + 1];
    *pos += 2;
    if (*pos + len > body_len) return 0;
    size_t copy = len < out_len - 1 ? len : out_len - 1;
    memcpy(out, body + *pos, copy);
    out[copy] = 0;
    *pos += len;
    return 1;
}

int mm_net_mqtt_decode_publish(uint8_t header, const uint8_t * body,
                               size_t body_len, char * topic,
                               size_t topic_len,
                               const uint8_t ** payload,
                               size_t * payload_len) {
    if (!body || !topic || topic_len == 0 || !payload || !payload_len)
        return 0;
    size_t pos = 0;
    if ((header >> 4) != 3) return 0;
    if (!mm_net_mqtt_read_utf8(body, body_len, &pos, topic, topic_len))
        return 0;
    int qos = (header >> 1) & 0x03;
    if (qos) pos += 2;
    if (pos > body_len) return 0;
    *payload = body + pos;
    *payload_len = body_len - pos;
    return 1;
}
