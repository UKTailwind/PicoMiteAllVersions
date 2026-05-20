/*
 * MMBasic_REPL.c — extracted from PicoMite.c.
 *
 * The interactive prompt loop: setjmp landing for errors, autorun
 * handling, and the `while(1) { … tokenise(true); ExecuteProgram(tknbuf); … }`
 * cycle at the heart of interactive MMBasic.
 *
 * Both the device main() and the host REPL (host/host_main.c) call
 * MMBasic_RunPromptLoop() so they share identical behavior: `*foo`
 * run-shortcuts, drive-letter switching, MM.PROMPT customization,
 * RUN / FRUN / EDIT / AUTOSAVE special dispatch, PIN lock, etc.
 *
 * All hardware/platform-conditional branches remain guarded by the
 * same #ifdefs the code had when it lived in PicoMite.c.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Draw.h"
#include "hal/hal_keyboard.h"
#include "hal/hal_main_init.h"

#ifdef PC386_BOOT_TRACE
extern void kputs(const char *s);
#define PC386_TRACE(s) kputs(s)
#else
#define PC386_TRACE(s) ((void)0)
#endif

/* Defined in MMBasic_Prompt.c. */
extern int MMPromptPos;

/* Sign-on banner. Called once at entry by both PicoMite.c (device) and
 * host_main.c (host) so the text lives in one place. Device ports define
 * neither MMBASIC_BANNER_NAME nor MMBASIC_BANNER_TRAILER and fall back to
 * the runtime `banner` array (patched for rp2350a/b variants, etc.).
 * Host ports supply MMBASIC_BANNER_NAME (e.g. "PicoMite MMBasic Host")
 * via port_config.h; native host also supplies a trailer line. Copyright
 * trailer is shared (Version.h MMBASIC_COPYRIGHT). */
void MMBasic_PrintBanner(void) {
#ifdef MMBASIC_BANNER_NAME
    MMPrintString("\r" MMBASIC_BANNER_NAME " " VERSION "\r\n");
    MMPrintString(MMBASIC_COPYRIGHT);
#ifdef MMBASIC_BANNER_TRAILER
    MMPrintString(MMBASIC_BANNER_TRAILER);
#endif
#else
    extern char banner[];
    MMPrintString(banner);
    MMPrintString(MMBASIC_COPYRIGHT);
#endif
}

/* Token-table decoder — used to tell whether the line the user just typed
 * is a RUN / FRUN / EDIT / AUTOSAVE so we can treat them specially. */
CommandToken commandtbl_decode(const unsigned char *p){
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f)<<7);
}

/* `*filename [args...]` → `RUN "filename", args...` shortcut transform.
 * Moved here from PicoMite.c because the prompt loop is its only caller. */
void MIPS16 transform_star_command(char *input) {
    char *src = input;
    while (isspace((uint8_t)*src)) src++; // Skip leading whitespace.
    if (*src != '*') error("Internal fault");
    src++;

    // Trim any trailing whitespace from the input.
    char *end = input + strlen(input) - 1;
    while (isspace((uint8_t)*end)) *end-- = '\0';

    // Allocate extra space to avoid string overrun.
    char *tmp = (char *) GetTempMemory(STRINGSIZE + 32);
    strcpy(tmp, "RUN");
    char *dst = tmp + 3;

    if (*src == '"') {
        // Everything before the second quote is the name of the file to RUN.
        *dst++ = ' ';
        *dst++ = *src++; // Leading quote.
        while (*src && *src != '"') *dst++ = *src++;
        if (*src == '"') *dst++ = *src++; // Trailing quote.
    } else {
        // Everything before the first space is the name of the file to RUN.
        int count = 0;
        while (*src && !isspace((uint8_t)*src)) {
            if (++count == 1) {
                *dst++ = ' ';
                *dst++ = '\"';
            }
            *dst++ = *src++;
        }
        if (count) *dst++ = '\"';
    }

    while (isspace((uint8_t)*src)) src++; // Skip whitespace.

    // Anything else is arguments.
    if (*src) {
        *dst++ = ',';
        *dst++ = ' ';

        // If 'src' starts with double-quote then replace with: Chr$(34) +
        if (*src == '"') {
            memcpy(dst, "Chr$(34) + ", 11);
            dst += 11;
            src++;
        }

        *dst++ = '\"';

        // Copy from 'src' to 'dst'.
        while (*src) {
            if (*src == '"') {
                // Close current set of quotes to insert a Chr$(34)
                memcpy(dst, "\" + Chr$(34)", 12);
                dst += 12;

                // Open another set of quotes unless this was the last character.
                if (*(src + 1)) {
                    memcpy(dst, " + \"", 4);
                    dst += 4;
                }
                src++;
            } else {
                *dst++ = *src++;
            }
            if (dst - tmp >= STRINGSIZE) error("String too long");
        }

        // End with a double quote unless 'src' ended with one.
        if (*(src - 1) != '"') *dst++ = '\"';

        *dst = '\0';
    }

    if (dst - tmp >= STRINGSIZE) error("String too long");

    // Copy transformed string back into the input buffer.
    memcpy(input, tmp, STRINGSIZE);
    input[STRINGSIZE - 1] = '\0';

    ClearSpecificTempMemory(tmp);
}

/* MM.PROMPT recursion guard — was a function-static inside main(). */
static int ErrorInPrompt;

static void prompt_return_cleanup(void) {
    FlashLoad = 0;
    hal_keyboard_clear_repeat_state();
    ScrewUpTimer = 0;
    ProgMemory=(uint8_t *)flash_progmemory;
    ContinuePoint = nextstmt;
    *tknbuf = 0;
    optionangle=1.0;
    useoptionangle=false;
    WatchdogSet = false;
}

void MMBasic_RunPromptLoop(void) {
    int i = 0;
    char savewatchdog = WatchdogSet;

    if(setjmp(mark) != 0) {
     // we got here via a long jump which means an error or CTRL-C or the program wants to exit to the command prompt
        prompt_return_cleanup();
		char *ptr = findvar((unsigned char *)"MM.ENDLINE$", V_NOFIND_NULL);
        if(ptr && *ptr){
            CurrentLinePtr=0;
            memcpy(inpbuf,ptr,*ptr+1);
            *ptr=0;
            MtoC(inpbuf);
            *ptr=0;
            tokenise(true);
            goto autorun;
        }
    } else {
        PC386_TRACE("REPL:init, ");
        if(*ProgMemory == 0x01 ) ClearVars(0,true);
        else {
            ClearProgram(true);
        }
    PC386_TRACE("REPL:clear, ");
    /* WiFi-stack init + WebConnect: real impl in MMsetwifi.c, stub
     * no-op in MMweb_stubs.c so the call is unconditional here.
     * PicoMite SPI-LCD post-clear-program housekeeping (SPIatRisk
     * + Display_Refresh) sits behind another universal hook. */
    port_repl_wifi_arch_init_and_connect();
    PC386_TRACE("REPL:wifi, ");
    port_repl_post_clear_display_refresh();
    PC386_TRACE("REPL:refresh, ");
        PrepareProgram(true);
        PC386_TRACE("REPL:prepare1, ");
        if(FindSubFun((unsigned char *)"MM.STARTUP", 0) >= 0) {
            PC386_TRACE("REPL:startup, ");
            ExecuteProgram((unsigned char *)"MM.STARTUP\0");
            memset(inpbuf,0,STRINGSIZE);
        }
        PC386_TRACE("REPL:startup-check, ");
        if(Option.Autorun && _excep_code != RESTART_DOAUTORUN) {
            PC386_TRACE("REPL:autorun, ");
            ClearRuntime(true);
            PrepareProgram(true);
            if(*ProgMemory == 0x01 ){
                memset(tknbuf,0,STRINGSIZE);
                unsigned short tkn=GetCommandValue((unsigned char *)"RUN");
                tknbuf[0] = (tkn & 0x7f ) + C_BASETOKEN;
                tknbuf[1] = (tkn >> 7) + C_BASETOKEN; //tokens can be 14-bit
                goto autorun;
            }  else {
                Option.Autorun=0;
                SaveOptions();
            }
        }
    }
    while(1) {
    PC386_TRACE("REPL:loop, ");
    ApplyPromptConsoleColours();
    PC386_TRACE("REPL:colours, ");
    if(Option.DISPLAY_CONSOLE && CurrentX != 0) MMPrintString("\r\n");                    // prompt should be on a new line
        PC386_TRACE("REPL:newline1, ");
        MMAbort = false;
        BreakKey = BREAK_KEY;
        EchoOption = true;
        g_LocalIndex = 0;                                             // this should not be needed but it ensures that all space will be cleared
        ClearTempMemory();                                          // clear temp string space (might have been used by the prompt)
        PC386_TRACE("REPL:temp, ");
        CurrentLinePtr = NULL;                                      // do not use the line number in error reporting
        if(MMCharPos > 1) MMPrintString("\r\n");                    // prompt should be on a new line
        PC386_TRACE("REPL:newline2, ");
        while(Option.PIN && !IgnorePIN) {
            _excep_code = PIN_RESTART;
            if(Option.PIN == 99999999)                              // 99999999 is permanent lockdown
                MMPrintString("Console locked, press enter to restart: ");
            else
                MMPrintString("Enter PIN or 0 to restart: ");
            MMgetline(0, (char *)inpbuf);
            if(Option.PIN == 99999999) SoftReset();
            if(*inpbuf != 0) {
                uSec(3000000);
                i = atoi((char *)inpbuf);
                if(i == 0) SoftReset();
                if(i == Option.PIN) {
                    IgnorePIN = true;
                    break;
                }
            }
        }
        if(_excep_code!=POSSIBLE_WATCHDOG)_excep_code = 0;
        PrepareProgram(false);
        PC386_TRACE("REPL:prepare2, ");
        if(!ErrorInPrompt && FindSubFun((unsigned char *)"MM.PROMPT", 0) >= 0) {
            PC386_TRACE("REPL:mmprompt, ");
            ErrorInPrompt = true;
            ExecuteProgram((unsigned char *)"MM.PROMPT\0");
            MMPromptPos=MMCharPos-1;    //Save length of prompt
        } else{
            PC386_TRACE("REPL:prompt-print, ");
            MMPrintString("> ");                                    // print the prompt
            PC386_TRACE("REPL:prompt-ok, ");
            MMPromptPos=2;    //Save length of prompt
        }
        ErrorInPrompt = false;
        PC386_TRACE("REPL:edit, ");
        EditInputLine();
        PC386_TRACE("REPL:edit-return, ");
        //InsertLastcmd(inpbuf);                                  // save in case we want to edit it later
        if(!*inpbuf) continue;                                      // ignore an empty line
        char *p=(char *)inpbuf;
        skipspace(p);
//        executelocal(p);
        if(strlen(p)==2 && p[1]==':'){
            if(toupper(*p)=='A')strcpy(p,"drive \"a:\"");
            if(toupper(*p)=='B')strcpy(p,"drive \"b:\"");
            if(toupper(*p)=='C')strcpy(p,"drive \"c:\"");
        }
        if(*p=='*' && p[1]!='('){ //shortform RUN command so convert to a normal version
                transform_star_command((char *)inpbuf);
                p = (char *)inpbuf;
        }
        multi=false;
        tokenise(true);                                             // turn into executable code
autorun:
        i=0;
        WatchdogSet=savewatchdog;
        CommandToken tkn=commandtbl_decode(tknbuf);
        if(tkn==GetCommandValue((unsigned char *)"RUN") || tkn==GetCommandValue((unsigned char *)"FRUN") || tkn==GetCommandValue((unsigned char *)"EDIT") || tkn==GetCommandValue((unsigned char *)"AUTOSAVE"))i=1;
        if (setjmp(jmprun) != 0) {
            PrepareProgram(false);
            CurrentLinePtr = 0;
        }
        ExecuteProgram(tknbuf);                                     // execute the line straight away
        if(i){
            cmdline=NULL;
            do_end(false);
            longjmp(mark, 1);												// jump back to the input prompt
        }
        else {
            memset(inpbuf,0,STRINGSIZE);
	        longjmp(mark, 1);												// jump back to the input prompt
        }
	}
}
