#include "pico_runtime_internal.h"

#ifndef rp2350
void __no_inline_not_in_flash_func(modclock)(uint16_t speed){
       ssi_hw->ssienr=0;
       ssi_hw->baudr=0;
       ssi_hw->baudr=speed;
       ssi_hw->ssienr=1;
}
#endif
