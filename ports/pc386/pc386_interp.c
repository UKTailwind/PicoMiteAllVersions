/*
 * ports/pc386/pc386_interp.c — interpreter-level glue, not a hardware
 * driver but real implementations of MMBasic commands that ride on top
 * of pc386's runtime:
 *
 *   SETTICK period_ms, handler [, irq]   — periodic callback
 *   IRETURN                              — return from a tick handler
 *   WATCHDOG ms / OFF / (bare = kick)    — soft watchdog
 *   MM.INFO(...)                         — runtime query
 *   GetIntAddress(handler-name)          — resolve handler ref to addr
 *
 * The poll-based driver (which advances TickTimer, fires handlers, and
 * SoftResets on watchdog expiry) lives in pc386_runtime.c's
 * check_interrupt(). This file just wires the BASIC verbs to it.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern volatile int TickTimer[];
extern int TickPeriod[];
extern unsigned char * TickInt[];
extern volatile unsigned char TickActive[];
extern unsigned char * InterruptReturn;
extern unsigned char * nextstmt;
extern int g_LocalIndex;
extern void pc386_watchdog_kick(uint32_t period_ms);

extern int FindSubFun(unsigned char * p, int type);
extern unsigned char * findlabel(unsigned char * p);
extern unsigned char * findline(int linenbr, int mustfind);
extern unsigned char * subfun[];
extern short gui_font_width, gui_font_height;
extern int gui_fcolour, gui_bcolour;

/* ---------- GetIntAddress -------------------------------------------- */

unsigned char * GetIntAddress(unsigned char * p) {
    if (isnamestart((uint8_t)*p)) {
        int i = FindSubFun(p, 0);
        if (i == -1) return findlabel(p);
        return subfun[i];
    }
    return findline(getinteger(p), 1);
}

/* ---------- SETTICK / IRETURN ---------------------------------------- */

void cmd_settick(void) {
    int period, irq = 0;
    char * s = GetTempMemory(STRINGSIZE);
    getargs(&cmdline, 5, (unsigned char *)",");
    /* Canonical SETTICK accepts argc == 3 or 5. The forms are:
     *   SETTICK period, handler [, irq]
     *   SETTICK PAUSE / RESUME, anything [, irq]      (middle arg ignored)
     * Note: `SETTICK PAUSE` alone is NOT valid — the period-or-keyword
     * arg is mandatory and so is the (placeholder) handler arg. */
    if (!(argc == 3 || argc == 5)) error("Argument count");
    strcpy(s, (char *)argv[0]);
    if (argc == 5) irq = getint(argv[4], 1, 4) - 1;
    if (strcasecmp((char *)argv[0], "PAUSE") == 0) {
        TickActive[irq] = 0;
        return;
    }
    if (strcasecmp((char *)argv[0], "RESUME") == 0) {
        TickActive[irq] = 1;
        return;
    }
    period = getint(argv[0], -1, 0x7FFFFFFF);
    if (period == 0) {
        TickInt[irq] = NULL;
        TickPeriod[irq] = 0;
        TickTimer[irq] = 0;
        TickActive[irq] = 0;
    } else {
        TickPeriod[irq] = period;
        TickInt[irq] = GetIntAddress(argv[2]);
        TickTimer[irq] = 0;
        InterruptUsed = 1; /* match canonical MM_Misc.c */
        TickActive[irq] = 1;
    }
}

void cmd_ireturn(void) {
    if (InterruptReturn == NULL) error("Not in interrupt");
    nextstmt = InterruptReturn;
    InterruptReturn = NULL;
    if (g_LocalIndex > 0) g_LocalIndex--;
}

/* ---------- WATCHDOG ------------------------------------------------- */
/* Canonical MMBasic forms (per core/mmbasic/MM_Misc.c):
 *     WATCHDOG ms       — arm or refresh; resets if not refreshed before
 *                         the countdown elapses (call repeatedly to stay
 *                         alive — this IS the "kick" pattern, no bare form)
 *     WATCHDOG OFF      — disarm
 *     WATCHDOG HW ...   — hardware WDT (errors on pc386: no HW WDT)
 * The poll-based decrement + SoftReset lives in pc386_runtime.c's
 * check_interrupt(). */

void cmd_watchdog(void) {
    unsigned char * p;
    if ((p = checkstring(cmdline, (unsigned char *)"HW"))) {
        (void)p;
        error("WATCHDOG HW not available on pc386 (no hardware WDT)");
    }
    if (checkstring(cmdline, (unsigned char *)"OFF") != NULL) {
        pc386_watchdog_kick(0);
        return;
    }
    int ms = getint(cmdline, 1, 0x7FFFFFFF);
    pc386_watchdog_kick((uint32_t)ms);
}

/* ---------- MM.INFO -------------------------------------------------- */

void fun_info(void) {
    sret = GetTempMemory(STRINGSIZE);
    if (!ep || !*ep) {
        sret[0] = 0;
        targ = T_STR;
        return;
    }
    if (checkstring(ep, (unsigned char *)"HRES")) {
        iret = HRes;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"VRES")) {
        iret = VRes;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"FONTWIDTH")) {
        iret = gui_font_width;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"FONTHEIGHT")) {
        iret = gui_font_height;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"FONT")) {
        iret = (gui_font >> 4) + 1;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"FCOLOUR") ||
        checkstring(ep, (unsigned char *)"FCOLOR")) {
        iret = gui_fcolour;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"BCOLOUR") ||
        checkstring(ep, (unsigned char *)"BCOLOR")) {
        iret = gui_bcolour;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"PLAYING") ||
        checkstring(ep, (unsigned char *)"SOUND")) {
        strcpy((char *)sret, PlayingStr[CurrentlyPlaying]);
        CtoM(sret);
        targ = T_STR;
        return;
    }
    if (checkstring(ep, (unsigned char *)"VERSION")) {
        strcpy((char *)sret, MMBASIC_BANNER_NAME);
        CtoM(sret);
        targ = T_STR;
        return;
    }
    if (checkstring(ep, (unsigned char *)"HEAP") ||
        checkstring(ep, (unsigned char *)"MEMORY")) {
        iret = (int64_t)MMHeap;
        targ = T_INT;
        return;
    }
    iret = 0;
    targ = T_INT;
}
