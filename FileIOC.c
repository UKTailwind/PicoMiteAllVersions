#include <stdint.h>
#include "configuration.h"

uint32_t lastfptr[MAXOPENFILES + 1] = {[0 ... MAXOPENFILES] = -1};
unsigned int bw[MAXOPENFILES + 1] = {[0 ... MAXOPENFILES] = -1};
