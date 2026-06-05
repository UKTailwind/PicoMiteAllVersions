/*
 * ports/pico_sdk_common/defines_loader.c — compile-time DEFINES parser
 * and MemLoadProgram / FileLoadCMM2Program — RP2350-only.
 *
 * The DEFINES feature is gated on RP2350 via a_dlist / nDefines /
 * loadbuffsize scratch state. FileLoadCMM2Program
 * translates the legacy CMM2 ASCII format into tokenised MMBasic source
 * using the same pipeline. Both are only referenced from code already
 * gated on `#ifdef rp2350` (Commands.c CMM2mode branch, RAM LOAD
 * command in ports/pico_sdk_common/cmd_psram.c), so the file-wide gate
 * here matches.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "FileIO.h"

#if defined(rp2350)

extern int nDefines;
extern int LineCount;
extern a_dlist * dlist;

extern void mmbasic_chdir(char * p);
extern char fullpathname[][FF_MAX_LFN];
extern int FatFSFileSystem;
extern int FatFSFileSystemSave;
extern void getfullfilename(char * p, char * q);
extern void MMgetline(int fnbr, char * buff);

#define loadbuffsize EDIT_BUFFER_SIZE - sizeof(a_dlist) * MAXDEFINES - 4096
int cmpstr(char * s1, char * s2) {
    unsigned char * p1 = (unsigned char *)s1;
    unsigned char * p2 = (unsigned char *)s2;
    unsigned char c1, c2;

    if (p1 == p2)
        return 0;

    do {
        c1 = tolower(*p1++);
        c2 = tolower(*p2++);
        if (c1 == '\0') return 0;
    } while (c1 == c2);

    return c1 - c2;
}

int massage(char * buff) {
    int i = nDefines;
    while (i--) {
        char * p = dlist[i].from;
        while (*p) {
            *p = toupper(*p);
            p++;
        }
        p = dlist[i].to;
        while (*p) {
            *p = toupper(*p);
            p++;
        }
        STR_REPLACE(buff, dlist[i].from, dlist[i].to, 0);
    }
    STR_REPLACE(buff, "=<", "<=", 0);
    STR_REPLACE(buff, "=>", ">=", 0);
    STR_REPLACE(buff, " ,", ",", 0);
    STR_REPLACE(buff, ", ", ",", 0);
    STR_REPLACE(buff, " *", "*", 0);
    STR_REPLACE(buff, "* ", "*", 0);
    STR_REPLACE(buff, "- ", "-", 0);
    STR_REPLACE(buff, " /", "/", 0);
    STR_REPLACE(buff, "/ ", "/", 0);
    STR_REPLACE(buff, "= ", "=", 0);
    STR_REPLACE(buff, "+ ", "+", 0);
    STR_REPLACE(buff, " )", ")", 0);
    STR_REPLACE(buff, ") ", ")", 0);
    STR_REPLACE(buff, "( ", "(", 0);
    STR_REPLACE(buff, "> ", ">", 0);
    STR_REPLACE(buff, "< ", "<", 0);
    STR_REPLACE(buff, " '", "'", 0);
    return strlen(buff);
}
void importfile(char * pp, char * tp, char ** p, uint32_t buf, int convertdebug, bool message) {
    int fnbr;
    char buff[256];
    char qq[FF_MAX_LFN] = {0};
    int importlines = 0;
    int ignore = 0;
    char *fname, *sbuff, *op, *ip;
    int c, slen, data;
    fnbr = FindFreeFileNbr();
    char * q;
    if ((q = strchr((char *)tp, 34)) == 0) error("Syntax");
    q++;
    if ((q = strchr(q, 34)) == 0) error("Syntax");
    fname = (char *)getFstring((unsigned char *)tp);
    fnbr = FindFreeFileNbr();
    if (strchr((char *)fname, '.') == NULL) strcat((char *)fname, ".INC");
    q = &fname[strlen(fname) - 4];
    if (strcasecmp(q, ".inc") != 0) error("must be a .inc file");
    if (!(fname[1] == ':' && (fname[0] == 'A' || fname[0] == 'a' || fname[0] == 'B' || fname[0] == 'b'))) {
        strcpy(qq, pp);
        strcat(qq, fname);
    } else
        strcpy(qq, fname);
    if (message) {
        MMPrintString("Importing ");
        MMPrintString(qq);
        PRet();
    }
    if (!BasicFileOpen(qq, fnbr, FA_READ)) return;
    **p = '\'';
    *p += 1;
    **p = '#';
    *p += 1;
    strcpy(*p, qq);
    *p += strlen(qq);
    **p = '\r';
    *p += 1;
    **p = '\n';
    *p += 1;
    while (!FileEOF(fnbr)) {
        int toggle = 0, len = 0; // while waiting for the end of file
        sbuff = buff;
        if (((uint32_t)*p - buf) >= loadbuffsize) {
            FreeMemorySafe((void **)&buf);
            FreeMemorySafe((void **)&dlist);
            error("Not enough memory");
        }
        memset(buff, 0, 256);
        MMgetline(fnbr, (char *)buff); // get the input line
        data = 0;
        importlines++;
        LineCount++;
        routinechecks();
        len = strlen(buff);
        toggle = 0;
        for (c = 0; c < strlen(buff); c++) {
            if (buff[c] == TAB) buff[c] = ' ';
        }
        while (*sbuff == ' ') {
            sbuff++;
            len--;
        }
        if (ignore && sbuff[0] != '#') *sbuff = '\'';
        if (strncasecmp(sbuff, "rem ", 4) == 0 || (len == 3 && strncasecmp(sbuff, "rem", 3) == 0)) {
            sbuff += 2;
            *sbuff = '\'';
            continue;
        }
        if (strncasecmp(sbuff, "data ", 5) == 0) data = 1;
        slen = len;
        op = sbuff;
        ip = sbuff;
        while (*ip) {
            if (*ip == 34) {
                if (toggle == 0)
                    toggle = 1;
                else
                    toggle = 0;
            }
            if (!toggle && (*ip == ' ' || *ip == ':')) {
                *op++ = *ip++; //copy the first space
                while (*ip == ' ') {
                    ip++;
                    len--;
                }
            } else
                *op++ = *ip++;
        }
        slen = len;
        if (!(toupper(sbuff[0]) == 'R' && toupper(sbuff[1]) == 'U' && toupper(sbuff[2]) == 'N' && (strlen(sbuff) == 3 || sbuff[3] == ' '))) {
            toggle = 0;
            for (c = 0; c < slen; c++) {
                if (!(toggle || data)) sbuff[c] = toupper(sbuff[c]);
                if (sbuff[c] == 34) {
                    if (toggle == 0)
                        toggle = 1;
                    else
                        toggle = 0;
                }
            }
        }
        toggle = 0;
        for (c = 0; c < slen; c++) {
            if (sbuff[c] == 34) {
                if (toggle == 0)
                    toggle = 1;
                else
                    toggle = 0;
            }
            if (!toggle && sbuff[c] == 39 && len == slen) {
                len = c; //get rid of comments
                break;
            }
        }
        if (sbuff[0] == '#') {
            unsigned char * tp = checkstring((unsigned char *)&sbuff[1], (unsigned char *)"DEFINE");
            if (tp) {
                getargs(&tp, 3, (unsigned char *)",");
                if (nDefines >= MAXDEFINES) {
                    FreeMemorySafe((void *)&buf);
                    FreeMemorySafe((void *)&dlist);
                    error("Too many #DEFINE statements");
                }
                strcpy(dlist[nDefines].from, (char *)getCstring(argv[0]));
                strcpy(dlist[nDefines].to, (char *)getCstring(argv[2]));
                nDefines++;
                ClearTempMemory();
            } else {
                if (cmpstr("COMMENT END", &sbuff[1]) == 0) ignore = 0;
                if (cmpstr("COMMENT START", &sbuff[1]) == 0) ignore = 1;
                if (cmpstr("MMDEBUG ON", &sbuff[1]) == 0) convertdebug = 0;
                if (cmpstr("MMDEBUG OFF", &sbuff[1]) == 0) convertdebug = 1;
                if (cmpstr("INCLUDE ", &sbuff[1]) == 0) {
                    FreeMemorySafe((void **)&buf);
                    FreeMemorySafe((void **)&dlist);
                    error("Can't import from an import");
                }
            }
        } else {
            if (toggle) sbuff[len++] = 34;
            sbuff[len++] = 39;
            sbuff[len] = 0;
            len = massage(sbuff); //can't risk crushing lines with a quote in them
            if ((sbuff[0] != 39) || (sbuff[0] == 39 && sbuff[1] == 39)) {
                memcpy(*p, sbuff, len);
                *p += len;
                **p = '\n';
                *p += 1;
            }
        }
    }
    FileClose(fnbr);
    return;
}

// load a file into program memory
int FileLoadCMM2Program(char * fname, bool message) {
    int fnbr;
    char *p, *op, *ip, *buf, *sbuff, buff[STRINGSIZE];
    char pp[FF_MAX_LFN] = {0};
    int c;
    int convertdebug = 1;
    int ignore = 0;
    nDefines = 0;
    LineCount = 0;
    int importlines = 0, data;
    if (!InitSDCard()) return false;
    ClearProgram(true); // clear any leftovers from the previous program
    fnbr = FindFreeFileNbr();
    p = (char *)getFstring((unsigned char *)fname);
    if (strchr((char *)p, '.') == NULL) strcat((char *)p, ".bas");
    char q[FF_MAX_LFN] = {0};
    FatFSFileSystemSave = FatFSFileSystem;
    getfullfilename(p, q);
    int CurrentFileSystem = FatFSFileSystem;
    FatFSFileSystem = FatFSFileSystemSave;
    strcpy(pp, CurrentFileSystem ? "B:" : "A:");
    strcat(pp, fullpathname[FatFSFileSystem]);
    strcat(pp, "/");
    mmbasic_chdir(pp);
    if (!BasicFileOpen(p, fnbr, FA_READ)) return false;
    p = buf = GetMemory(loadbuffsize);
    *p++ = '\'';
    *p++ = '#';
    strcpy(p, CurrentFileSystem ? "B:" : "A:");
    p += 2;
    strcpy(p, q);
    p += strlen(q);
    *p++ = '\r';
    *p++ = '\n';
    dlist = GetMemory(sizeof(a_dlist) * MAXDEFINES);

    while (!FileEOF(fnbr)) {           // while waiting for the end of file
        int toggle = 0, len = 0, slen; // while waiting for the end of file
        sbuff = buff;
        if ((p - buf) >= loadbuffsize) {
            FreeMemorySafe((void **)&buf);
            FreeMemorySafe((void **)&dlist);
            error("Not enough memory");
        }
        memset(buff, 0, 256);
        MMgetline(fnbr, (char *)buff); // get the input line
        data = 0;
        importlines++;
        LineCount++;
        routinechecks();
        len = strlen(buff);
        toggle = 0;
        for (c = 0; c < strlen(buff); c++) {
            if (buff[c] == TAB) buff[c] = ' ';
        }
        while (sbuff[0] == ' ') { //strip leading spaces
            sbuff++;
            len--;
        }
        if (ignore && sbuff[0] != '#') *sbuff = '\'';
        if (strncasecmp(sbuff, "rem ", 4) == 0 || (len == 3 && strncasecmp(sbuff, "rem", 3) == 0)) {
            sbuff += 2;
            *sbuff = '\'';
            continue;
        }
        if (strncasecmp(sbuff, "mmdebug ", 7) == 0 && convertdebug == 1) {
            sbuff += 6;
            *sbuff = '\'';
            continue;
        }
        if (strncasecmp(sbuff, "data ", 5) == 0) data = 1;
        slen = len;
        op = sbuff;
        ip = sbuff;
        while (*ip) {
            if (*ip == 34) {
                if (toggle == 0)
                    toggle = 1;
                else
                    toggle = 0;
            }
            if (!toggle && (*ip == ' ' || *ip == ':')) {
                *op++ = *ip++; //copy the first space
                while (*ip == ' ') {
                    ip++;
                    len--;
                }
            } else
                *op++ = *ip++;
        }
        slen = len;
        if (sbuff[0] == '#') {
            unsigned char * tp = checkstring((unsigned char *)&sbuff[1], (unsigned char *)"DEFINE");
            if (tp) {
                getargs(&tp, 3, (unsigned char *)",");
                if (nDefines >= MAXDEFINES) {
                    FreeMemorySafe((void *)&buf);
                    FreeMemorySafe((void *)&dlist);
                    error("Too many #DEFINE statements");
                }
                strcpy(dlist[nDefines].from, (char *)getCstring(argv[0]));
                strcpy(dlist[nDefines].to, (char *)getCstring(argv[2]));
                nDefines++;
            } else {
                if (cmpstr("COMMENT END", &sbuff[1]) == 0) ignore = 0;
                if (cmpstr("COMMENT START", &sbuff[1]) == 0) ignore = 1;
                if (cmpstr("MMDEBUG ON", &sbuff[1]) == 0) convertdebug = 0;
                if (cmpstr("MMDEBUG OFF", &sbuff[1]) == 0) convertdebug = 1;
                if (cmpstr("INCLUDE", &sbuff[1]) == 0) {
                    importfile(pp, &sbuff[8], &p, (uint32_t)buf, convertdebug, message);
                    ClearTempMemory();
                }
            }
        } else {
            if (!(toupper(sbuff[0]) == 'R' && toupper(sbuff[1]) == 'U' && toupper(sbuff[2]) == 'N' && (strlen(sbuff) == 3 || sbuff[3] == ' '))) {
                toggle = 0;
                for (c = 0; c < slen; c++) {
                    if (!(toggle || data)) sbuff[c] = toupper(sbuff[c]);
                    if (sbuff[c] == 34) {
                        if (toggle == 0)
                            toggle = 1;
                        else
                            toggle = 0;
                    }
                }
            }
            toggle = 0;
            for (c = 0; c < slen; c++) {
                if (sbuff[c] == 34) {
                    if (toggle == 0)
                        toggle = 1;
                    else
                        toggle = 0;
                }
                if (!toggle && sbuff[c] == 39 && len == slen) {
                    len = c; //get rid of comments
                    break;
                }
            }
            if (toggle) sbuff[len++] = 34;
            sbuff[len++] = 39;
            sbuff[len] = 0;
            len = massage(sbuff); //can't risk crushing lines with a quote in them
            if ((sbuff[0] != 39) || (sbuff[0] == 39 && sbuff[1] == 39)) {
                memcpy(p, sbuff, len);
                p += len;
                *p++ = '\n';
            }
        }
    }
    *p = 0; // terminate the string in RAM
    FileClose(fnbr);
    unsigned char continuation = Option.continuation;
    SaveProgramToFlash((unsigned char *)buf, false);
    Option.continuation = continuation;
    FreeMemorySafe((void **)&buf);
    FreeMemorySafe((void **)&dlist);
    return true;
}

#else /* !defined(rp2350) */

/* rp2040 stub — Commands.c's do_run CMM2mode branch references this
 * unconditionally; returns failure so the BASIC command just exits
 * without loading. CMM2mode = false is the default caller, so
 * ordinary RUN is unaffected on rp2040. */
int FileLoadCMM2Program(char * fname, bool message) {
    (void)fname;
    (void)message;
    return 0;
}

#endif /* defined(rp2350) */
