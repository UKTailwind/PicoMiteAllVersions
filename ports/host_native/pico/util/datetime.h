/* Stub for host build */
#ifndef _PICO_UTIL_DATETIME_H
#define _PICO_UTIL_DATETIME_H
#include <stdint.h>
typedef struct {
    int16_t year; int8_t month; int8_t day;
    int8_t dotw; int8_t hour; int8_t min; int8_t sec;
} datetime_t;
#endif
