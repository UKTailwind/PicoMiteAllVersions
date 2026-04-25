/*
 * ports/pico_sdk_common/cmd_psram.c — RAM command (PSRAM slot
 * management) for RP2350 non-WEB variants.
 *
 * The feature is RP2350-only (requires the 8MB PSRAM block and the
 * mmap/psmap page tables); AllCommands.h's command table entry is
 * gated on `#ifdef rp2350 && !PICOMITEWEB`, so cmd_psram is never
 * referenced on other targets. Body-level ifdefs here are permissible
 * per the fixup-plan rules (port impl file).
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_flash.h"

#if defined(rp2350) && !HAL_PORT_HAS_WIFI

extern unsigned int mmap[HEAP_MEMORY_SIZE / PAGESIZE / PAGESPERWORD];
extern unsigned int psmap[7 * 1024 * 1024 / PAGESIZE / PAGESPERWORD];

/* Symbols not reachable through Hardware_Includes.h's PICOMITEWEB-gated
 * sections — declare here so this TU compiles under the rp2350+!WEB
 * gate. All are defined in core (FileIO.c / PicoMite.c). */
extern int MemLoadProgram(unsigned char *fname, unsigned char *ram);
extern const uint8_t *flash_target_contents;
extern const uint8_t *flash_progmemory;
extern unsigned char *LibMemory;

void MIPS16 cmd_psram(void)
{
    if (!PSRAMsize) error("PSRAM not enabled");
    unsigned char *p;
    if ((p = checkstring(cmdline, (unsigned char *)"ERASE ALL"))) {
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
        disable_interrupts_pico();
        hal_flash_erase(PROGSTART, MAX_PROG_SIZE);
        enable_interrupts_pico();
        j = (MAX_PROG_SIZE >> 2);
        uSec(250000);
        int *pp = (int *)flash_progmemory;
        while (j--)
            if (*pp++ != 0xFFFFFFFF) error("Erase error");
        disable_interrupts_pico();
        uint8_t *q = (uint8_t *)(PSRAMblock + ((i - 1) * MAX_PROG_SIZE));
        uint8_t *writebuff = GetTempMemory(4096);
        if (*q == 0xFF) {
            enable_interrupts_pico();
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
        enable_interrupts_pico();
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

#endif /* defined(rp2350) && !HAL_PORT_HAS_WIFI */
