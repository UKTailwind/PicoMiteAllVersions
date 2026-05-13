/*
 * shared/net/mm_net_interrupts.h - BASIC-visible network interrupt state.
 */

#ifndef MM_NET_INTERRUPTS_H
#define MM_NET_INTERRUPTS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile bool TCPreceived;
extern char *TCPreceiveInterrupt;
extern volatile bool UDPreceive;
extern char *UDPinterrupt;
extern volatile bool MQTTComplete;
extern char *MQTTInterrupt;

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_INTERRUPTS_H */
