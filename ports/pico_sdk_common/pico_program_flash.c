#include "pico_runtime_internal.h"

void stripcomment(char * p) {
    char * q = p;
    int toggle = 0;
    while (*q) {
        if (*q == '\'' && toggle == 0) {
            *q = 0;
            break;
        }
        if (*q == '"') toggle ^= 1;
        q++;
    }
}

// takes a pointer to RAM containing a program (in clear text) and writes it to memory in tokenised format
void MIPS16 SaveProgramToFlash(unsigned char * pm, int msg) {
    unsigned char *p, fontnbr, prevchar = 0, buf[STRINGSIZE];
    unsigned short endtoken, tkn;
    int nbr, i, j, n, SaveSizeAddr;
    bool continuation = false;
    multi = false;
    uint32_t storedupdates[MAXCFUNCTION], updatecount = 0, realflashsave;
    initFonts();
#ifdef rp2350
    __dsb();
#endif
    hal_keyboard_clear_repeat_state(); /* USB only — stub no-op */
    memcpy(buf, tknbuf, STRINGSIZE);   // save the token buffer because we are going to use it
    FlashWriteInit(PROGRAM_FLASH);
    hal_flash_erase(realflashpointer, MAX_PROG_SIZE);
    j = MAX_PROG_SIZE / 4;
    int * pp = (int *)(flash_progmemory);
    while (j--)
        if (*pp++ != 0xFFFFFFFF) {
            fileio_flash_write_end();
            error("Flash erase problem");
        }
    nbr = 0;
    // this is used to count the number of bytes written to flash
    while (*pm) {
    contloop:
        if (continuation) {
            p = &inpbuf[strlen((char *)inpbuf)];
            continuation = false;
        } else
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
        if (inpbuf[strlen((char *)inpbuf) - 1] == Option.continuation && inpbuf[strlen((char *)inpbuf) - 2] == ' ' && Option.continuation) {
            continuation = true;
            inpbuf[strlen((char *)inpbuf) - 2] = 0; //strip the continuation character
            goto contloop;
        }
        tokenise(false); // turn into executable code
        p = tknbuf;
        while (!(p[0] == 0 && p[1] == 0)) {
            FlashWriteByte(*p++);
            nbr++;

            if ((int)((char *)realflashpointer - (uint32_t)PROGSTART) >= MAX_PROG_SIZE - 5) goto exiterror;
        }
        FlashWriteByte(0);
        nbr++; // terminate that line in flash
    }
    FlashWriteByte(0);
    FlashWriteAlign(); // this will flush the buffer and step the flash write pointer to the next word boundary
    // now we must scan the program looking for CFUNCTION/CSUB/DEFINEFONT statements, extract their data and program it into the flash used by  CFUNCTIONs
    // programs are terminated with two zero bytes and one or more bytes of 0xff.  The CFunction area starts immediately after that.
    // the format of a CFunction/CSub/Font in flash is:
    //   Unsigned Int - Address of the CFunction/CSub in program memory (points to the token representing the "CFunction" keyword) or NULL if it is a font
    //   Unsigned Int - The length of the CFunction/CSub/Font in bytes including the Offset (see below)
    //   Unsigned Int - The Offset (in words) to the main() function (ie, the entry point to the CFunction/CSub).  Omitted in a font.
    //   word1..wordN - The CFunction/CSub/Font code
    // The next CFunction/CSub/Font starts immediately following the last word of the previous CFunction/CSub/Font
    int firsthex = 1;
    realflashsave = realflashpointer;
    p = (unsigned char *)flash_progmemory; // start scanning program memory
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
                    fileio_flash_write_end();
                    error("Cannot redefine fonts 1, 6 or 7");
                }
                realflashpointer += 4;
                skipelement(p); // go to the end of the command
                p--;
            } else {
                endtoken = GetCommandValue((unsigned char *)"End CSub");
                realflashpointer += 4;
                fontnbr = 0;
                firsthex = 0;
                p++;
            }
            SaveSizeAddr = realflashpointer; // save where we are so that we can write the CFun size in here
            realflashpointer += 4;
            p++;
            skipspace(p);
            if (!fontnbr) { //process CSub
                if (!isnamestart((uint8_t)*p)) {
                    fileio_flash_write_end();
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
                            fileio_flash_write_end();
                            error("Invalid hex word");
                        }
                        if ((int)((char *)realflashpointer - (uint32_t)PROGSTART) >= MAX_PROG_SIZE - 5) goto exiterror;
                        n = n << 4;
                        if (*p <= '9')
                            n |= (*p - '0');
                        else
                            n |= (toupper(*p) - 'A' + 10);
                        p++;
                    }
                    realflashpointer += 4;
                    skipspace(p);
                    if (firsthex) {
                        firsthex = 0;
                        if (((n >> 16) & 0xff) < 0x20) {
                            fileio_flash_write_end();
                            error("Can't define non-printing characters");
                        }
                    }
                }
                // we are at the end of a embedded code line
                while (*p) p++; // make sure that we move to the end of the line
                p++;            // step to the start of the next line
                if (*p == 0) {
                    fileio_flash_write_end();
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
            storedupdates[updatecount++] = realflashpointer - SaveSizeAddr - 4;
        }
        while (*p) p++; // look for the zero marking the start of the next element
    }
    realflashpointer = realflashsave;
    updatecount = 0;
    p = (unsigned char *)flash_progmemory; // start scanning program memory
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
                    fileio_flash_write_end();
                    error("Cannot redefine fonts 1, 6, or 7");
                }

                //FlashWriteWord(fontnbr - 1);                        // a low number (< FONT_TABLE_SIZE) marks the entry as a font
                // B31 = 1 now marks entry as font.
                FlashWriteByte(fontnbr - 1);
                FlashWriteByte(0x00);
                FlashWriteByte(0x00);
                FlashWriteByte(0x80);

                skipelement(p); // go to the end of the command
                p--;
            } else {
                endtoken = GetCommandValue((unsigned char *)"End CSub");
                FlashWriteWord((unsigned int)(p - flash_progmemory)); // if a CFunction/CSub save a relative pointer to the declaration
                fontnbr = 0;
                p++;
            }
            SaveSizeAddr = realflashpointer;              // save where we are so that we can write the CFun size in here
            FlashWriteWord(storedupdates[updatecount++]); // leave this blank so that we can later do the write
            p++;
            skipspace(p);
            if (!fontnbr) {
                if (!isnamestart((uint8_t)*p)) {
                    fileio_flash_write_end();
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
                            fileio_flash_write_end();
                            error("Invalid hex word");
                        }
                        if ((int)((char *)realflashpointer - (uint32_t)PROGSTART) >= MAX_PROG_SIZE - 5) goto exiterror;
                        n = n << 4;
                        if (*p <= '9')
                            n |= (*p - '0');
                        else
                            n |= (toupper(*p) - 'A' + 10);
                        p++;
                    }

                    FlashWriteWord(n);
                    skipspace(p);
                }
                // we are at the end of a embedded code line
                while (*p) p++; // make sure that we move to the end of the line
                p++;            // step to the start of the next line
                if (*p == 0) {
                    fileio_flash_write_end();
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
    FlashWriteWord(0xffffffff);                   // make sure that the end of the CFunctions is terminated with an erased word
    FlashWriteClose();                            // this will flush the buffer and step the flash write pointer to the next word boundary
    if (msg) {                                    // if requested by the caller, print an informative message
        if (MMCharPos > 1) MMPrintString("\r\n"); // message should be on a new line
        MMPrintString("Saved ");
        IntToStr((char *)tknbuf, nbr + 3, 10);
        MMPrintString((char *)tknbuf);
        MMPrintString(" bytes\r\n");
    }
    memcpy(tknbuf, buf, STRINGSIZE);   // restore the token buffer in case there are other commands in it
                                       //    initConsole();
    hal_keyboard_clear_repeat_state(); /* USB only — stub no-op */
    fileio_flash_write_end();
    return;

// we only get here in an error situation while writing the program to flash
exiterror:
    FlashWriteByte(0);
    FlashWriteByte(0);
    FlashWriteByte(0); // terminate the program in flash
    FlashWriteClose();
    error("Not enough memory");
}
