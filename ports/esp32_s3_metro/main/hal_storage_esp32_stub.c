/*
 * hal_storage_esp32_stub.c — Phase B stub for hal/hal_storage.h.
 * Phase E replaces with NVS-backed Options blob.
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal/hal_storage.h"

int hal_storage_init(hal_storage_dev_t d) {
    (void)d;
    return 0;
}
int hal_storage_read(hal_storage_dev_t d, uint32_t l, uint32_t c, void * b) {
    (void)d;
    (void)l;
    (void)c;
    (void)b;
    return -1;
}
int hal_storage_write(hal_storage_dev_t d, uint32_t l, uint32_t c, const void * b) {
    (void)d;
    (void)l;
    (void)c;
    (void)b;
    return -1;
}
int hal_storage_erase(hal_storage_dev_t d, uint32_t l, uint32_t c) {
    (void)d;
    (void)l;
    (void)c;
    return -1;
}
int hal_storage_sync(hal_storage_dev_t d) {
    (void)d;
    return 0;
}
bool hal_storage_present(hal_storage_dev_t d) {
    (void)d;
    return false;
}
