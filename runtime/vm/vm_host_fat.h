#ifndef __VM_HOST_FAT_H
#define __VM_HOST_FAT_H

#ifdef MMBASIC_HOST

#include "ff.h"

FRESULT vm_host_fat_mount(void);
void vm_host_fat_reset(void);
const char *vm_host_fat_path(const char *filename);

#endif

#endif
