/*
 * shared/net/mm_net_state.c - BASIC-visible network message buffers.
 *
 * The buffers use MMBasic's counted-string format so MM.MESSAGE$,
 * MM.ADDRESS$, and MM.TOPIC$ can copy them directly into a string result.
 */

#include <string.h>
#include <stdio.h>

#include "MMBasic_Includes.h"
#include "shared/net/mm_net_state.h"

unsigned char topicbuff[STRINGSIZE] = {0};
unsigned char messagebuff[STRINGSIZE] = {0};
unsigned char addressbuff[20] = {0};

static unsigned char * select_buffer(int which, size_t * cap_out) {
    switch (which) {
    case MM_NET_STATE_MESSAGE:
        if (cap_out) *cap_out = sizeof(messagebuff);
        return messagebuff;
    case MM_NET_STATE_ADDRESS:
        if (cap_out) *cap_out = sizeof(addressbuff);
        return addressbuff;
    case MM_NET_STATE_TOPIC:
        if (cap_out) *cap_out = sizeof(topicbuff);
        return topicbuff;
    default:
        if (cap_out) *cap_out = 0;
        return NULL;
    }
}

void mm_net_state_clear_messages(void) {
    memset(messagebuff, 0, sizeof(messagebuff));
    memset(addressbuff, 0, sizeof(addressbuff));
    memset(topicbuff, 0, sizeof(topicbuff));
}

void mm_net_state_copy_mstring(int which, unsigned char * out) {
    unsigned char * src = select_buffer(which, NULL);
    if (!src) {
        out[0] = 0;
        out[1] = 0;
        return;
    }
    Mstrcpy(out, src);
}

void mm_net_state_set_mstring(int which, const void * data, size_t len) {
    size_t cap = 0;
    unsigned char * dst = select_buffer(which, &cap);
    if (!dst || cap == 0) return;
    if (len > cap - 1) len = cap - 1;
    memset(dst, 0, cap);
    dst[0] = (unsigned char)len;
    if (len && data) memcpy(dst + 1, data, len);
}

void mm_net_state_set_ipv4_address(const uint8_t bytes[4]) {
    if (!bytes) return;
    char text[16];
    int n = snprintf(text, sizeof(text), "%u.%u.%u.%u",
                     (unsigned)bytes[0], (unsigned)bytes[1],
                     (unsigned)bytes[2], (unsigned)bytes[3]);
    if (n < 0) return;
    mm_net_state_set_mstring(MM_NET_STATE_ADDRESS, text, (size_t)n);
}

/* Functions.c calls this port hook for MM.MESSAGE$ / MM.ADDRESS$ /
 * MM.TOPIC$. Keeping the symbol here lets ports share the state without
 * pulling network-specific logic into Functions.c. */
void port_fun_mm_mqtt_copy(int which, unsigned char * out) {
    mm_net_state_copy_mstring(which, out);
}
