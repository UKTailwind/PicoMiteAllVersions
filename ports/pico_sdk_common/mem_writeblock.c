/*
 * ports/pico_sdk_common/mem_writeblock.c — rp2350 PSRAM write-block
 * helpers + CMM2 legacy loader.
 *
 * The MemWriteBlock / MemWriteByte / MemWriteInt state machine stages
 * program-image writes into the 32-byte QSPI page units that RP2350
 * flash/PSRAM expects. cmd_loadCMM2 / cmd_RunCMM2 use the same pipeline
 * to translate the CMM2 (Colour Maximite 2) text format into MMBasic
 * tokenised source.
 *
 * All rp2350-only; RP2040 variants never reference these symbols
 * (AllCommands.h gates the CMM2 command-table entries on `#ifdef rp2350`).
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "FileIO.h"
#include "hal/hal_keyboard.h"

#if defined(rp2350)

/* Symbols defined in FileIO.c / PicoMite.c that this TU references.
 * MemWord / mi8p come from FileIO.h. */
extern int TraceOn;

volatile uint32_t realmempointer;
void MemWriteBlock(void) {
    int i;
    uint32_t address = realmempointer - 32;
    if (address % 32) error("Memory write address");
    memcpy((char *)address, (char *)&MemWord.i64[0], 32);
    for (i = 0; i < 8; i++) MemWord.i32[i] = 0xFFFFFFFF;
}
void MemWriteByte(unsigned char b) {
    realmempointer++;
    MemWord.i8[mi8p] = b;
    mi8p++;
    mi8p %= 32;
    if (mi8p == 0) {
        MemWriteBlock();
    }
}
void MemWriteWord(unsigned int i) {
    MemWriteByte(i & 0xFF);
    MemWriteByte((i >> 8) & 0xFF);
    MemWriteByte((i >> 16) & 0xFF);
    MemWriteByte((i >> 24) & 0xFF);
}

void MemWriteAlign(void) {
    while (mi8p != 0) {
        MemWriteByte(0x0);
    }
    MemWriteWord(0xFFFFFFFF);
}
void MemWriteClose(void) {
    while (mi8p != 0) {
        MemWriteByte(0xff);
    }
}

void MIPS16 SaveProgramToRAM(unsigned char * pm, int msg, uint8_t * ram) {
    unsigned char *p, fontnbr, prevchar = 0, buf[STRINGSIZE];
    unsigned short endtoken, tkn;
    int nbr, i, n, SaveSizeAddr;
    multi = false;
    uint32_t storedupdates[MAXCFUNCTION], updatecount = 0, realmemsave;
    initFonts();
    hal_keyboard_clear_repeat_state();
    memcpy(buf, tknbuf, STRINGSIZE); // save the token buffer because we are going to use it
    memset(ram, 0xFF, MAX_PROG_SIZE);
    realmempointer = (volatile uint32_t)ram;
    nbr = 0;
    // this is used to count the number of bytes written to ram
    while (*pm) {
        p = inpbuf;
        while (!(*pm == 0 || *pm == '\r' || (*pm == '\n' && prevchar != '\r'))) {
            if (*pm == TAB) {
                do {
                    *p++ = ' ';
                    if ((p - inpbuf) >= MAXSTRLEN) goto exiterror;
                } while ((p - inpbuf) % 2);
            } else {
                if (isprint((uint8_t)*pm)) {
                    *p++ = *pm;
                    if ((p - inpbuf) >= MAXSTRLEN) goto exiterror;
                }
            }
            prevchar = *pm++;
        }
        if (*pm) prevchar = *pm++; // step over the end of line char but not the terminating zero
        *p = 0;                    // terminate the string in inpbuf

        if (*inpbuf == 0 && (*pm == 0 || (!isprint((uint8_t)*pm) && pm[1] == 0))) break; // don't save a trailing newline

        tokenise(false); // turn into executable code
        p = tknbuf;
        while (!(p[0] == 0 && p[1] == 0)) {
            MemWriteByte(*p++);
            nbr++;

            if (((uint32_t)realmempointer - (uint32_t)ram) >= MAX_PROG_SIZE - 5) goto exiterror;
        }
        MemWriteByte(0);
        nbr++; // terminate that line in flash
    }
    MemWriteByte(0);
    MemWriteAlign(); // this will flush the buffer and step the flash write pointer to the next word boundary
    // now we must scan the program looking for CFUNCTION/CSUB/DEFINEFONT statements, extract their data and program it into the flash used by  CFUNCTIONs
    // programs are terminated with two zero bytes and one or more bytes of 0xff.  The CFunction area starts immediately after that.
    // the format of a CFunction/CSub/Font in flash is:
    //   Unsigned Int - Address of the CFunction/CSub in program memory (points to the token representing the "CFunction" keyword) or NULL if it is a font
    //   Unsigned Int - The length of the CFunction/CSub/Font in bytes including the Offset (see below)
    //   Unsigned Int - The Offset (in words) to the main() function (ie, the entry point to the CFunction/CSub).  Omitted in a font.
    //   word1..wordN - The CFunction/CSub/Font code
    // The next CFunction/CSub/Font starts immediately following the last word of the previous CFunction/CSub/Font
    int firsthex = 1;
    realmemsave = realmempointer;
    p = (unsigned char *)ram; // start scanning program memory
    while (*p != 0xff) {
        nbr++;
        if (*p == 0) p++;   // if it is at the end of an element skip the zero marker
        if (*p == 0) break; // end of the program
        if (*p == T_NEWLINE) {
            CurrentLinePtr = p;
            p++; // skip the newline token
        }
        if (*p == T_LINENBR) p += 3; // step over the line number

        skipspace(p);
        if (*p == T_LABEL) {
            p += p[1] + 2; // skip over the label
            skipspace(p);  // and any following spaces
        }
        tkn = p[0] & 0x7f;
        tkn |= ((unsigned short)(p[1] & 0x7f) << 7);
        if (tkn == cmdCSUB || tkn == GetCommandValue((unsigned char *)"DefineFont")) { // found a CFUNCTION, CSUB or DEFINEFONT token
            if (tkn == GetCommandValue((unsigned char *)"DefineFont")) {
                endtoken = GetCommandValue((unsigned char *)"End DefineFont");
                p += 2; // step over the token
                skipspace(p);
                if (*p == '#') p++;
                fontnbr = getint(p, 1, FONT_TABLE_SIZE);
                // font 6 has some special characters, some of which depend on font 1
                if (fontnbr == 1 || fontnbr == 6 || fontnbr == 7) {
                    error("Cannot redefine fonts 1, 6 or 7");
                }
                realmempointer += 4;
                skipelement(p); // go to the end of the command
                p--;
            } else {
                endtoken = GetCommandValue((unsigned char *)"End CSub");
                realmempointer += 4;
                fontnbr = 0;
                firsthex = 0;
                p++;
            }
            SaveSizeAddr = realmempointer; // save where we are so that we can write the CFun size in here
            realmempointer += 4;
            p++;
            skipspace(p);
            if (!fontnbr) { //process CSub
                if (!isnamestart((uint8_t)*p)) {
                    error("Function name");
                }
                do {
                    p++;
                } while (isnamechar((uint8_t)*p));
                skipspace(p);
                if (!(isxdigit((uint8_t)p[0]) && isxdigit((uint8_t)p[1]) && isxdigit((uint8_t)p[2]))) {
                    skipelement(p);
                    p++;
                    if (*p == T_NEWLINE) {
                        CurrentLinePtr = p;
                        p++; // skip the newline token
                    }
                    if (*p == T_LINENBR) p += 3; // skip over a line number
                }
            }
            do {
                while (*p && *p != '\'') {
                    skipspace(p);
                    n = 0;
                    for (i = 0; i < 8; i++) {
                        if (!isxdigit((uint8_t)*p)) {
                            error("Invalid hex word");
                        }
                        if (((uint32_t)realmempointer - (uint32_t)ram) >= MAX_PROG_SIZE - 5) goto exiterror;
                        n = n << 4;
                        if (*p <= '9')
                            n |= (*p - '0');
                        else
                            n |= (toupper(*p) - 'A' + 10);
                        p++;
                    }
                    realmempointer += 4;
                    skipspace(p);
                    if (firsthex) {
                        firsthex = 0;
                        if (((n >> 16) & 0xff) < 0x20) {
                            error("Can't define non-printing characters");
                        }
                    }
                }
                // we are at the end of a embedded code line
                while (*p) p++; // make sure that we move to the end of the line
                p++;            // step to the start of the next line
                if (*p == 0) {
                    error("Missing END declaration");
                }
                if (*p == T_NEWLINE) {
                    CurrentLinePtr = p;
                    p++; // skip the newline token
                }
                if (*p == T_LINENBR) p += 3; // skip over the line number
                skipspace(p);
                tkn = p[0] & 0x7f;
                tkn |= ((unsigned short)(p[1] & 0x7f) << 7);
            } while (tkn != endtoken);
            storedupdates[updatecount++] = realmempointer - SaveSizeAddr - 4;
        }
        while (*p) p++; // look for the zero marking the start of the next element
    }
    realmempointer = realmemsave;
    updatecount = 0;
    p = (unsigned char *)ram; // start scanning program memory
    while (*p != 0xff) {
        nbr++;
        if (*p == 0) p++;   // if it is at the end of an element skip the zero marker
        if (*p == 0) break; // end of the program
        if (*p == T_NEWLINE) {
            CurrentLinePtr = p;
            p++; // skip the newline token
        }
        if (*p == T_LINENBR) p += 3; // step over the line number

        skipspace(p);
        if (*p == T_LABEL) {
            p += p[1] + 2; // skip over the label
            skipspace(p);  // and any following spaces
        }
        tkn = p[0] & 0x7f;
        tkn |= ((unsigned short)(p[1] & 0x7f) << 7);
        if (tkn == cmdCSUB || tkn == GetCommandValue((unsigned char *)"DefineFont")) { // found a CFUNCTION, CSUB or DEFINEFONT token
            if (tkn == GetCommandValue((unsigned char *)"DefineFont")) {               // found a CFUNCTION, CSUB or DEFINEFONT token
                endtoken = GetCommandValue((unsigned char *)"End DefineFont");
                p += 2; // step over the token
                skipspace(p);
                if (*p == '#') p++;
                fontnbr = getint(p, 1, FONT_TABLE_SIZE);
                // font 6 has some special characters, some of which depend on font 1
                if (fontnbr == 1 || fontnbr == 6 || fontnbr == 7) {
                    error("Cannot redefine fonts 1, 6, or 7");
                }

                //FlashWriteWord(fontnbr - 1);                        // a low number (< FONT_TABLE_SIZE) marks the entry as a font
                // B31 = 1 now marks entry as font.
                MemWriteByte(fontnbr - 1);
                MemWriteByte(0x00);
                MemWriteByte(0x00);
                MemWriteByte(0x80);

                skipelement(p); // go to the end of the command
                p--;
            } else {
                endtoken = GetCommandValue((unsigned char *)"End CSub");
                MemWriteWord((unsigned int)(p - ram)); // if a CFunction/CSub save a relative pointer to the declaration
                fontnbr = 0;
                p++;
            }
            SaveSizeAddr = realmempointer;              // save where we are so that we can write the CFun size in here
            MemWriteWord(storedupdates[updatecount++]); // leave this blank so that we can later do the write
            p++;
            skipspace(p);
            if (!fontnbr) {
                if (!isnamestart((uint8_t)*p)) {
                    error("Function name");
                }
                do {
                    p++;
                } while (isnamechar(*p));
                skipspace(p);
                if (!(isxdigit(p[0]) && isxdigit(p[1]) && isxdigit(p[2]))) {
                    skipelement(p);
                    p++;
                    if (*p == T_NEWLINE) {
                        CurrentLinePtr = p;
                        p++; // skip the newline token
                    }
                    if (*p == T_LINENBR) p += 3; // skip over a line number
                }
            }
            do {
                while (*p && *p != '\'') {
                    skipspace(p);
                    n = 0;
                    for (i = 0; i < 8; i++) {
                        if (!isxdigit(*p)) {
                            error("Invalid hex word");
                        }
                        if (((uint32_t)realmempointer - (uint32_t)ram) >= MAX_PROG_SIZE - 5) goto exiterror;
                        n = n << 4;
                        if (*p <= '9')
                            n |= (*p - '0');
                        else
                            n |= (toupper(*p) - 'A' + 10);
                        p++;
                    }

                    MemWriteWord(n);
                    skipspace(p);
                }
                // we are at the end of a embedded code line
                while (*p) p++; // make sure that we move to the end of the line
                p++;            // step to the start of the next line
                if (*p == 0) {
                    error("Missing END declaration");
                }
                if (*p == T_NEWLINE) {
                    CurrentLinePtr = p;
                    p++; // skip the newline token
                }
                if (*p == T_LINENBR) p += 3; // skip over a line number
                skipspace(p);
                tkn = p[0] & 0x7f;
                tkn |= ((unsigned short)(p[1] & 0x7f) << 7);
            } while (tkn != endtoken);
        }
        while (*p) p++; // look for the zero marking the start of the next element
    }
    MemWriteWord(0xffffffff);                     // make sure that the end of the CFunctions is terminated with an erased word
    MemWriteClose();                              // this will flush the buffer and step the flash write pointer to the next word boundary
    if (msg) {                                    // if requested by the caller, print an informative message
        if (MMCharPos > 1) MMPrintString("\r\n"); // message should be on a new line
        MMPrintString("Saved ");
        IntToStr((char *)tknbuf, nbr + 3, 10);
        MMPrintString((char *)tknbuf);
        MMPrintString(" bytes\r\n");
    }
    memcpy(tknbuf, buf, STRINGSIZE); // restore the token buffer in case there are other commands in it
                                     //    initConsole();
    hal_keyboard_clear_repeat_state();
    return;

// we only get here in an error situation while writing the program to flash
exiterror:
    MemWriteByte(0);
    MemWriteByte(0);
    MemWriteByte(0); // terminate the program in flash
    MemWriteClose();
    error("Not enough memory");
}
int MemLoadProgram(unsigned char * fname, unsigned char * ram) {
    int fnbr;
    char *p, *buf;
    int c, oldfont = gui_font;
    if (!InitSDCard()) return false;
    initFonts();
    m_alloc(M_LIMITED); // init the variables for program memory
    if (Option.DISPLAY_TYPE >= VIRTUAL && WriteBuf) FreeMemorySafe((void **)&WriteBuf);
    ClearRuntime(false);
    PSize = 0;
    StartEditPoint = NULL;
    StartEditChar = 0;
    ProgramChanged = false;
    TraceOn = false;
    SetFont(oldfont);
    PromptFont = oldfont;
    fnbr = FindFreeFileNbr();
    p = (char *)getFstring(fname);
    if (strchr((char *)p, '.') == NULL) strcat((char *)p, ".bas");
    char q[FF_MAX_LFN] = {0};
    FatFSFileSystemSave = FatFSFileSystem;
    getfullfilename(p, q);
    int CurrentFileSystem = FatFSFileSystem;
    FatFSFileSystem = FatFSFileSystemSave;
    if (!BasicFileOpen(p, fnbr, FA_READ)) return false;
    p = buf = GetTempMemory(EDIT_BUFFER_SIZE - 2048); // get all the memory while leaving space for the couple of buffers defined and the file handle
    *p++ = '\'';
    *p++ = '#';
    strcpy(p, CurrentFileSystem ? "B:" : "A:");
    p += 2;
    strcpy(p, q);
    p += strlen(q);
    *p++ = '\r';
    *p++ = '\n';
    while (!FileEOF(fnbr)) { // while waiting for the end of file
        if ((p - buf) >= EDIT_BUFFER_SIZE - 2048 - 512)
            error("Not enough memory");
        c = FileGetChar(fnbr) & 0x7f;
        if (isprint(c) || c == '\r' || c == '\n' || c == TAB) {
            if (c == TAB)
                c = ' ';
            *p++ = c; // get the input into RAM
        }
    }
    *p = 0; // terminate the string in RAM
    FileClose(fnbr);
    ClearSavedVars(); // clear any saved variables
    SaveProgramToRAM((unsigned char *)buf, false, ram);
    return true;
}

void MIPS16 loadCMM2(unsigned char * p, bool autorun, bool message) {
    getargs(&p, 1, (unsigned char *)",");
    if (!(argc & 1) || argc == 0)
        error("Syntax");
    if (CurrentLinePtr != NULL && !autorun)
        error("Invalid in a program");

    if (!FileLoadCMM2Program((char *)argv[0], message))
        return;
    FlashLoad = 0;
    if (autorun) {
        if (*ProgMemory != 0x01)
            return; // no program to run
        ClearRuntime(true);
        WatchdogSet = false;
        PrepareProgram(true);
        IgnorePIN = false;
        if (Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE) ExecuteProgram(ProgMemory - Option.LIBRARY_FLASH_SIZE); // run anything that might be in the library
        nextstmt = ProgMemory;
    }
    return;
}
/*  @endcond */

void MIPS16 cmd_loadCMM2(void) {
    bool autorun = false;
    getargs(&cmdline, 3, (unsigned char *)",");
    if (argc == 3) {
        if (toupper(*argv[2]) == 'R')
            autorun = true;
        else
            error("Syntax");
    } else if (CurrentLinePtr != NULL)
        error("Invalid in a program");

    loadCMM2(argv[0], autorun, true);
}

#endif /* defined(rp2350) */
