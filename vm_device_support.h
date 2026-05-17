#ifndef VM_DEVICE_SUPPORT_H
#define VM_DEVICE_SUPPORT_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

#include "configuration.h"
#include "lfs.h"
#include "Hardware_Includes.h"
#include "FileIO.h"
#include "Draw.h"

#ifndef MMBASIC_HOST
#include "SPI-LCD.h"
#endif

#ifndef RADCONV
#define RADCONV   (MMFLOAT)57.2957795130823229
#endif
#ifndef Rad
#define Rad(a)  (((MMFLOAT)(a)) / RADCONV)
#endif

extern jmp_buf mark;

void error(char *msg, ...);
void Mstrcpy(unsigned char *dest, unsigned char *src);
int Mstrcmp(unsigned char *s1, unsigned char *s2);
int64_t FloatToInt64(MMFLOAT x);
void IntToStr(char *strr, int64_t nbr, unsigned int base);
void FloatToStr(char *p, MMFLOAT f, int m, int n, unsigned char ch);
void ClearVars(int level, bool all);

extern unsigned char *CurrentLinePtr;
extern int g_OptionBase;
extern int last_fcolour;
extern int last_bcolour;

extern uint64_t readusclock(void);
extern uint64_t timeroffset;
extern int MMInkey(void);
extern int check_interrupt(void);

#ifndef MMBASIC_HOST
extern volatile bool mergedone;
#endif

#endif
