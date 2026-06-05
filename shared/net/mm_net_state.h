/*
 * shared/net/mm_net_state.h - BASIC-visible network message state.
 */

#ifndef MM_NET_STATE_H
#define MM_NET_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "shared/net/mm_net_interrupts.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MM_NET_STATE_MESSAGE = 0,
    MM_NET_STATE_ADDRESS = 1,
    MM_NET_STATE_TOPIC = 2,
};

void mm_net_state_clear_messages(void);
void mm_net_state_copy_mstring(int which, unsigned char * out);
void mm_net_state_set_mstring(int which, const void * data, size_t len);
void mm_net_state_set_ipv4_address(const uint8_t bytes[4]);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_STATE_H */
