/*
 * shared/net/mm_net_interrupts.c - BASIC-visible network interrupt state.
 */

#include "shared/net/mm_net_interrupts.h"

volatile bool TCPreceived = false;
char *TCPreceiveInterrupt = 0;
volatile bool UDPreceive = false;
char *UDPinterrupt = 0;
volatile bool MQTTComplete = false;
char *MQTTInterrupt = 0;
