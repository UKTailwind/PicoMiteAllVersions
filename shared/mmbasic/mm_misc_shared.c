/***********************************************************************************************************************
PicoMite MMBasic

mm_misc_shared.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/

/*
 * Portable subset of MM_Misc.c that compiles for both host (MMBASIC_HOST) and
 * device builds. Functions here must not touch hardware peripherals — no PWM,
 * GPIO, PIO, SPI, flash, watchdog, multicore. Device-only code stays in
 * MM_Misc.c.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_time.h"
#include "hal/hal_calendar.h"
#include <time.h>
#include <string.h>
#include "xregex.h"
#include "aes.h"

extern uint8_t getrnd(void);
extern unsigned int b64d_size(unsigned int in_size);
extern unsigned int b64e_size(unsigned int in_size);
extern unsigned int b64_encode(const unsigned char * in, unsigned int in_len, unsigned char * out);
extern unsigned int b64_decode(const unsigned char * in, unsigned int in_len, unsigned char * out);

/* Shared globals moved out of MM_Misc.c. */
int64_t TimeOffsetToUptime = 1704067200;
uint64_t timeroffset = 0;
#if defined(MMBASIC_HOST) && !defined(MMBASIC_ESP32)
int host_time_use_mmbasic_offset = 0;
#endif
const char * daystrings[] = {"dummy", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

/* Forward decl: cmd_longString calls parselongAES (defined below). */
void parselongAES(uint8_t * p, int ivadd, uint8_t * keyx, uint8_t * ivx, int64_t ** inint, int64_t ** outint);

void integersort(int64_t * iarray, int n, int64_t * index, int flags, int startpoint) {
    int i, j = n, s = 1;
    int64_t t;
    if ((flags & 1) == 0) {
        while (s) {
            s = 0;
            for (i = 1; i < j; i++) {
                if (iarray[i] < iarray[i - 1]) {
                    t = iarray[i];
                    iarray[i] = iarray[i - 1];
                    iarray[i - 1] = t;
                    s = 1;
                    if (index != NULL) {
                        t = index[i - 1 + startpoint];
                        index[i - 1 + startpoint] = index[i + startpoint];
                        index[i + startpoint] = t;
                    }
                }
            }
            j--;
        }
    } else {
        while (s) {
            s = 0;
            for (i = 1; i < j; i++) {
                if (iarray[i] > iarray[i - 1]) {
                    t = iarray[i];
                    iarray[i] = iarray[i - 1];
                    iarray[i - 1] = t;
                    s = 1;
                    if (index != NULL) {
                        t = index[i - 1 + startpoint];
                        index[i - 1 + startpoint] = index[i + startpoint];
                        index[i + startpoint] = t;
                    }
                }
            }
            j--;
        }
    }
}
void floatsort(MMFLOAT * farray, int n, int64_t * index, int flags, int startpoint) {
    int i, j = n, s = 1;
    int64_t t;
    MMFLOAT f;
    if ((flags & 1) == 0) {
        while (s) {
            s = 0;
            for (i = 1; i < j; i++) {
                if (farray[i] < farray[i - 1]) {
                    f = farray[i];
                    farray[i] = farray[i - 1];
                    farray[i - 1] = f;
                    s = 1;
                    if (index != NULL) {
                        t = index[i - 1 + startpoint];
                        index[i - 1 + startpoint] = index[i + startpoint];
                        index[i + startpoint] = t;
                    }
                }
            }
            j--;
        }
    } else {
        while (s) {
            s = 0;
            for (i = 1; i < j; i++) {
                if (farray[i] > farray[i - 1]) {
                    f = farray[i];
                    farray[i] = farray[i - 1];
                    farray[i - 1] = f;
                    s = 1;
                    if (index != NULL) {
                        t = index[i - 1 + startpoint];
                        index[i - 1 + startpoint] = index[i + startpoint];
                        index[i + startpoint] = t;
                    }
                }
            }
            j--;
        }
    }
}

void stringsort(unsigned char * sarray, int n, int offset, int64_t * index, int flags, int startpoint) {
    int ii, i, s = 1, isave;
    int k;
    unsigned char *s1, *s2, *p1, *p2;
    unsigned char temp;
    int reverse = 1 - ((flags & 1) << 1);
    while (s) {
        s = 0;
        for (i = 1; i < n; i++) {
            s2 = i * offset + sarray;
            s1 = (i - 1) * offset + sarray;
            ii = *s1 < *s2 ? *s1 : *s2; //get the smaller  length
            p1 = s1 + 1;
            p2 = s2 + 1;
            k = 0; //assume the strings match
            while ((ii--) && (k == 0)) {
                if (flags & 2) {
                    if (toupper(*p1) > toupper(*p2)) {
                        k = reverse; //earlier in the array is bigger
                    }
                    if (toupper(*p1) < toupper(*p2)) {
                        k = -reverse; //later in the array is bigger
                    }
                } else {
                    if (*p1 > *p2) {
                        k = reverse; //earlier in the array is bigger
                    }
                    if (*p1 < *p2) {
                        k = -reverse; //later in the array is bigger
                    }
                }
                p1++;
                p2++;
            }
            // if up to this point the strings match
            // make the decision based on which one is shorter
            if (k == 0) {
                if (*s1 > *s2) k = reverse;
                if (*s1 < *s2) k = -reverse;
            }
            if (k == 1) {                   // if earlier is bigger swap them round
                ii = *s1 > *s2 ? *s1 : *s2; //get the bigger length
                ii++;
                p1 = s1;
                p2 = s2;
                while (ii--) {
                    temp = *p1;
                    *p1 = *p2;
                    *p2 = temp;
                    p1++;
                    p2++;
                }
                s = 1;
                if (index != NULL) {
                    isave = index[i - 1 + startpoint];
                    index[i - 1 + startpoint] = index[i + startpoint];
                    index[i + startpoint] = isave;
                }
            }
        }
    }
    if ((flags & 5) == 5) {
        for (i = n - 1; i >= 0; i--) {
            s2 = i * offset + sarray;
            if (*s2 != 0) break;
        }
        i++;
        if (i) {
            s2 = (n - i) * offset + sarray;
            memmove(s2, sarray, offset * i);
            memset(sarray, 0, offset * (n - i));
            if (index != NULL) {
                int64_t * newindex = (int64_t *)GetTempMemory(n * sizeof(int64_t));
                memmove(&newindex[n - i], &index[startpoint], i * sizeof(int64_t));
                memmove(newindex, &index[startpoint + i], (n - i) * sizeof(int64_t));
                memmove(&index[startpoint], newindex, n * sizeof(int64_t));
            }
        }
    } else if (flags & 4) {
        for (i = 0; i < n; i++) {
            s2 = i * offset + sarray;
            if (*s2 != 0) break;
        }
        if (i) {
            s2 = i * offset + sarray;
            memmove(sarray, s2, offset * (n - i));
            s2 = (n - i) * offset + sarray;
            memset(s2, 0, offset * i);
            if (index != NULL) {
                int64_t * newindex = (int64_t *)GetTempMemory(n * sizeof(int64_t));
                memmove(newindex, &index[startpoint + i], (n - i) * sizeof(int64_t));
                memmove(&newindex[n - i], &index[startpoint], i * sizeof(int64_t));
                memmove(&index[startpoint], newindex, n * sizeof(int64_t));
            }
        }
    }
}

void cmd_sort(void) {
    MMFLOAT * a3float = NULL;
    int64_t *a3int = NULL, *a4int = NULL;
    unsigned char * a3str = NULL;
    int i, size = 0, truesize, flags = 0, maxsize = 0, startpoint = 0;
    getargs(&cmdline, 9, (unsigned char *)",");
    size = parseany(argv[0], &a3float, &a3int, &a3str, &maxsize, true) - 1;
    truesize = size;
    if (argc >= 3 && *argv[2]) {
        int card = parseintegerarray(argv[2], &a4int, 2, 1, NULL, true) - 1;
        if (card != size) error("Array size mismatch");
    }
    if (argc >= 5 && *argv[4]) flags = getint(argv[4], 0, 7);
    if (argc >= 7 && *argv[6]) startpoint = getint(argv[6], g_OptionBase, size + g_OptionBase);
    size -= startpoint;
    if (argc == 9) size = getint(argv[8], 1, size + 1 + g_OptionBase) - 1;
    if (startpoint) startpoint -= g_OptionBase;
    if (a3float != NULL) {
        a3float += startpoint;
        if (a4int != NULL)
            for (i = 0; i < truesize + 1; i++) a4int[i] = i + g_OptionBase;
        floatsort(a3float, size + 1, a4int, flags, startpoint);
    } else if (a3int != NULL) {
        a3int += startpoint;
        if (a4int != NULL)
            for (i = 0; i < truesize + 1; i++) a4int[i] = i + g_OptionBase;
        integersort(a3int, size + 1, a4int, flags, startpoint);
    } else if (a3str != NULL) {
        a3str += ((startpoint) * (maxsize + 1));
        if (a4int != NULL)
            for (i = 0; i < truesize + 1; i++) a4int[i] = i + g_OptionBase;
        stringsort(a3str, size + 1, maxsize + 1, a4int, flags, startpoint);
    }
}
// this is invoked as a command (ie, TIMER = 0)
// search through the line looking for the equals sign and step over it,
// evaluate the rest of the command and save in the timer
void cmd_timer(void) {
    uint64_t mytime = hal_time_us_64();
    while (*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
    if (!*cmdline) error("Syntax");
    timeroffset = mytime - (uint64_t)getint(++cmdline, 0, mytime / 1000) * 1000;
}
// this is invoked as a function
void MMB_HOT_FUNC(fun_timer)(void) {
    fret = (MMFLOAT)(hal_time_us_64() - timeroffset) / 1000.0;
    targ = T_NBR;
}
uint64_t gettimefromepoch(int * year, int * month, int * day, int * hour, int * minute, int * second) {
    struct tm * tm;
    struct tm tma;
    tm = &tma;
    uint64_t fulltime = hal_time_us_64();
    time_t epochnow = fulltime / 1000000 + TimeOffsetToUptime;
    hal_calendar_epoch_to_tm(epochnow, tm);
    *year = tm->tm_year + 1900;
    *month = tm->tm_mon + 1;
    *day = tm->tm_mday;
    *hour = tm->tm_hour;
    *minute = tm->tm_min;
    *second = tm->tm_sec;
    return fulltime;
}
void fun_datetime(void) {
    sret = GetTempMemory(STRINGSIZE); // this will last for the life of the command
    if (checkstring(ep, (unsigned char *)"NOW")) {
        int year, month, day, hour, minute, second;
        gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
        IntToStrPad((char *)sret, day, '0', 2, 10);
        sret[2] = '-';
        IntToStrPad((char *)sret + 3, month, '0', 2, 10);
        sret[5] = '-';
        IntToStr((char *)sret + 6, year, 10);
        sret[10] = ' ';
        IntToStrPad((char *)sret + 11, hour, '0', 2, 10);
        sret[13] = ':';
        IntToStrPad((char *)sret + 14, minute, '0', 2, 10);
        sret[16] = ':';
        IntToStrPad((char *)sret + 17, second, '0', 2, 10);
    } else {
        struct tm * tm;
        struct tm tma;
        tm = &tma;
        time_t timestamp = getinteger(ep);
        hal_calendar_epoch_to_tm(timestamp, tm);
        IntToStrPad((char *)sret, tm->tm_mday, '0', 2, 10);
        sret[2] = '-';
        IntToStrPad((char *)sret + 3, tm->tm_mon + 1, '0', 2, 10);
        sret[5] = '-';
        IntToStr((char *)sret + 6, tm->tm_year + 1900, 10);
        sret[10] = ' ';
        IntToStrPad((char *)sret + 11, tm->tm_hour, '0', 2, 10);
        sret[13] = ':';
        IntToStrPad((char *)sret + 14, tm->tm_min, '0', 2, 10);
        sret[16] = ':';
        IntToStrPad((char *)sret + 17, tm->tm_sec, '0', 2, 10);
    }
    CtoM(sret);
    targ = T_STR;
}
time_t get_epoch(int year, int month, int day, int hour, int minute, int second) {
    struct tm * tm;
    struct tm tma;
    tm = &tma;
    tm->tm_year = year - 1900;
    tm->tm_mon = month - 1;
    tm->tm_mday = day;
    tm->tm_hour = hour;
    tm->tm_min = minute;
    tm->tm_sec = second;
    return hal_calendar_tm_to_epoch(tm);
}

void fun_epoch(void) {
    unsigned char * arg;
    struct tm * tm;
    struct tm tma;
    tm = &tma;
    int d, m, y, h, min, s;
    if (!checkstring(ep, (unsigned char *)"NOW")) {
        arg = getCstring(ep);
        getargs(&arg, 11, (unsigned char *)"-/ :"); // this is a macro and must be the first executable stmt in a block
        if (!(argc == 11)) error("Syntax");
        d = atoi((char *)argv[0]);
        m = atoi((char *)argv[2]);
        y = atoi((char *)argv[4]);
        if (d > 1000) {
            int tmp = d;
            d = y;
            y = tmp;
        }
        if (y >= 0 && y < 100) y += 2000;
        if (d < 1 || d > 31 || m < 1 || m > 12 || y < 1902 || y > 2999) error("Invalid date");
        h = atoi((char *)argv[6]);
        min = atoi((char *)argv[8]);
        s = atoi((char *)argv[10]);
        if (h < 0 || h > 23 || min < 0 || m > 59 || s < 0 || s > 59) error("Invalid time");
        //            day = d;
        //            month = m;
        //            year = y;
        tm->tm_year = y - 1900;
        tm->tm_mon = m - 1;
        tm->tm_mday = d;
        tm->tm_hour = h;
        tm->tm_min = min;
        tm->tm_sec = s;
    } else {
        int year, month, day, hour, minute, second;
        gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
        tm->tm_year = year - 1900;
        tm->tm_mon = month - 1;
        tm->tm_mday = day;
        tm->tm_hour = hour;
        tm->tm_min = minute;
        tm->tm_sec = second;
    }
    time_t timestamp = hal_calendar_tm_to_epoch(tm);
    iret = timestamp;
    targ = T_INT;
}

static inline CommandToken mm_misc_commandtbl_decode(const unsigned char * p) {
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f) << 7);
}

static unsigned char * mm_misc_current_command_start(void) {
    unsigned char * p = cmdline;
    unsigned char * floor = CurrentLinePtr ? CurrentLinePtr : ProgMemory;

    while (p > floor) {
        p--;
        if (p[0] >= C_BASETOKEN && p[1] >= C_BASETOKEN &&
            mm_misc_commandtbl_decode(p) == cmdtoken) {
            return p;
        }
    }
    return cmdline - sizeof(CommandToken);
}

void cmd_pause(void) {
    static int interrupted = false;
    MMFLOAT f;
    static uint64_t PauseTimer, IntPauseTimer;
    f = getnumber(cmdline) * 1000; // get the pulse width
    if (f < 0) error("Number out of bounds");
    if (f < 2) return;

    if (f < 1500) {
        PauseTimer = hal_time_us_64() + (uint64_t)f;
        while (hal_time_us_64() < PauseTimer) {
        } // if less than 1.5mS do the pause right now
        return; // and exit straight away
    }

    if (InterruptReturn == NULL) {
        // we are running pause in a normal program
        // first check if we have reentered (from an interrupt) and only zero the timer if we have NOT been interrupted.
        // This means an interrupted pause will resume from where it was when interrupted
        if (!interrupted) PauseTimer = hal_time_us_64();
        interrupted = false;

        while (hal_time_us_64() < FloatToInt32(f) + PauseTimer) {
            CheckAbort();
            if (check_interrupt()) {
                // if there is an interrupt fake the return point to the start of this stmt
                // and return immediately to the program processor so that it can send us off
                // to the interrupt routine.  When the interrupt routine finishes we should reexecute
                // this stmt and because the variable interrupted is static we can see that we need to
                // resume pausing rather than start a new pause time.
                InterruptReturn = mm_misc_current_command_start(); // point to the command token
                interrupted = true;                                // show that this stmt was interrupted
                return;                                            // and let the interrupt run
            }
        }
        interrupted = false;
    } else {
        // we are running pause in an interrupt, this is much simpler but note that
        // we use a different timer from the main pause code (above)
        IntPauseTimer = hal_time_us_64();
        while (hal_time_us_64() < FloatToInt32(f) + IntPauseTimer) CheckAbort();
    }
}

void cmd_longString(void) {
    unsigned char * tp;
    tp = checkstring(cmdline, (unsigned char *)"SETBYTE");
    if (tp) {
        int64_t * dest = NULL;
        int p = 0;
        uint8_t * q = NULL;
        int nbr;
        int j = 0;
        getargs(&tp, 5, (unsigned char *)",");
        if (argc != 5) error("Argument count");
        j = (parseintegerarray(argv[0], &dest, 1, 1, NULL, true) - 1) * 8 - 1;
        q = (uint8_t *)&dest[1];
        p = getint(argv[2], g_OptionBase, j - g_OptionBase);
        nbr = getint(argv[4], 0, 255);
        q[p - g_OptionBase] = nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"APPEND");
    if (tp) {
        int64_t * dest = NULL;
        char * p = NULL;
        char * q = NULL;
        int i, j, nbr;
        getargs(&tp, 3, (unsigned char *)",");
        if (argc != 3) error("Argument count");
        j = parseintegerarray(argv[0], &dest, 1, 1, NULL, true) - 1;
        q = (char *)&dest[1];
        q += dest[0];
        p = (char *)getstring(argv[2]);
        nbr = i = *p++;
        if (j * 8 < dest[0] + i) error("Integer array too small");
        while (i--) *q++ = *p++;
        dest[0] += nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"TRIM");
    if (tp) {
        int64_t * dest = NULL;
        uint32_t trim;
        char *p, *q = NULL;
        int i;
        getargs(&tp, 3, (unsigned char *)",");
        if (argc != 3) error("Argument count");
        parseintegerarray(argv[0], &dest, 1, 1, NULL, true);
        q = (char *)&dest[1];
        trim = getint(argv[2], 1, dest[0]);
        i = dest[0] - trim;
        p = q + trim;
        while (i--) *q++ = *p++;
        dest[0] -= trim;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"REPLACE");
    if (tp) {
        int64_t * dest = NULL;
        char * p = NULL;
        char * q = NULL;
        int i, nbr;
        getargs(&tp, 5, (unsigned char *)",");
        if (argc != 5) error("Argument count");
        parseintegerarray(argv[0], &dest, 1, 1, NULL, true);
        q = (char *)&dest[1];
        p = (char *)getstring(argv[2]);
        nbr = getint(argv[4], 1, dest[0] - *p + 1);
        q += nbr - 1;
        i = *p++;
        while (i--) *q++ = *p++;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"LOAD");
    if (tp) {
        int64_t * dest = NULL;
        char * p;
        char * q = NULL;
        int i, j;
        getargs(&tp, 5, (unsigned char *)",");
        if (argc != 5) error("Argument count");
        int64_t nbr = getinteger(argv[2]);
        i = nbr;
        j = parseintegerarray(argv[0], &dest, 1, 1, NULL, true) - 1;
        q = (char *)&dest[1];
        dest[0] = 0;
        p = (char *)getstring(argv[4]);
        if (nbr > *p) nbr = *p;
        p++;
        if (j * 8 < dest[0] + nbr) error("Integer array too small");
        while (i--) *q++ = *p++;
        dest[0] += nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"LEFT");
    if (tp) {
        int64_t *dest = NULL, *src = NULL;
        char * p = NULL;
        char * q = NULL;
        int i, j, nbr;
        getargs(&tp, 5, (unsigned char *)",");
        if (argc != 5) error("Argument count");
        j = parseintegerarray(argv[0], &dest, 1, 1, NULL, true) - 1;
        q = (char *)&dest[1];
        parseintegerarray(argv[2], &src, 2, 1, NULL, false);
        p = (char *)&src[1];
        nbr = i = getinteger(argv[4]);
        if (nbr > src[0]) nbr = i = src[0];
        if (j * 8 < i) error("Destination array too small");
        while (i--) *q++ = *p++;
        dest[0] = nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"RIGHT");
    if (tp) {
        int64_t *dest = NULL, *src = NULL;
        char * p = NULL;
        char * q = NULL;
        int i, j, nbr;
        getargs(&tp, 5, (unsigned char *)",");
        if (argc != 5) error("Argument count");
        j = parseintegerarray(argv[0], &dest, 1, 1, NULL, true) - 1;
        q = (char *)&dest[1];
        parseintegerarray(argv[2], &src, 2, 1, NULL, false);
        p = (char *)&src[1];
        nbr = i = getinteger(argv[4]);
        if (nbr > src[0]) {
            nbr = i = src[0];
        } else
            p += (src[0] - nbr);
        if (j * 8 < i) error("Destination array too small");
        while (i--) *q++ = *p++;
        dest[0] = nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"MID");
    if (tp) {
        int64_t *dest = NULL, *src = NULL;
        char * p = NULL;
        char * q = NULL;
        int i, j, nbr, start;
        getargs(&tp, 7, (unsigned char *)",");
        if (argc < 5) error("Argument count");
        j = parseintegerarray(argv[0], &dest, 1, 1, NULL, true) - 1;
        q = (char *)&dest[1];
        parseintegerarray(argv[2], &src, 2, 1, NULL, false);
        p = (char *)&src[1];
        start = getint(argv[4], 1, src[0]);
        if (argc == 7)
            nbr = getinteger(argv[6]);
        else
            nbr = src[0];
        p += start - 1;
        if (nbr + start > src[0]) {
            nbr = src[0] - start + 1;
        }
        i = nbr;
        if (j * 8 < nbr) error("Destination array too small");
        while (i--) *q++ = *p++;
        dest[0] = nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"CLEAR");
    if (tp) {
        int64_t * dest = NULL;
        getargs(&tp, 1, (unsigned char *)",");
        if (argc != 1) error("Argument count");
        parseintegerarray(argv[0], &dest, 1, 1, NULL, true);
        dest[0] = 0;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"RESIZE");
    if (tp) {
        int64_t * dest = NULL;
        int j = 0;
        getargs(&tp, 3, (unsigned char *)",");
        if (argc != 3) error("Argument count");
        j = (parseintegerarray(argv[0], &dest, 1, 1, NULL, true) - 1) * 8;
        dest[0] = getint(argv[2], 0, j);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"UCASE");
    if (tp) {
        int64_t * dest = NULL;
        char * q = NULL;
        int i;
        getargs(&tp, 1, (unsigned char *)",");
        if (argc != 1) error("Argument count");
        parseintegerarray(argv[0], &dest, 1, 1, NULL, true);
        q = (char *)&dest[1];
        i = dest[0];
        while (i--) {
            if (*q >= 'a' && *q <= 'z')
                *q -= 0x20;
            q++;
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"PRINT");
    if (tp) {
        int64_t * dest = NULL;
        char * q = NULL;
        int j, fnbr = 0;
        bool docrlf = true;
        getargs(&tp, 5, (unsigned char *)",;");
        if (argc == 5) error("Syntax");
        if (argc >= 3) {
            if (*argv[0] == '#') argv[0]++; // check if the first arg is a file number
            fnbr = getinteger(argv[0]);     // get the number
            parseintegerarray(argv[2], &dest, 2, 1, NULL, true);
            if (*argv[3] == ';') docrlf = false;
        } else {
            parseintegerarray(argv[0], &dest, 1, 1, NULL, true);
            if (*argv[1] == ';') docrlf = false;
        }
        q = (char *)&dest[1];
        j = dest[0];
        while (j--) {
            MMfputc(*q++, fnbr);
        }
        if (docrlf) MMfputs((unsigned char *)"\2\r\n", fnbr);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"LCASE");
    if (tp) {
        int64_t * dest = NULL;
        char * q = NULL;
        int i;
        getargs(&tp, 1, (unsigned char *)",");
        if (argc != 1) error("Argument count");
        parseintegerarray(argv[0], &dest, 1, 1, NULL, true);
        q = (char *)&dest[1];
        i = dest[0];
        while (i--) {
            if (*q >= 'A' && *q <= 'Z')
                *q += 0x20;
            q++;
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"COPY");
    if (tp) {
        int64_t *dest = NULL, *src = NULL;
        char * p = NULL;
        char * q = NULL;
        int i = 0, j;
        getargs(&tp, 3, (unsigned char *)",");
        if (argc != 3) error("Argument count");
        j = parseintegerarray(argv[0], &dest, 1, 1, NULL, true);
        q = (char *)&dest[1];
        dest[0] = 0;
        parseintegerarray(argv[2], &src, 2, 1, NULL, false);
        p = (char *)&src[1];
        if (j * 8 < src[0]) error("Destination array too small");
        i = src[0];
        while (i--) *q++ = *p++;
        dest[0] = src[0];
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"CONCAT");
    if (tp) {
        int64_t *dest = NULL, *src = NULL;
        char * p = NULL;
        char * q = NULL;
        int i = 0, j, d = 0, s = 0;
        getargs(&tp, 3, (unsigned char *)",");
        if (argc != 3) error("Argument count");
        j = parseintegerarray(argv[0], &dest, 1, 1, NULL, true) - 1;
        q = (char *)&dest[1];
        d = dest[0];
        parseintegerarray(argv[2], &src, 2, 1, NULL, false);
        p = (char *)&src[1];
        i = s = src[0];
        if (j * 8 < (d + s)) error("Destination array too small");
        q += d;
        while (i--) *q++ = *p++;
        dest[0] += src[0];
        return;
    }
    //unsigned char * parselongAES(uint8_t *p, int ivadd, uint8_t *keyx, uint8_t *ivx, int64_t **inint, int64_t **outint)
    tp = checkstring(cmdline, (unsigned char *)"AES128");
    if (tp) {
        struct AES_ctx ctx;
        unsigned char keyx[16];
        unsigned char * p;
        int64_t *dest = NULL, *src = NULL;
        char * qq = NULL;
        char * q = NULL;
        //void parselongAES(uint8_t *p, int ivadd, uint8_t *keyx, uint8_t *ivx, int64_t **inint, int64_t **outint){
        if ((p = checkstring(tp, (unsigned char *)"ENCRYPT CBC"))) {
            uint8_t iv[16];
            for (int i = 0; i < 16; i++) iv[i] = getrnd();
            parselongAES(p, 16, &keyx[0], &iv[0], &src, &dest);
            qq = (char *)&src[1];
            q = (char *)&dest[1];
            dest[0] = src[0] + 16;
            memcpy(&q[16], qq, src[0]);
            memcpy(q, iv, 16);
            AES_init_ctx_iv(&ctx, keyx, iv);
            AES_CBC_encrypt_buffer(&ctx, (unsigned char *)&q[16], src[0]);
            return;
        } else if ((p = checkstring(tp, (unsigned char *)"DECRYPT CBC"))) {
            uint8_t iv[16];
            parselongAES(p, -16, &keyx[0], NULL, &src, &dest);
            dest[0] = src[0] - 16;
            qq = (char *)&src[1];
            q = (char *)&dest[1];
            memcpy(iv, qq, 16); //restore the IV
            memcpy(q, &qq[16], dest[0]);
            AES_init_ctx_iv(&ctx, keyx, iv);
            AES_CBC_decrypt_buffer(&ctx, (unsigned char *)q, dest[0]);
            return;
        } else if ((p = checkstring(tp, (unsigned char *)"ENCRYPT ECB"))) {
            struct AES_ctx ctxcopy;
            parselongAES(p, 0, &keyx[0], NULL, &src, &dest);
            qq = (char *)&src[1];
            q = (char *)&dest[1];
            dest[0] = src[0];
            memcpy(q, qq, src[0]);
            AES_init_ctx(&ctxcopy, keyx);
            for (int i = 0; i < src[0]; i += 16) {
                memcpy(&ctx, &ctxcopy, sizeof(ctx));
                AES_ECB_encrypt(&ctx, (unsigned char *)&q[i]);
            }
            return;
        } else if ((p = checkstring(tp, (unsigned char *)"DECRYPT ECB"))) {
            struct AES_ctx ctxcopy;
            parselongAES(p, 0, &keyx[0], NULL, &src, &dest);
            qq = (char *)&src[1];
            q = (char *)&dest[1];
            dest[0] = src[0];
            memcpy(q, qq, src[0]);
            AES_init_ctx(&ctxcopy, keyx);
            for (int i = 0; i < src[0]; i += 16) {
                memcpy(&ctx, &ctxcopy, sizeof(ctx));
                AES_ECB_decrypt(&ctx, (unsigned char *)&q[i]);
            }
            return;
        } else if ((p = checkstring(tp, (unsigned char *)"ENCRYPT CTR"))) {
            uint8_t iv[16];
            for (int i = 0; i < 16; i++) iv[i] = getrnd();
            parselongAES(p, 16, &keyx[0], &iv[0], &src, &dest);
            qq = (char *)&src[1];
            q = (char *)&dest[1];
            dest[0] = src[0] + 16;
            memcpy(&q[16], qq, src[0]);
            memcpy(q, iv, 16);
            AES_init_ctx_iv(&ctx, keyx, iv);
            AES_CTR_xcrypt_buffer(&ctx, (unsigned char *)&q[16], src[0]);
            return;
        } else if ((p = checkstring(tp, (unsigned char *)"DECRYPT CTR"))) {
            uint8_t iv[16];
            parselongAES(p, -16, &keyx[0], NULL, &src, &dest);
            dest[0] = src[0] - 16;
            qq = (char *)&src[1];
            q = (char *)&dest[1];
            memcpy(iv, qq, 16); //restore the IV
            memcpy(q, &qq[16], dest[0]);
            AES_init_ctx_iv(&ctx, keyx, iv);
            AES_CTR_xcrypt_buffer(&ctx, (unsigned char *)q, dest[0]);
            return;
        } else
            error("Syntax");
    }
    tp = checkstring(cmdline, (unsigned char *)"BASE64");
    if (tp) {
        unsigned char * p;
        if ((p = checkstring(tp, (unsigned char *)"ENCODE"))) {
            int64_t *dest = NULL, *src = NULL;
            unsigned char * qq = NULL;
            unsigned char * q = NULL;
            int j;
            getargs(&p, 3, (unsigned char *)",");
            if (argc != 3) error("Argument count");
            j = parseintegerarray(argv[2], &dest, 2, 1, NULL, true) - 1;
            q = (unsigned char *)&dest[1];
            parseintegerarray(argv[0], &src, 1, 1, NULL, false);
            qq = (unsigned char *)&src[1];
            if (j * 8 < b64e_size(src[0])) error("Destination array too small");
            dest[0] = b64_encode(qq, src[0], q);
            return;
        } else if ((p = checkstring(tp, (unsigned char *)"DECODE"))) {
            int64_t *dest = NULL, *src = NULL;
            unsigned char * qq = NULL;
            unsigned char * q = NULL;
            int j;
            getargs(&p, 3, (unsigned char *)",");
            if (argc != 3) error("Argument count");
            j = parseintegerarray(argv[2], &dest, 2, 1, NULL, true) - 1;
            q = (unsigned char *)&dest[1];
            parseintegerarray(argv[0], &src, 1, 1, NULL, false);
            qq = (unsigned char *)&src[1];
            if (j * 8 < b64d_size(src[0])) error("Destination array too small");
            dest[0] = b64_decode(qq, src[0], q);
            return;
        } else
            error("Syntax");
    }
    error("Invalid option");
}
void parselongAES(uint8_t * p, int ivadd, uint8_t * keyx, uint8_t * ivx, int64_t ** inint, int64_t ** outint) {
    int64_t *a1int = NULL, *a2int = NULL, *a3int = NULL, *a4int = NULL;
    unsigned char *a1str = NULL, *a4str = NULL;
    MMFLOAT *a1float = NULL, *a4float = NULL;
    int card1, card3;
    getargs(&p, 7, (unsigned char *)",");
    if (ivx == NULL) {
        if (argc != 5) error("Syntax");
    } else {
        if (argc < 5) error("Syntax");
    }
    *outint = NULL;
    // first process the key
    int length = 0;
    card1 = parseany(argv[0], &a1float, &a1int, &a1str, &length, false);
    if (card1 != 16) error("Key must be 16 elements long");
    if (a1int != NULL) {
        for (int i = 0; i < 16; i++) {
            if (a1int[i] < 0 || a1int[i] > 255) error("Key number out of bounds 0-255");
            keyx[i] = a1int[i];
        }
    } else if (a1float != NULL) {
        for (int i = 0; i < 16; i++) {
            if (a1float[i] < 0 || a1float[i] > 255) error("Key number out of bounds 0-255");
            keyx[i] = a1float[i];
        }
    } else if (a1str != NULL) {
        for (int i = 0; i < 16; i++) {
            keyx[i] = a1str[i + 1];
        }
    }
    //next process the initialisation vector if any
    if (argc == 7) {
        length = 0;
        card1 = parseany(argv[6], &a4float, &a4int, &a4str, &length, false);
        if (card1 != 16) error("Initialisation vector must be 16 elements long");
        if (a4int != NULL) {
            for (int i = 0; i < 16; i++) {
                if (a4int[i] < 0 || a4int[i] > 255) error("Key number out of bounds 0-255");
                ivx[i] = a4int[i];
            }
        } else if (a4float != NULL) {
            for (int i = 0; i < 16; i++) {
                if (a4float[i] < 0 || a4float[i] > 255) error("Key number out of bounds 0-255");
                ivx[i] = a4float[i];
            }
        } else if (a4str != NULL) {
            for (int i = 0; i < 16; i++) {
                ivx[i] = a4str[i + 1];
            }
        }
    }
    //now process the longstring used for input
    parseintegerarray(argv[2], &a2int, 2, 1, NULL, false);
    if (*a2int % 16) error("input must be multiple of 16 elements long");
    *inint = a2int;
    card3 = parseintegerarray(argv[4], &a3int, 3, 1, NULL, false);
    if ((card3 - 1) * 8 < *a2int + ivadd) error("Output array too small");
    *outint = a3int;
}

void fun_LGetStr(void) {
    char * p;
    char * s = NULL;
    int64_t * src = NULL;
    int start, nbr, j;
    getargs(&ep, 5, (unsigned char *)",");
    if (argc != 5) error("Argument count");
    j = (parseintegerarray(argv[0], &src, 2, 1, NULL, false) - 1) * 8;
    start = getint(argv[2], 1, j);
    nbr = getinteger(argv[4]);
    if (nbr < 1 || nbr > MAXSTRLEN) error("Number out of bounds");
    if (start + nbr > src[0]) nbr = src[0] - start + 1;
    sret = GetTempMemory(STRINGSIZE); // this will last for the life of the command
    s = (char *)&src[1];
    s += (start - 1);
    p = (char *)sret + 1;
    *sret = nbr;
    while (nbr--) *p++ = *s++;
    *p = 0;
    targ = T_STR;
}

void fun_LGetByte(void) {
    uint8_t * s = NULL;
    int64_t * src = NULL;
    int start, j;
    getargs(&ep, 3, (unsigned char *)",");
    if (argc != 3) error("Argument count");
    j = (parseintegerarray(argv[0], &src, 2, 1, NULL, false) - 1) * 8;
    s = (uint8_t *)&src[1];
    start = getint(argv[2], g_OptionBase, j - g_OptionBase);
    iret = s[start - g_OptionBase];
    targ = T_INT;
}

void fun_LInstr(void) {
    int64_t * src = NULL;
    char srch[STRINGSIZE];
    char * str = NULL;
    int slen, found = 0, i, j, n;
    getargs(&ep, 7, (unsigned char *)",");
    if (argc < 3 || argc > 7) error("Argument count");
    int64_t start;
    if (argc >= 5 && *argv[4])
        start = getinteger(argv[4]) - 1;
    else
        start = 0;
    j = (parseintegerarray(argv[0], &src, 2, 1, NULL, false) - 1);
    str = (char *)&src[0];
    Mstrcpy((unsigned char *)srch, (unsigned char *)getstring(argv[2]));
    if (argc < 7) {
        slen = *srch;
        iret = 0;
        if (start > src[0] || start < 0 || slen == 0 || src[0] == 0 || slen > src[0] - start) found = 1;
        if (!found) {
            n = src[0] - slen - start;

            for (i = start; i <= n + start; i++) {
                if (str[i + 8] == srch[1]) {
                    for (j = 0; j < slen; j++)
                        if (str[j + i + 8] != srch[j + 1])
                            break;
                    if (j == slen) {
                        iret = i + 1;
                        break;
                    }
                }
            }
        }
    } else { //search string is a regular expression
        regex_t regex;
        int reti;
        regmatch_t pmatch;
        MMFLOAT * temp = NULL;
        MtoC((unsigned char *)srch);
        temp = findvar(argv[6], V_FIND);
        if (!(g_vartbl[g_VarIndex].type & T_NBR)) error("Invalid variable");
        reti = regcomp(&regex, srch, 0);
        if (reti) {
            regfree(&regex);
            error("Could not compile regex");
        }
        reti = regexec(&regex, &str[start + 8], 1, &pmatch, 0);
        if (!reti) {
            iret = pmatch.rm_so + 1 + start;
            if (temp) *temp = (MMFLOAT)(pmatch.rm_eo - pmatch.rm_so);
        } else if (reti == REG_NOMATCH) {
            iret = 0;
            if (temp) *temp = 0.0;
        } else {
            regfree(&regex);
            error("Regex execution error");
        }
        regfree(&regex);
    }
    targ = T_INT;
}

void fun_LCompare(void) {
    int64_t *dest, *src;
    char * p = NULL;
    char * q = NULL;
    int d = 0, s = 0, found = 0;
    getargs(&ep, 3, (unsigned char *)",");
    if (argc != 3) error("Argument count");
    parseintegerarray(argv[0], &dest, 1, 1, NULL, false);
    q = (char *)&dest[1];
    d = dest[0];
    parseintegerarray(argv[2], &src, 1, 1, NULL, false);
    p = (char *)&src[1];
    s = src[0];
    while (!found) {
        if (d == 0 && s == 0) {
            found = 1;
            iret = 0;
        }
        if (d == 0 && !found) {
            found = 1;
            iret = -1;
        }
        if (s == 0 && !found) {
            found = 1;
            iret = 1;
        }
        if (*q < *p && !found) {
            found = 1;
            iret = -1;
        }
        if (*q > *p && !found) {
            found = 1;
            iret = 1;
        }
        q++;
        p++;
        d--;
        s--;
    }
    targ = T_INT;
}

void fun_LLen(void) {
    int64_t * dest = NULL;
    getargs(&ep, 1, (unsigned char *)",");
    if (argc != 1) error("Argument count");
    parseintegerarray(argv[0], &dest, 1, 1, NULL, false);
    iret = dest[0];
    targ = T_INT;
}

// this is invoked as a command (ie, date$ = "6/7/2010")
// search through the line looking for the equals sign and step over it,
// evaluate the rest of the command, split it up and save in the system counters
void cmd_date(void) {
    unsigned char * arg;
    struct tm * tm;
    struct tm tma;
    tm = &tma;
    int dd, mm, yy;
    while (*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
    if (!*cmdline) error("Syntax");
    ++cmdline;
    arg = getCstring(cmdline);
    {
        getargs(&arg, 5, (unsigned char *)"-/"); // this is a macro and must be the first executable stmt in a block
        if (argc != 5) error("Syntax");
        dd = atoi((char *)argv[0]);
        mm = atoi((char *)argv[2]);
        yy = atoi((char *)argv[4]);
        if (dd > 1000) {
            int tmp = dd;
            dd = yy;
            yy = tmp;
        }
        if (yy >= 0 && yy < 100) yy += 2000;
        //check year
        if (yy >= 1900 && yy <= 9999) {
            //check month
            if (mm >= 1 && mm <= 12) {
                //check days
                if ((dd >= 1 && dd <= 31) && (mm == 1 || mm == 3 || mm == 5 || mm == 7 || mm == 8 || mm == 10 || mm == 12)) {
                } else if ((dd >= 1 && dd <= 30) && (mm == 4 || mm == 6 || mm == 9 || mm == 11)) {
                } else if ((dd >= 1 && dd <= 28) && (mm == 2)) {
                } else if (dd == 29 && mm == 2 && (yy % 400 == 0 || (yy % 4 == 0 && yy % 100 != 0))) {
                } else
                    error("Day is invalid");
            } else {
                error("Month is not valid");
            }
        } else {
            error("Year is not valid");
        }
        int year, month, day, hour, minute, second;
        gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
        //		mT4IntEnable(0);       										// disable the timer interrupt to prevent any conflicts while updating
        day = dd;
        month = mm;
        year = yy;
        tm->tm_year = year - 1900;
        tm->tm_mon = month - 1;
        tm->tm_mday = day;
        tm->tm_hour = hour;
        tm->tm_min = minute;
        tm->tm_sec = second;
        time_t timestamp = hal_calendar_tm_to_epoch(tm);
        hal_calendar_epoch_to_tm(timestamp, tm);
        day_of_week = tm->tm_wday;
        if (day_of_week == 0) day_of_week = 7;
        TimeOffsetToUptime = get_epoch(year, month, day, hour, minute, second) - hal_time_us_64() / 1000000;
        //		update_clock();
        //		mT4IntEnable(1);       										// enable interrupt
    }
}

// this is invoked as a function
void fun_date(void) {
#if defined(MMBASIC_HOST) && !defined(MMBASIC_ESP32)
    /* Tests set MMBASIC_HOST_DATE to pin a deterministic value across
     * interpreter + VM comparison; otherwise fall back to wall clock so
     * --sim shows real time. WEB NTP flips host_time_use_mmbasic_offset so
     * host network conformance follows the device DATE$/TIME$ path. */
    sret = GetTempMemory(STRINGSIZE);
    const char * mock = getenv("MMBASIC_HOST_DATE");
    if (mock && *mock) {
        strncpy((char *)sret, mock, 15);
        ((char *)sret)[15] = '\0';
    } else if (host_time_use_mmbasic_offset) {
        int year, month, day, hour, minute, second;
        gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
        snprintf((char *)sret, 16, "%02d-%02d-%04d", day, month, year);
    } else {
        time_t now = time(NULL);
        struct tm * lt = localtime(&now);
        if (lt) {
            snprintf((char *)sret, 16, "%02d-%02d-%04d",
                     lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900);
        }
    }
    CtoM(sret);
    targ = T_STR;
#else
    int year, month, day, hour, minute, second;
    gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
    sret = GetTempMemory(STRINGSIZE); // this will last for the life of the command
    IntToStrPad((char *)sret, day, '0', 2, 10);
    sret[2] = '-';
    IntToStrPad((char *)sret + 3, month, '0', 2, 10);
    sret[5] = '-';
    IntToStr((char *)sret + 6, year, 10);
    CtoM(sret);
    targ = T_STR;
#endif
}

// this is invoked as a function
void fun_day(void) {
    unsigned char * arg;
    struct tm * tm;
    struct tm tma;
    tm = &tma;
    time_t time_of_day;
    int i;
    sret = GetTempMemory(STRINGSIZE); // this will last for the life of the command
    int d, m, y;
    if (!checkstring(ep, (unsigned char *)"NOW")) {
        arg = getCstring(ep);
        getargs(&arg, 5, (unsigned char *)"-/"); // this is a macro and must be the first executable stmt in a block
        if (!(argc == 5)) error("Syntax");
        d = atoi((char *)argv[0]);
        m = atoi((char *)argv[2]);
        y = atoi((char *)argv[4]);
        if (d > 1000) {
            int tmp = d;
            d = y;
            y = tmp;
        }
        if (y >= 0 && y < 100) y += 2000;
        if (d < 1 || d > 31 || m < 1 || m > 12 || y < 1902 || y > 2999) error("Invalid date");
        tm->tm_year = y - 1900;
        tm->tm_mon = m - 1;
        tm->tm_mday = d;
        tm->tm_hour = 0;
        tm->tm_min = 0;
        tm->tm_sec = 0;
        time_of_day = hal_calendar_tm_to_epoch(tm);
        hal_calendar_epoch_to_tm(time_of_day, tm);
        i = tm->tm_wday;
        if (i == 0) i = 7;
        strcpy((char *)sret, daystrings[i]);
    } else {
        int year, month, day, hour, minute, second;
        gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
        tm->tm_year = year - 1900;
        tm->tm_mon = month - 1;
        tm->tm_mday = day;
        tm->tm_hour = 0;
        tm->tm_min = 0;
        tm->tm_sec = 0;
        time_of_day = hal_calendar_tm_to_epoch(tm);
        hal_calendar_epoch_to_tm(time_of_day, tm);
        i = tm->tm_wday;
        if (i == 0) i = 7;
        strcpy((char *)sret, daystrings[i]);
    }
    CtoM(sret);
    targ = T_STR;
}

// this is invoked as a command (ie, time$ = "6:10:45")
// search through the line looking for the equals sign and step over it,
// evaluate the rest of the command, split it up and save in the system counters
void cmd_time(void) {
    unsigned char * arg;
    int h = 0;
    int m = 0;
    int s = 0;
    MMFLOAT f;
    int64_t i64;
    unsigned char * ss;
    int t = 0;
    int offset;
    while (*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
    if (!*cmdline) error("Syntax");
    ++cmdline;
    evaluate(cmdline, &f, &i64, &ss, &t, false);
    int year, month, day, hour, minute, second;
    gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
    if (t & T_STR) {
        arg = getCstring(cmdline);
        {
            getargs(&arg, 5, (unsigned char *)":"); // this is a macro and must be the first executable stmt in a block
            if (argc % 2 == 0) error("Syntax");
            h = atoi((char *)argv[0]);
            if (argc >= 3) m = atoi((char *)argv[2]);
            if (argc == 5) s = atoi((char *)argv[4]);
            if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) error("Invalid time");
            //            mT4IntEnable(0);       										// disable the timer interrupt to prevent any conflicts while updating
            hour = h;
            minute = m;
            second = s;
            SecondsTimer = 0;
            //		update_clock();
            //            mT4IntEnable(1);       										// enable interrupt
        }
    } else {
        struct tm * tm;
        struct tm tma;
        tm = &tma;
        offset = getinteger(cmdline);
        tm->tm_year = year - 1900;
        tm->tm_mon = month - 1;
        tm->tm_mday = day;
        tm->tm_hour = hour;
        tm->tm_min = minute;
        tm->tm_sec = second;
        time_t timestamp = hal_calendar_tm_to_epoch(tm);
        timestamp += offset;
        hal_calendar_epoch_to_tm(timestamp, tm);
        //		mT4IntEnable(0);       										// disable the timer interrupt to prevent any conflicts while updating
        hour = tm->tm_hour;
        minute = tm->tm_min;
        second = tm->tm_sec;
        SecondsTimer = 0;
        //		update_clock();
        //    	mT4IntEnable(1);       										// enable interrupt
    }
    TimeOffsetToUptime = get_epoch(year, month, day, hour, minute, second) - hal_time_us_64() / 1000000;
}

// this is invoked as a function
void fun_time(void) {
#if defined(MMBASIC_HOST) && !defined(MMBASIC_ESP32)
    /* See fun_date() above for the env-var/runtime override rationale. */
    sret = GetTempMemory(STRINGSIZE);
    const char * mock = getenv("MMBASIC_HOST_TIME");
    if (mock && *mock) {
        strncpy((char *)sret, mock, 15);
        ((char *)sret)[15] = '\0';
    } else if (host_time_use_mmbasic_offset) {
        int year, month, day, hour, minute, second;
        uint64_t fulltime = gettimefromepoch(&year, &month, &day, &hour,
                                             &minute, &second);
        snprintf((char *)sret, 16, "%02d:%02d:%02d", hour, minute, second);
        if (optionfulltime) {
            sret[8] = '.';
            IntToStrPad((char *)sret + 9, (fulltime / 1000) % 1000, '0', 3, 10);
        }
    } else {
        time_t now = time(NULL);
        struct tm * lt = localtime(&now);
        if (lt) {
            snprintf((char *)sret, 16, "%02d:%02d:%02d",
                     lt->tm_hour, lt->tm_min, lt->tm_sec);
        }
    }
    CtoM(sret);
    targ = T_STR;
#else
    int year, month, day, hour, minute, second;
    sret = GetTempMemory(STRINGSIZE); // this will last for the life of the command
    uint64_t fulltime = gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
    IntToStrPad((char *)sret, hour, '0', 2, 10);
    sret[2] = ':';
    IntToStrPad((char *)sret + 3, minute, '0', 2, 10);
    sret[5] = ':';
    IntToStrPad((char *)sret + 6, second, '0', 2, 10);
    if (optionfulltime) {
        sret[8] = '.';
        IntToStrPad((char *)sret + 9, (fulltime / 1000) % 1000, '0', 3, 10);
    }
    CtoM(sret);
    targ = T_STR;
#endif
}

void fun_format(void) {
    unsigned char *p, *fmt;
    int inspec;
    getargs(&ep, 3, (unsigned char *)",");
    if (argc % 2 == 0) error("Invalid syntax");
    if (argc == 3)
        fmt = getCstring(argv[2]);
    else
        fmt = (unsigned char *)"%g";

    // check the format string for errors that might crash the CPU
    for (inspec = 0, p = fmt; *p; p++) {
        if (*p == '%') {
            inspec++;
            if (inspec > 1) error("Only one format specifier (%) allowed");
            continue;
        }

        if (inspec == 1 && (*p == 'g' || *p == 'G' || *p == 'f' || *p == 'e' || *p == 'E' || *p == 'l'))
            inspec++;

        if (inspec == 1 && !(IsDigitinline(*p) || *p == '+' || *p == '-' || *p == '.' || *p == ' '))
            error("Illegal character in format specification");
    }
    if (inspec != 2) error("Format specification not found");
    sret = GetTempMemory(STRINGSIZE); // this will last for the life of the command
    sprintf((char *)sret, (char *)fmt, getnumber(argv[0]));
    CtoM(sret);
    targ = T_STR;
}
