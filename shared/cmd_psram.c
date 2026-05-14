/*
 * shared/cmd_psram.c — `RAM` command (PSRAM slot management + march
 * test) implementation, shared across every port that exposes the
 * command through its port_tokens.h table.
 *
 * Hardware-clean: all cache and nocache-alias work routes through
 * hal/hal_psram.h. The runtime gate is `if (!PSRAMsize)`; ports that
 * don't expose PSRAM keep PSRAMsize at 0 and the gate fires before any
 * HAL call. The `RAM TEST NOCACHE` modifier is supported only on ports
 * whose hal_psram_nocache_alias() returns non-NULL; everywhere else the
 * command errors with "NOCACHE not supported on this port".
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_flash.h"
#include "hal/hal_psram.h"

/* Symbols not reachable through Hardware_Includes.h's PICOMITEWEB-gated
 * sections — declare here so this TU compiles across every port. All
 * are defined in core (FileIO.c / PicoMite.c). */
extern int MemLoadProgram(unsigned char *fname, unsigned char *ram);
extern const uint8_t *flash_target_contents;
extern const uint8_t *flash_progmemory;
extern unsigned char *LibMemory;

static void psram_test_fail(const char *phase, uintptr_t addr, uint32_t expected, uint32_t actual)
{
    char msg[160];
    snprintf(msg, sizeof(msg),
             "RAM TEST FAIL %s addr=&H%08lx expected=&H%08lx actual=&H%08lx\r\n",
             phase, (unsigned long)addr, (unsigned long)expected, (unsigned long)actual);
    MMPrintString(msg);
    error("PSRAM test failed");
}

static uint32_t psram_test_pattern(uintptr_t addr, size_t index)
{
    uint32_t x = (uint32_t)addr ^ (uint32_t)(index * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    return x ^ 0xA5A55A5Au;
}

typedef enum {
    PSRAM_TEST_CACHED,
    PSRAM_TEST_NOCACHE
} psram_test_mode_t;

static void psram_test_barrier(psram_test_mode_t mode)
{
    if (mode == PSRAM_TEST_CACHED) {
        hal_psram_cache_sync();
    }
    /* No-cache writes bypass the cache by definition, so there's no
     * cache state to flush between phases. The volatile pointer in the
     * test loop already orders the accesses. */
}

static void psram_march_test(uint8_t *base, size_t bytes, psram_test_mode_t mode)
{
    volatile uint32_t *mem = (volatile uint32_t *)base;
    size_t words = bytes / sizeof(uint32_t);
    hal_psram_cache_sync();
    MMPrintString("RAM TEST phase 0 fill zero\r\n");
    for (size_t i = 0; i < words; i++) mem[i] = 0u;
    psram_test_barrier(mode);

    MMPrintString("RAM TEST phase 1 up zero->ones\r\n");
    for (size_t i = 0; i < words; i++) {
        uint32_t actual = mem[i];
        if (actual != 0u) psram_test_fail("up0", (uintptr_t)&mem[i], 0u, actual);
        mem[i] = 0xFFFFFFFFu;
    }
    psram_test_barrier(mode);

    MMPrintString("RAM TEST phase 2 up ones->pattern\r\n");
    for (size_t i = 0; i < words; i++) {
        uint32_t actual = mem[i];
        if (actual != 0xFFFFFFFFu) psram_test_fail("up1", (uintptr_t)&mem[i], 0xFFFFFFFFu, actual);
        mem[i] = psram_test_pattern((uintptr_t)&mem[i], i);
    }
    psram_test_barrier(mode);

    MMPrintString("RAM TEST phase 3 down pattern->inverse\r\n");
    for (size_t i = words; i-- > 0;) {
        uint32_t expected = psram_test_pattern((uintptr_t)&mem[i], i);
        uint32_t actual = mem[i];
        if (actual != expected) psram_test_fail("downp", (uintptr_t)&mem[i], expected, actual);
        mem[i] = ~expected;
    }
    psram_test_barrier(mode);

    MMPrintString("RAM TEST phase 4 down inverse->zero\r\n");
    for (size_t i = words; i-- > 0;) {
        uint32_t expected = ~psram_test_pattern((uintptr_t)&mem[i], i);
        uint32_t actual = mem[i];
        if (actual != expected) psram_test_fail("downi", (uintptr_t)&mem[i], expected, actual);
        mem[i] = 0u;
    }
    psram_test_barrier(mode);

    MMPrintString("RAM TEST phase 5 final zero\r\n");
    for (size_t i = 0; i < words; i++) {
        uint32_t actual = mem[i];
        if (actual != 0u) psram_test_fail("final0", (uintptr_t)&mem[i], 0u, actual);
    }
}

void MIPS16 cmd_psram(void)
{
    if (!PSRAMsize) error("PSRAM not enabled");
    unsigned char *p;
    if ((p = checkstring(cmdline, (unsigned char *)"TEST"))) {
        size_t bytes = PSRAMsize;
        uint8_t *test_base = (uint8_t *)PSRAMbase;
        psram_test_mode_t mode = PSRAM_TEST_CACHED;
        skipspace(p);
        unsigned char *tp;
        unsigned char *nocache_kw = NULL;
        if ((tp = checkstring(p, (unsigned char *)"NOCACHE"))) {
            nocache_kw = tp;
        } else if ((tp = checkstring(p, (unsigned char *)"NC"))) {
            nocache_kw = tp;
        }
        if (nocache_kw) {
            uint8_t *alias = hal_psram_nocache_alias((uint8_t *)PSRAMbase);
            if (alias == NULL) error("NOCACHE not supported on this port");
            test_base = alias;
            mode = PSRAM_TEST_NOCACHE;
            p = nocache_kw;
            skipspace(p);
        }
        /* Maximum scan span: heap region + the 0x60000 gap + slot region.
         * Per-port PSRAMblocksize sizes the slot region (1.75 MB on
         * RP2350, MAXRAMSLOTS*MAX_PROG_SIZE on ESP32). Anything beyond
         * that walks past the slab the HAL handed us and either reads
         * unrelated ESP-IDF heap pages or faults on unmapped PSRAM. */
        const size_t psram_total = (size_t)PSRAMsize + 0x60000u + (size_t)PSRAMblocksize;
        if (*p) {
            if (checkstring(p, (unsigned char *)"ALL")) {
                bytes = psram_total;
            } else {
                int max_mb = (int)(psram_total / (1024u * 1024u));
                if (max_mb < 1) max_mb = 1;
                bytes = (size_t)getint(p, 1, max_mb) * 1024u * 1024u;
            }
        }
        char msg[96];
        snprintf(msg, sizeof(msg), "RAM TEST START base=&H%08lx bytes=%lu\r\n",
                 (unsigned long)test_base, (unsigned long)bytes);
        MMPrintString(msg);
        psram_march_test(test_base, bytes, mode);
        MMPrintString("RAM TEST OK\r\n");
    } else if ((p = checkstring(cmdline, (unsigned char *)"ERASE ALL"))) {
        memset((void *)PSRAMblock, 0, PSRAMblocksize);
    } else if ((p = checkstring(cmdline, (unsigned char *)"ERASE"))) {
        int i = getint(p, 1, MAXRAMSLOTS);
        uint8_t *j = (uint8_t *)PSRAMblock + ((i - 1) * MAX_PROG_SIZE);
        memset(j, 0, MAX_PROG_SIZE);
    } else if ((p = checkstring(cmdline, (unsigned char *)"OVERWRITE"))) {
        int i = getint(p, 1, MAXRAMSLOTS);
        uint8_t *j = (uint8_t *)PSRAMblock + ((i - 1) * MAX_PROG_SIZE);
        memset(j, 0, MAX_PROG_SIZE);
        uint8_t *q = ProgMemory;
        memcpy(j, q, MAX_PROG_SIZE);
    } else if ((p = checkstring(cmdline, (unsigned char *)"LIST"))) {
        int j, i, k;
        int *pp;
        getargs(&p, 3, (unsigned char *)",");
        if (argc) {
            int i = getint(argv[0], 1, MAXRAMSLOTS);
            ProgMemory = (uint8_t *)PSRAMblock + ((i - 1) * MAX_PROG_SIZE);
            if (Option.DISPLAY_CONSOLE && (SPIREAD || Option.NoScroll)) {
                ClearScreen(gui_bcolour);
                CurrentX = 0;
                CurrentY = 0;
            }
            if (argc == 1)
                ListProgram(ProgMemory, false);
            else if (argc == 3 && checkstring(argv[2], (unsigned char *)"ALL"))
                ListProgram(ProgMemory, true);
            else
                error("Syntax");
            ProgMemory = (unsigned char *)flash_progmemory;
        } else {
            int n = MAXRAMSLOTS;
            for (i = 1; i <= n; i++) {
                k = 0;
                j = MAX_PROG_SIZE >> 2;
                pp = (int *)(PSRAMblock + ((i - 1) * MAX_PROG_SIZE));
                while (j--)
                    if (*pp++ != 0x0) {
                        char buff[STRINGSIZE] = {0};
                        MMPrintString(" RAM Slot ");
                        PInt(i);
                        MMPrintString(" in use");
                        pp--;
                        if ((unsigned char)*pp == T_NEWLINE) {
                            char *p = (char *)pp;
                            MMPrintString(": \"");
                            buff[0] = '\'';
                            buff[1] = '#';
                            while (buff[0] == '\'' && buff[1] == '#')
                                p = (char *)llist((unsigned char *)buff, (unsigned char *)p);
                            MMPrintString(buff);
                            MMPrintString("\"\r\n");
                        } else {
                            MMPrintString("\r\n");
                        }
                        k = 1;
                        break;
                    }
                if (k == 0) {
                    MMPrintString(" RAM Slot ");
                    PInt(i);
                    MMPrintString(" available\r\n");
                }
            }
        }
    } else if ((p = checkstring(cmdline, (unsigned char *)"FILE LOAD"))) {
        int overwrite = 0;
        getargs(&p, 5, (unsigned char *)",");
        if (!(argc == 3 || argc == 5)) error("Syntax");
        int i = getint(argv[0], 1, MAXRAMSLOTS);
        if (argc == 5) {
            if (checkstring(argv[4], (unsigned char *)"O") ||
                checkstring(argv[4], (unsigned char *)"OVERWRITE"))
                overwrite = 1;
            else
                error("Syntax");
        }
        uint8_t *c = (uint8_t *)(PSRAMblock + ((i - 1) * MAX_PROG_SIZE));
        if (*c != 0x0 && overwrite == 0) error("Already programmed");
        memset(c, 0xFF, MAX_PROG_SIZE);
        ClearTempMemory();
        SaveContext();
        MemLoadProgram(argv[2], c);
        RestoreContext(false);
    } else if ((p = checkstring(cmdline, (unsigned char *)"SAVE"))) {
        int i = getint(p, 1, MAXRAMSLOTS);
        uint8_t *c = (uint8_t *)(PSRAMblock + ((i - 1) * MAX_PROG_SIZE));
        if (*c != 0x0) error("Already programmed");
        uint8_t *q = ProgMemory;
        memcpy(c, q, MAX_PROG_SIZE);
    } else if ((p = checkstring(cmdline, (unsigned char *)"LOAD"))) {
        if (CurrentLinePtr) error("Invalid in program");
        int j = (Option.PROG_FLASH_SIZE >> 2), i = getint(p, 1, MAXRAMSLOTS);
        fileio_flash_write_begin();
        hal_flash_erase(PROGSTART, MAX_PROG_SIZE);
        fileio_flash_write_end();
        j = (MAX_PROG_SIZE >> 2);
        uSec(250000);
        int *pp = (int *)flash_progmemory;
        while (j--)
            if (*pp++ != 0xFFFFFFFF) error("Erase error");
        fileio_flash_write_begin();
        uint8_t *q = (uint8_t *)(PSRAMblock + ((i - 1) * MAX_PROG_SIZE));
        uint8_t *writebuff = GetTempMemory(4096);
        if (*q == 0xFF) {
            fileio_flash_write_end();
            FlashWriteInit(PROGRAM_FLASH);
            hal_flash_erase(realflashpointer, MAX_PROG_SIZE);
            FlashWriteByte(0);
            FlashWriteByte(0);
            FlashWriteByte(0);
            FlashWriteClose();
            error("Flash slot empty");
        }
        for (int k = 0; k < MAX_PROG_SIZE; k += 4096) {
            for (int j = 0; j < 4096; j++) writebuff[j] = *q++;
            hal_flash_program((PROGSTART + k), writebuff, 4096);
        }
        fileio_flash_write_end();
        FlashLoad = 0;
    } else if ((p = checkstring(cmdline, (unsigned char *)"CHAIN"))) {
        if (!CurrentLinePtr) error("Invalid at command prompt");
        int i = getint(p, 0, MAXRAMSLOTS);
        if (i)
            ProgMemory = (unsigned char *)(PSRAMblock + ((i - 1) * MAX_PROG_SIZE));
        else
            ProgMemory = (unsigned char *)(flash_target_contents + MAXFLASHSLOTS * MAX_PROG_SIZE);
        FlashLoad = i;
        PrepareProgram(true);
        nextstmt = (unsigned char *)ProgMemory;
    } else if ((p = checkstring(cmdline, (unsigned char *)"RUN"))) {
        int i = getint(p, 0, MAXRAMSLOTS);
        if (i)
            ProgMemory = (unsigned char *)(uint8_t *)PSRAMblock + ((i - 1) * MAX_PROG_SIZE);
        else
            ProgMemory = (unsigned char *)(flash_target_contents + MAXFLASHSLOTS * MAX_PROG_SIZE);
        ClearRuntime(true);
        FlashLoad = i;
        PrepareProgram(true);
        if (Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE) ExecuteProgram(LibMemory);
        nextstmt = (unsigned char *)ProgMemory;
    } else {
        error("Syntax");
    }
}
