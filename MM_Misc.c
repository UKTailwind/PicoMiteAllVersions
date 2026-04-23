/***********************************************************************************************************************
PicoMite MMBasic

MM_Misc.c

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


#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include <time.h>
#include "picomite_gpio_irq.h"
//#include "upng.h"
#include <complex.h>
#include "pico/bootrom.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/pwm.h"
#include "hardware/dma.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hal/hal_flash.h"
#include "hal/hal_time.h"
#include "hal/hal_pin.h"
#include "hal/hal_keyboard.h"
#include "hardware/regs/addressmap.h"     /* XIP_BASE */
#include "hardware/spi.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include <malloc.h>
#include "xregex.h"
#include "hardware/structs/pwm.h"
#include "aes.h"

/* PicoCalc HW hooks — real impls in ports/pico_sdk_common/picocalc_features.c
 * (PICOCALC builds) and host/host_runtime.c (host stubs). */
extern void port_picocalc_set_keyboard_backlight(int level);
extern int  port_picocalc_battery_pct(void);
extern int  port_picocalc_is_charging(void);
extern void port_picocalc_factory_reset_options(void);
extern void port_print_supported_boards(void);
extern int  port_factory_reset_board(unsigned char *p);
extern int  port_display_option_setter(unsigned char *cmdline);
extern void port_print_display_options(void);
extern void port_print_lcd_spi(void);
extern int  port_keyboard_option_setter(unsigned char *cmdline);
extern void port_print_keyboard_heartbeat(void);
extern void port_print_usb_kb_repeat(void);
extern void port_clear_lcd_spi_if_shares_system(void);
/* PICOMITE+rp2350 only — defined in ports/pico_rp2350/port_defaults.c.
 * Caller is gated `#if defined(PICOMITE) && defined(rp2350)`. */
extern void disable_lcdspi(void);
/* Port-specific pin alias maps for MM.PINNO / MM.PIN. PICOMITEWEB
 * exposes virtual GP23/24/25/29 → 41-44 (CYW43-reserved); HDMI
 * exposes GP12-19 → 16-25 (HDMI-reserved). Other ports return 0/NULL. */
extern int  port_pinno_alias_for_name(const char *name);
extern int  port_pin_is_reserved_alias(int pin);
extern const char *port_pin_reserved_label(int pin);
extern int  port_lcd320_option_setter(unsigned char *cmdline);
extern int  port_misc_option_setter(unsigned char *cmdline);
extern int  port_pico_pins_option_setter(unsigned char *cmdline);
extern int  port_heartbeat_option_setter(unsigned char *cmdline);
extern int  port_system_lcd_spi_option_setter(unsigned char *cmdline);
extern int  port_audio_i2s_pio_slice(int pin1, int pin2);
extern int  port_mminfo_interrupts(int64_t *out_iret);
extern int  port_mminfo_touch_status(unsigned char *out_sret);
extern int  port_mminfo_scroll_start(int64_t *out_iret);
extern int  port_mminfo_screenbuff(int64_t *out_iret);
extern PIO  port_pio_for_index(int pio_idx);
extern int  port_poke_display_panel(unsigned char *p);
extern void port_apply_default_console_colors(int default_fc, int default_bc);
extern void port_web_print_options(void);
extern int  port_web_option_setter(unsigned char *cmdline);
extern int  port_web_mminfo(unsigned char *ep, int64_t *out_iret,
                            unsigned char *out_sret, int *out_targ);
extern int  port_web_get_ssid(unsigned char *out_sret, int *out_targ);
/* TimeOffsetToUptime defined in mm_misc_shared.c (also used by vm_sys_time.c) */
extern int64_t TimeOffsetToUptime;
extern int last_adc;
extern char banner[];
extern char *pinsearch(int pin);
extern uint8_t getrnd(void);
extern uint32_t restart_reason;
extern unsigned int b64d_size(unsigned int in_size);
extern unsigned int b64e_size(unsigned int in_size);
extern unsigned int b64_encode(const unsigned char* in, unsigned int in_len, unsigned char* out);
extern unsigned int b64_decode(const unsigned char* in, unsigned int in_len, unsigned char* out);
void parselongAES(uint8_t *p, int ivadd, uint8_t *keyx, uint8_t *ivx, int64_t **inint, int64_t **outint);
/* SSD1963data is defined in SSD1963.c on PICO/PICORP2350/PICOUSB/
 * PICOUSBRP2350/WEB/WEBRP2350; ports/vga{,_rp2350}/port_defaults.c +
 * ports/hdmi_rp2350/port_defaults.c stub it so the OPTION LCDPANEL
 * DISABLE reset path can write 0 unconditionally. */
extern int SSD1963data;
uint32_t getTotalHeap(void) {
   extern char __StackLimit, __bss_end__;
   
   return &__StackLimit  - &__bss_end__;
}

uint32_t getFreeHeap(void) {
   struct mallinfo m = mallinfo();

   return getTotalHeap() - m.uordblks;
}

uint32_t getProgramSize(void) {
   extern char __flash_binary_start, __flash_binary_end;

   return &__flash_binary_end - &__flash_binary_start;
}

uint32_t getFreeProgramSpace() {
   return PICO_FLASH_SIZE_BYTES - getProgramSize();
}
static inline CommandToken commandtbl_decode(const unsigned char *p){
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f)<<7);
}
extern int busfault;
//#include "pico/stdio_usb/reset_interface.h"
const char *OrientList[] = {"LANDSCAPE", "PORTRAIT", "RLANDSCAPE", "RPORTRAIT"};
const char *KBrdList[] = {"", "US", "FR", "GR", "IT", "BE", "UK", "ES" };
extern const void * const CallTable[];
struct s_inttbl inttbl[NBRINTERRUPTS];
unsigned char *InterruptReturn;
extern const char *FErrorMsg[];
uint8_t *buff320=NULL;
/* MQTT state globals. Defined on every target so MMBasic's interrupt
 * dispatch (which runs in core) can reference them without gating.
 * Only PICOMITEWEB actually populates them from MMMqtt.c; elsewhere
 * they stay at their initialisers. closeMQTT is strong-linked in
 * MMMqtt.c on WEB and falls back to the stub below otherwise. */
char *MQTTInterrupt = NULL;
volatile bool MQTTComplete = false;
/* UDP / TCP wifi interrupt globals always exist so the interrupt-dispatch
 * loop below can reference them unconditionally. setwifi lives in
 * MMsetwifi.c on PICOMITEWEB builds (see CMakeLists). TCP{received,
 * receiveInterrupt} are defined in Custom.c on device WEB builds and
 * stubbed in host_runtime.c — extern'd here because Custom.h's decls
 * are gated under `#ifdef PICOMITEWEB`. */
char         *UDPinterrupt = NULL;
volatile bool UDPreceive   = false;
extern volatile bool TCPreceived;
extern char         *TCPreceiveInterrupt;
/* setwifi lives in MMsetwifi.c on WEB builds. */
extern void setwifi(unsigned char *tp);
/* USB device-info hooks: real impls in drivers/usb_host_kbd/USBKeyboard.c
 * (USB device builds) and stubs in drivers/ps2_matrix/Keyboard.c (non-USB
 * device builds) and host_runtime.c (host). The HID[4] array stays
 * encapsulated on USB builds; non-USB pays no BSS for it. */
extern int port_usb_count(void);
extern int port_usb_hid_field(int n, int field);
/* VGArecovery (PICOMITEVGA && !HDMI helper) lives in
 * ports/vga{,_rp2350}/port_defaults.c — VGA ports only. */
/* WEB-stack stubs — closeMQTT / ProcessWeb / tcp_*_recv_buffers /
 * port_web_* — live in MMweb_stubs.c (linked on non-WEB device) and
 * host_runtime.c (host); real impls in MMsetwifi.c / MMMqtt.c /
 * MMtcpserver.c on WEB. */
extern const uint8_t *flash_target_contents;
int TickPeriod[NBRSETTICKS]={0};
volatile int TickTimer[NBRSETTICKS]={0};
unsigned char *TickInt[NBRSETTICKS]={NULL};
volatile unsigned char TickActive[NBRSETTICKS]={0};
unsigned char *OnKeyGOSUB = NULL;
unsigned char *OnPS2GOSUB = NULL;
/* daystrings[] defined in mm_misc_shared.c (only fun_day references it) */
const char *CaseList[] = {"", "LOWER", "UPPER"};
int OptionErrorCheck;
unsigned char EchoOption = true;
unsigned long long int __attribute__((section(".my_section"))) saved_variable;  //  __attribute__ ((persistent));  // and this is the address
unsigned int CurrentCpuSpeed;
unsigned int PeripheralBusSpeed;
extern char *ADCInterrupt;
extern volatile int ConsoleTxBufHead;
extern volatile int ConsoleTxBufTail;
extern char *LCDList[];
extern volatile BYTE SDCardStat;
extern uint64_t TIM12count;
extern char id_out[12];
extern void WriteComand(int cmd);
extern void WriteData(int data);
char *CSubInterrupt;
MMFLOAT optionangle=1.0;
bool useoptionangle=false;
bool optionfastaudio=false;
bool optionfulltime=false;
bool screen320=false;
bool optionlogging=false;
volatile bool CSubComplete=false;
/* timeroffset defined in mm_misc_shared.c (also used by bc_vm.c) */
extern uint64_t timeroffset;
int SaveOptionErrorSkip=0;
char SaveErrorMessage[MAXERRMSG] = { 0 };
int Saveerrno = 0;


// this is invoked as a command (ie, TIMER = 0)
// search through the line looking for the equals sign and step over it,
// evaluate the rest of the command and save in the timer
// this is invoked as a function













/*void update_clock(void){
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    sTime.Hours = hour;
    sTime.Minutes = minute;
    sTime.Seconds = second;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
    {
        error("RTC hardware error");
    }
    sDate.WeekDay = day_of_week;
    sDate.Month = month;
    sDate.Date = day;
    sDate.Year = year-2000;

    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
    {
        error("RTC hardware error");
    }
}*/


// this is invoked as a command (ie, date$ = "6/7/2010")
// search through the line looking for the equals sign and step over it,
// evaluate the rest of the command, split it up and save in the system counters

// this is invoked as a function

// this is invoked as a function

// this is invoked as a command (ie, time$ = "6:10:45")
// search through the line looking for the equals sign and step over it,
// evaluate the rest of the command, split it up and save in the system counters




// this is invoked as a function



void cmd_ireturn(void){
    if(InterruptReturn == NULL) error("Not in interrupt");
    checkend(cmdline);
    nextstmt = InterruptReturn;
    InterruptReturn = NULL;
    if(g_LocalIndex)    ClearVars(g_LocalIndex--, true);                        // delete any local variables
    g_TempMemoryIsChanged = true;                                     // signal that temporary memory should be checked
    *CurrentInterruptName = 0;                                        // for static vars we are not in an interrupt
#ifdef GUICONTROLS
    if(DelayedDrawKeyboard) {
        DrawKeyboard(1);                                            // the pop-up GUI keyboard should be drawn AFTER the pen down interrupt
        DelayedDrawKeyboard = false;
    }
    if(DelayedDrawFmtBox) {
        DrawFmtBox(1);                                              // the pop-up GUI keyboard should be drawn AFTER the pen down interrupt
        DelayedDrawFmtBox = false;
    }
#endif
	if(SaveOptionErrorSkip>0)OptionErrorSkip=SaveOptionErrorSkip+1;
    strcpy(MMErrMsg , SaveErrorMessage);
    MMerrno = Saveerrno;
}

void MIPS16 cmd_library(void) {  
    unsigned char *tp;
    /********************************************************************************************************************
     ******* LIBRARY SAVE **********************************************************************************************/
    if(checkstring(cmdline, (unsigned char *)"SAVE")) {  
        unsigned char *p=NULL,  *pp , *m, *MemBuff, *TempPtr;
        unsigned short rem, tkn;
        int i, j, k, InCFun, InQuote, CmdExpected;
        unsigned int CFunDefAddr[100], *CFunHexAddr[100] ;
        if(CurrentLinePtr) error("Invalid in a program");
        if(*ProgMemory != 0x01) return;
        checkend(p);
        ClearRuntime(true);
        TempPtr = m = MemBuff = GetTempMemory(EDIT_BUFFER_SIZE);

        rem = GetCommandValue((unsigned char *)"Rem");
        InQuote = InCFun = j = 0;
        CmdExpected = true;
       if(Option.LIBRARY_FLASH_SIZE != MAX_PROG_SIZE){
           uint32_t *c = (uint32_t *)(flash_progmemory - MAX_PROG_SIZE);
           if (*c != 0xFFFFFFFF)
            error("Flash Slot % already in use",MAXFLASHSLOTS);
        ;
       }
        // first copy the current program code residing in the Library area to RAM
        if(Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE){
            p = ProgMemory - Option.LIBRARY_FLASH_SIZE;
            while(!(p[0] == 0 && p[1] == 0)) *m++ = *p++;
              *m++ = 0;                                               // terminate the last line
        }
        //dump(m, 256);
        // then copy the current contents of the program memory to RAM
        
       //MMPrintString("\r\n Size=1 ");PInt(m - MemBuff);
        p = ProgMemory;
        while(!(p[0] == 0xff && p[1] == 0xff)){
//        while(*p != 0xff) {
            if(p[0] == 0 && p[1] == 0) break;                       // end of the program
            if(*p == T_NEWLINE) {
                TempPtr = m;
                CurrentLinePtr = p;
                *m++ = *p++;
                CmdExpected = true;                                 // if true we can expect a command next (possibly a CFunction, etc)
                if(*p == 0) {                                       // if this is an empty line we can skip it
                    p++;
                    if(*p == 0) break;                              // end of the program or module
                    m--;
                    continue;
                }
            }

            if(*p == T_LINENBR) {
//                TempPtr = m;
                *m++ = *p++; *m++ = *p++; *m++ = *p++;              // copy the line number
                skipspace(p);
            }

            if(*p == T_LABEL) {
                for(i = p[1] + 2; i > 0; i--) *m++ = *p++;          // copy the label
//                TempPtr = m;
                skipspace(p);
            }
            tkn=commandtbl_decode(p);
            //if(CmdExpected && ( *p == GetCommandValue("End CFunction") || *p == GetCommandValue("End CSub") || *p == GetCommandValue("End DefineFont"))) {
            if(CmdExpected && (  tkn == GetCommandValue((unsigned char *)"End CSub") || tkn == GetCommandValue((unsigned char *)"End DefineFont"))) {
                InCFun = false;                                     // found an  END CSUB or END DEFINEFONT token
            }
            if(InCFun) {
                skipline(p);                                        // skip the body of a CFunction
                m = TempPtr;                                        // don't copy the new line
                continue;
            }

            tkn=commandtbl_decode(p);
            if(CmdExpected && ( tkn == cmdCSUB || tkn == GetCommandValue((unsigned char *)"DefineFont"))) {    // found a  CSUB or DEFINEFONT token
                CFunDefAddr[++j] = (unsigned int)m;                 // save its address so that the binary copy in the library can point to it
                while(*p) *m++ = *p++;                              // copy the declaration
                InCFun = true;
            }

            tkn=commandtbl_decode(p);
            if(CmdExpected && tkn == rem) {                          // found a REM token
                skipline(p);
                m = TempPtr;                                        // don't copy the new line tokens
                continue;
            }

            if(*p >= C_BASETOKEN || isnamestart(*p))
                CmdExpected = false;                                // stop looking for a CFunction, etc on this line

            if(*p == '"') InQuote = !InQuote;                       // found the start/end of a string

            if(*p == '\'' && !InQuote) {                            // found a modern remark char
                skipline(p);
                //PIntHC(*(m-3)); PIntHC(*(m-2)); PIntHC(*(m-1)); PIntHC(*(m));
                //MMPrintString("\r\n");
                //if(*(m-3) == 0x01) {        //Original condition from Micromites
                /* Check to see if comment is the first thing on the line or its only preceded by spaces.
                Spaces have been reduced to a single space so we treat a comment with 1 space before it
                as a comment line to be omitted.
                */    
                if((*(m-1) == 0x01) ||  ((*(m-2) == 0x01) && (*(m-1) == 0x20))){    
                    m = TempPtr;                                    // if the comment was the only thing on the line don't copy the line at all
                    continue;
                } else
                    p--;
            }

            if(*p == ' ' && !InQuote) {                             // found a space
                if(*(m-1) == ' ') m--;                              // don't copy the space if the preceeding char was a space
            }

            if(p[0] == 0 && p[1] == 0) break;                       // end of the program
            *m++ = *p++;
        }
      
       // MMPrintString("\r\n Size2= ");PInt(m - MemBuff);
       //The picomite will have any fonts or CSUBs binary starting on a new 256 byte block so there can be many 
       //0x00 bytes at the end of the program. We only need two of them .
        // At the end of the program so get the two 0x00 bytes
        *m++ = *p++; 
        *m++ = *p++; 
        // Step the program memory up to the first 0xFF of the 4 that mark the beginning of the CSub binaries.
        while(*p != 0xff) p++; 
        p++;p++; p++;p++;                                           // step over the header of the four 0xff bytes
                                    
        //step the memory to the next 4 word boundary
        // while((unsigned int)p & 0b11) p++;
        while((unsigned int)m & 0b11) *m++ = 0x00;                  // step memory to the next word boundary
        *m++=0xFF;*m++=0xFF;*m++=0xFF;*m++=0xFF;                    //write 4 byte of the csub binary header 
        
     
        // now copy the CFunction/CSub/Font data
        // =====================================
        // the format of a CFunction in flash is:
        //   Unsigned Int - Address of the CFunction/CSub/Font in program memory (points to the token).  For a font it is zero.
        //   Unsigned Int - The length of the CFunction/CSub/Font in bytes including the Offset (see below)
        //   Unsigned Int - The Offset (in words) to the main() function (ie, the entry point to the CFunction/CSub).  The specs for the font if it is a font.
        //   word1..wordN - The CFunction code
        // The next CFunction starts immediately following the last word of the previous CFunction

        // first, copy any CFunctions residing in the library area to RAM
       // MMPrintString("\r\n Copying CSUBS from library");
        k = 0;                                                      // this is used to index CFunHexLibAddr[] for later adjustment of a CFun's address
        if(CFunctionLibrary != NULL) {
            pp = (unsigned char *)CFunctionLibrary;
            while(*(unsigned int *)pp != 0xffffffff) {
//                CFunHexLibAddr[++k] = (unsigned int *)m;            // save the address for later adjustment
                j = (*(unsigned int *)(pp + 4)) + 8;                // calculate the total size of the CFunction
                while(j--) *m++ = *pp++;                            // copy it into RAM
            }
        }
      
        // then, copy any CFunctions in program memory to RAM
        
        i = 0;                                                      // this is used to index CFunHexAddr[] for later adjustment of a CFun's address
       // while(*(unsigned int *)p != 0xffffffff && (int)p < Option.PROG_FLASH_SIZE) {
        while(*(unsigned int *)p != 0xffffffff) {    
            CFunHexAddr[++i] = (unsigned int *)m;                   // save the address for later adjustment
            j = (*(unsigned int *)(p + 4)) + 8;                     // calculate the total size of the CFunction
            while(j--) *m++ = *p++;                                 // copy it into RAM
        }
        // we have now copied all the CFunctions into RAM

        // calculate the size of the library code  to  end on a word boundary
        j=(((m - MemBuff) + (0x4 - 1)) & (~(0x4 - 1)));
       
         //We only have reserved MAX_PROG_SIZE of flash for library code .
        //Error if we try to use too much
        if (j > MAX_PROG_SIZE) error("Library too big");
              
        TempPtr = (LibMemory);
     
        // now adjust the addresses of the declaration in each CFunction header
        // do not adjust a font who's "address" is  fontno-1.
        // NO ADJUSTMENT REQUIRED FOR PICOMITE as ADDRESS is RELATIVE and LIBRARY is at a fixed location.
        // first, CFunctions that were already in the library
        //for(; k > 0; k--) {
             //if ((*CFunHexLibAddr[k]>>31)==0)  *CFunHexLibAddr[k] -= ((unsigned int)( MAX_PROG_SIZE);
        //}

        // then, CFunctions that are being added to the library
        for(; i > 0; i--) {
          if ((*CFunHexAddr[i]>>31)==0)  *CFunHexAddr[i] = (CFunDefAddr[i] - (unsigned int)MemBuff);
        }

       
  //******************************************************************************
        //now write the library from ram to the library flash area
        // initialise for writing to the flash
        FlashWriteInit(LIBRARY_FLASH);
        hal_flash_erase(realflashpointer, MAX_PROG_SIZE);
        i=MAX_PROG_SIZE/4;
       
        int *ppp=(int *)(flash_progmemory - MAX_PROG_SIZE);
        while(i--)if(*ppp++ != 0xFFFFFFFF){
            enable_interrupts_pico();
            error("Flash erase problem");
        }
   
        i=0;
        for(k = 0; k < m - MemBuff; k++){        // write to the flash byte by byte
           FlashWriteByte(MemBuff[k]);
        }
        FlashWriteClose();
        Option.LIBRARY_FLASH_SIZE = MAX_PROG_SIZE;
        SaveOptions();

        if(MMCharPos > 1) MMPrintString("\r\n");                    // message should be on a new line
        MMPrintString("Library Saved ");
        IntToStr((char *)tknbuf, k, 10);
        MMPrintString((char *)tknbuf);
        MMPrintString(" bytes\r\n");
        fflush(stdout);
        uSec(2000);
     

        //Now call the new command that will clear the current program memory then
        //write the library code at Option.ProgFlashSize by copying it from the windbond
        //and return to the command prompt.
        cmdline = (unsigned char *)""; CurrentLinePtr = NULL;    // keep the NEW command happy
        cmd_new();                              //  delete the program,add the library code and return to the command prompt
    }
     /********************************************************************************************************************
     ******* LIBRARY DELETE **********************************************************************************************/

     if(checkstring(cmdline, (unsigned char *)"DELETE")) {
        int i;
        if(CurrentLinePtr) error("Invalid in a program");
        if(Option.LIBRARY_FLASH_SIZE != MAX_PROG_SIZE) return;
        
        FlashWriteInit(LIBRARY_FLASH);
        hal_flash_erase(realflashpointer, MAX_PROG_SIZE);
        i=MAX_PROG_SIZE/4;
       
        int *ppp=(int *)(flash_progmemory - MAX_PROG_SIZE);
        while(i--)if(*ppp++ != 0xFFFFFFFF){
            enable_interrupts_pico();
            error("Flash erase problem");
        }
        enable_interrupts_pico();

        Option.LIBRARY_FLASH_SIZE= 0;
        SaveOptions();
        return;
        // Clear Program Memory and also the Library at the end.
//        cmdline = ""; CurrentLinePtr = NULL;    // keep the NEW command happy
//        cmd_new();                              //  delete any program,and the library code and return to the command prompt
        
     }

      /********************************************************************************************************************
      ******* LIBRARY LIST **********************************************************************************************/

     if(checkstring(cmdline, (unsigned char *)"LIST ALL")) {
        if(CurrentLinePtr) error("Invalid in a program");
        if(Option.LIBRARY_FLASH_SIZE != MAX_PROG_SIZE) return;
        ListProgram(ProgMemory - Option.LIBRARY_FLASH_SIZE, true);
        return;
     }
     if(checkstring(cmdline, (unsigned char *)"LIST")) {
        if(CurrentLinePtr) error("Invalid in a program");
        if(Option.LIBRARY_FLASH_SIZE != MAX_PROG_SIZE) return;
        ListProgram(ProgMemory - Option.LIBRARY_FLASH_SIZE, false);
        return;
     }
     if((tp=checkstring(cmdline, (unsigned char *)"DISK SAVE"))) {
        getargs(&tp,1,(unsigned char *)",");
        if(!(argc==1))error("Syntax");
        if(CurrentLinePtr) error("Invalid in a program");
        int fnbr = FindFreeFileNbr();
        if (!InitSDCard())  return;
        if(Option.LIBRARY_FLASH_SIZE != MAX_PROG_SIZE) error("No library to store");
        char *pp = (char *)getFstring(argv[0]);
        if (strchr((char *)pp, '.') == NULL)
            strcat((char *)pp, ".lib");
        if (!BasicFileOpen((char *)pp, fnbr, FA_WRITE | FA_CREATE_ALWAYS)) return;
        int i = 0;
        // first count the normal program code residing in the Library
        char *p = (char *)LibMemory;
        while(!(p[0] == 0 && p[1] == 0)) {
            p++; i++;
        }
        while(*p == 0){ // the end of the program can have multiple zeros -count them
            p++;i++;
        }
        p++; i++;    //get 0xFF that ends the program and count it
        while((unsigned int)p & 0b11) { //count to the next word boundary
        p++;i++;
        }
            
        //Now add the binary used for CSUB and Fonts
        if(CFunctionLibrary != NULL) {
            int j=0;
            unsigned int *pint = (unsigned int *)CFunctionLibrary;
            while(*pint != 0xffffffff) {
                pint++;                                      //step over the address or Font No.
                j += *pint + 8;                              //Read the size
                pint += (*pint + 4) / sizeof(unsigned int);  //set pointer to start of next CSUB/Font
            }
            i=i+j;
        }
        char *s = (char *)LibMemory;
//        int i=MAX_PROG_SIZE;
        while(i--){
            FilePutChar(*s++,fnbr);
        }
        FileClose(fnbr);
        return;
     }
     if((tp=checkstring(cmdline, (unsigned char *)"DISK LOAD"))) {
        int fsize;
        getargs(&tp,1,(unsigned char *)",");
        if(!(argc==1))error("Syntax");
        if(CurrentLinePtr) error("Invalid in a program");
        int fnbr = FindFreeFileNbr();
        if (!InitSDCard())  return;
        char *pp = (char *)getFstring(argv[0]);
        if (strchr((char *)pp, '.') == NULL)
            strcat((char *)pp, ".lib");
        if (!BasicFileOpen((char *)pp, fnbr, FA_READ)) return;
		fsize = (int)hal_fs_size(hal_fds[fnbr]);
        if(fsize>MAX_PROG_SIZE)error("File size % should be % or less",fsize,MAX_PROG_SIZE);
        FlashWriteInit(LIBRARY_FLASH);
        hal_flash_erase(realflashpointer, MAX_PROG_SIZE);
        int i=MAX_PROG_SIZE/4;
        int *ppp=(int *)(flash_progmemory - MAX_PROG_SIZE);
        while(i--)if(*ppp++ != 0xFFFFFFFF){
            enable_interrupts_pico();
            error("Flash erase problem");
        }
        for(int k = 0; k < fsize; k++){        // write to the flash byte by byte
           FlashWriteByte(FileGetChar(fnbr));
        }
        FlashWriteClose();
        Option.LIBRARY_FLASH_SIZE = MAX_PROG_SIZE;
        SaveOptions();
        FileClose(fnbr);
        return;
    }
  
     error("Invalid syntax");
    }
     

// set up the tick interrupt
void cmd_settick(void){
    int period;
    int irq=0;;
//    int pause=0;
    char *s=GetTempMemory(STRINGSIZE);
    getargs(&cmdline, 5, (unsigned char *)",");
    strcpy(s,(char *)argv[0]);
    if(!(argc == 3 || argc == 5)) error("Argument count");
    if(argc == 5) irq = getint(argv[4], 1, NBRSETTICKS) - 1;
    if(strcasecmp((char *)argv[0],"PAUSE")==0){
        TickActive[irq]=0;
        return;
    } else if(strcasecmp((char *)argv[0],"RESUME")==0){
        TickActive[irq]=1;
        return;
    } else period = getint(argv[0], -1, INT_MAX);
    if(period == 0) {
        TickInt[irq] = NULL;                                        // turn off the interrupt
        TickPeriod[irq] = 0;
        TickTimer[irq] = 0;                                         // set the timer running
        TickActive[irq]=0;
    } else {
        TickPeriod[irq] = period;
        TickInt[irq] = GetIntAddress(argv[2]);                      // get a pointer to the interrupt routine
        TickTimer[irq] = 0;                                         // set the timer running
        InterruptUsed = true;
        TickActive[irq]=1;

    }
}
void PO(char *s) {
    MMPrintString("OPTION "); MMPrintString(s); MMPrintString(" ");
}

void PO2Str(char *s1, const char *s2) {
    PO(s1); MMPrintString((char *)s2); MMPrintString("\r\n");
}
void PO3Str(char *s1, const char *s2, const char *s3) {
    PO(s1); MMPrintString((char *)s2); MMPrintString(", ");MMPrintString((char *)s3); MMPrintString("\r\n");
}
void PO2StrInt(char *s1, const char *s2, int s3) {
    PO(s1); MMPrintString((char *)s2); MMPrintString(" @ ");PInt(s3);MMPrintString("KHz\r\n");
}


void PO2Int(char *s1, int n) {
    PO(s1); PInt(n); MMPrintString("\r\n");
}
void PO2IntH(char *s1, int n) {
    PO(s1); PIntH(n); MMPrintString("\r\n");
}
void PO3Int(char *s1, int n1, int n2) {
    PO(s1); PInt(n1); PIntComma(n2); MMPrintString("\r\n");
}
void PO4Int(char *s1, int n1, int n2, int n3) {
    PO(s1); PInt(n1); PIntComma(n2);  PIntComma(n3);  MMPrintString("\r\n");
}
void PO5Int(char *s1, int n1, int n2, int n3, int n4) {
    PO(s1); PInt(n1); PIntComma(n2);  PIntComma(n3);  PIntComma(n4); MMPrintString("\r\n");
}

void MIPS16 printoptions(void){
//	LoadOptions();
    MMPrintString(banner);
    if(Option.SerialConsole){
        MMPrintString("OPTION SERIAL CONSOLE COM");
        MMputchar((Option.SerialConsole & 3)+48,1);
        MMputchar(',',1);
        MMPrintString((char *)PinDef[Option.SerialTX].pinname);MMputchar(',',1);
        MMPrintString((char *)PinDef[Option.SerialRX].pinname);
        if(Option.SerialConsole & 4)MMPrintString((char *)",BOTH");
        PRet();
    }
    /* SYSTEM SPI + LCD SPI prints — VGA targets share SYSTEM_CLK with
     * the VGA scanout PIO, so they print VGA PINS instead (handled in
     * port_print_display_options). Skip both on VGA. */
    if (!HAL_PORT_IS_VGA) {
        if (Option.SYSTEM_CLK) {
            PO("SYSTEM SPI");
            MMPrintString((char *)PinDef[Option.SYSTEM_CLK].pinname); MMputchar(',', 1);
            MMPrintString((char *)PinDef[Option.SYSTEM_MOSI].pinname); MMputchar(',', 1);
            MMPrintString((char *)PinDef[Option.SYSTEM_MISO].pinname); MMPrintString("\r\n");
        }
        port_print_lcd_spi();
    }
    if(Option.SYSTEM_I2C_SDA){
        PO("SYSTEM I2C");
        MMPrintString((char *)PinDef[Option.SYSTEM_I2C_SDA].pinname);MMputchar(',',1);
        MMPrintString((char *)PinDef[Option.SYSTEM_I2C_SCL].pinname);
        if(Option.SYSTEM_I2C_SLOW)MMPrintString(", SLOW\r\n");
        else PRet();
    }
    if(Option.Autorun){
        PO("AUTORUN "); 
        if(Option.Autorun>0 && Option.Autorun<=MAXFLASHSLOTS)PInt(Option.Autorun);
        else MMPrintString("ON");
        if(Option.NoReset){
            MMPrintString(",NORESET");
        }
        PRet();
    }
    if(Option.Baudrate != CONSOLE_BAUDRATE) PO2Int("BAUDRATE", Option.Baudrate);
    if(Option.FlashSize !=2048*1024) PO2Int("FLASH SIZE", Option.FlashSize);
    if(MAX_PROG_SIZE == Option.LIBRARY_FLASH_SIZE) PO2IntH("LIBRARY_FLASH_SIZE ", Option.LIBRARY_FLASH_SIZE);
    if(Option.Invert == true) PO2Str("CONSOLE", "INVERT");
    if(Option.Invert == 2) PO2Str("CONSOLE", "AUTO");
    if(Option.ColourCode == true) PO2Str("COLOURCODE", "ON");
    if(Option.continuation) PO2Str("CONTINUATION LINES", "ON");
    if(Option.PWM == true) PO2Str("POWER PWM", "ON");
    if(Option.Listcase != CONFIG_TITLE) PO2Str("CASE", CaseList[(int)Option.Listcase]);
    if(Option.Tab != 2) PO2Int("TAB", Option.Tab);
    if(Option.DefaultFC !=WHITE ||Option.DefaultBC !=BLACK){
        PO("DEFAULT COLOURS");
            if(Option.DefaultFC==WHITE)MMPrintString("WHITE, ");
            else if(Option.DefaultFC==YELLOW)MMPrintString("YELLOW,");
            else if(Option.DefaultFC==LILAC)MMPrintString("LILAC,");
            else if(Option.DefaultFC==BROWN)MMPrintString("BROWN,");
            else if(Option.DefaultFC==FUCHSIA)MMPrintString("FUCHSIA,");
            else if(Option.DefaultFC==RUST)MMPrintString("RUST, ");
            else if(Option.DefaultFC==MAGENTA)MMPrintString("MAGENTA,");
            else if(Option.DefaultFC==RED)MMPrintString("RED,");
            else if(Option.DefaultFC==CYAN)MMPrintString("CYAN,");
            else if(Option.DefaultFC==GREEN)MMPrintString("GREEN,");
            else if(Option.DefaultFC==CERULEAN)MMPrintString("CERULEAN,");
            else if(Option.DefaultFC==MIDGREEN)MMPrintString("MIDGREEN,");
            else if(Option.DefaultFC==COBALT)MMPrintString("COBALT,");
            else if(Option.DefaultFC==MYRTLE)MMPrintString("MYRTLE,");
            else if(Option.DefaultFC==BLUE)MMPrintString("BLUE,");
            else if(Option.DefaultFC==BLACK)MMPrintString("BLACK,");
            if(Option.DefaultBC==WHITE)MMPrintString(" WHITE");
            else if(Option.DefaultBC==YELLOW)MMPrintString(" YELLOW");
            else if(Option.DefaultBC==LILAC)MMPrintString(" LILAC");
            else if(Option.DefaultBC==BROWN)MMPrintString(" BROWN");
            else if(Option.DefaultBC==FUCHSIA)MMPrintString(" FUCHSIA");
            else if(Option.DefaultBC==RUST)MMPrintString(" RUST");
            else if(Option.DefaultBC==MAGENTA)MMPrintString(" MAGENTA");
            else if(Option.DefaultBC==RED)MMPrintString(" RED");
            else if(Option.DefaultBC==CYAN)MMPrintString(" CYAN");
            else if(Option.DefaultBC==GREEN)MMPrintString(" GREEN");
            else if(Option.DefaultBC==CERULEAN)MMPrintString(" CERULEAN");
            else if(Option.DefaultBC==MIDGREEN)MMPrintString(" MIDGREEN");
            else if(Option.DefaultBC==COBALT)MMPrintString(" COBALT");
            else if(Option.DefaultBC==MYRTLE)MMPrintString(" MYRTLE");
            else if(Option.DefaultBC==BLUE)MMPrintString(" BLUE");
            else if(Option.DefaultBC==BLACK)MMPrintString(" BLACK");
        PRet();
    }
    port_print_keyboard_heartbeat();
    if(Option.AllPins)PO2Str("PICO", "OFF");
    port_print_display_options();
    if(Option.CombinedCS)PO2Str("SDCARD", "COMBINED CS");
    port_print_usb_kb_repeat();
    if(Option.AUDIO_L || Option.AUDIO_CLK_PIN || Option.audio_i2s_bclk){
        PO("AUDIO");
        if(Option.AUDIO_L){
            MMPrintString((char *)PinDef[Option.AUDIO_L].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.AUDIO_R].pinname);
        } else if(Option.audio_i2s_data){
            MMPrintString((char *)"I2S ");
            MMPrintString((char *)PinDef[Option.audio_i2s_bclk].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.audio_i2s_data].pinname);
        } else if(!Option.AUDIO_DCS_PIN){
            MMPrintString((char *)"SPI ");
            MMPrintString((char *)PinDef[Option.AUDIO_CS_PIN].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.AUDIO_CLK_PIN].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.AUDIO_MOSI_PIN].pinname);
        } else {
            MMPrintString((char *)"VS1053 ");
            MMPrintString((char *)PinDef[Option.AUDIO_CLK_PIN].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.AUDIO_MOSI_PIN].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.AUDIO_MISO_PIN].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.AUDIO_CS_PIN].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.AUDIO_DCS_PIN].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.AUDIO_DREQ_PIN].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.AUDIO_RESET_PIN].pinname);
        }
        MMPrintString("', ON PWM CHANNEL ");
        PInt(Option.AUDIO_SLICE);
        MMPrintString("\r\n");
    }
    if(Option.RTC)PO2Str("RTC AUTO", "ENABLE");

    if(Option.INT1pin!=9 || Option.INT2pin!=10 || Option.INT3pin!=11 || Option.INT4pin!=12){
        PO("COUNT"); MMPrintString((char *)PinDef[Option.INT1pin].pinname);
        MMputchar(',',1);;MMPrintString((char *)PinDef[Option.INT2pin].pinname);
        MMputchar(',',1);;MMPrintString((char *)PinDef[Option.INT3pin].pinname);
        MMputchar(',',1);;MMPrintString((char *)PinDef[Option.INT4pin].pinname);PRet();
    }

    if(Option.modbuff){
        PO("MODBUFF ENABLE ");
        if(Option.modbuffsize!=128)PInt(Option.modbuffsize);
        PRet();
    }
    if(*Option.F1key)PO2Str("F1", (const char *)Option.F1key);
    if(*Option.F5key)PO2Str("F5", (const char *)Option.F5key);
    if(*Option.F6key)PO2Str("F6", (const char *)Option.F6key);
    if(*Option.F7key)PO2Str("F7", (const char *)Option.F7key);
    if(*Option.F8key)PO2Str("F8", (const char *)Option.F8key);
    if(*Option.F9key)PO2Str("F9", (const char *)Option.F9key);
    if(*Option.platform && *Option.platform!=0xFF)PO2Str("PLATFORM", (const char *)Option.platform);
    if(Option.DefaultFont!=1)PO3Int("DEFAULT FONT",(Option.DefaultFont>>4)+1, Option.DefaultFont & 0xF);
    /* PSRAM_CS_PIN is only meaningful on rp2350 (HAL_PORT_HAS_PSRAM == 1
     * there, 0 elsewhere); on rp2040 the field stays zero so the print
     * is a no-op. */
    if (HAL_PORT_HAS_PSRAM && Option.PSRAM_CS_PIN != 0)
        PO2Str("PSRAM PIN", PinDef[Option.PSRAM_CS_PIN].pinname);
    /* HEARTBEAT PIN: rp2040 prints when heartbeatpin != default (43).
     * rp2350 also prints the default-pin case when rp2350a==false
     * (RP2350B has different default). Both pieces stay runtime-gated
     * via HAL_PORT_PWM_SLICE_COUNT > 8 (the rp2350 detector). */
    {
        int print_pin = (Option.heartbeatpin != 43 && !Option.NoHeartbeat)
            || (HAL_PORT_PWM_SLICE_COUNT > 8 &&
                Option.heartbeatpin == 43 && !rp2350a && !Option.NoHeartbeat);
        if (print_pin) PO2Str("HEARTBEAT PIN", PinDef[Option.heartbeatpin].pinname);
    }
}

int MIPS16 checkslice(int pin1,int pin2, int ignore){
    if((PinDef[pin1].slice & 0xf) != (PinDef[pin2].slice &0xf)) error("Pins not on same PWM slice");
    if(!ignore){
        if(!((PinDef[pin1].slice - PinDef[pin2].slice == 128) || (PinDef[pin2].slice - PinDef[pin1].slice == 128))) error("Pins both same channel");
    }
    return PinDef[pin1].slice & 0xf;
}

void MIPS16 setterminal(int height,int width){
	  char sp[20]={0};
	  strcpy(sp,"\033[8;");
	  IntToStr(&sp[strlen(sp)],height,10);
	  strcat(sp,";");
	  IntToStr(&sp[strlen(sp)],width+1,10);
	  strcat(sp,"t");
	  SSPrintString(sp);						//
}
/* fun_keydown lives in vm_sys_input.c — routes through hal_keyboard_*.
 * cmd_update (Update Firmware) is non-USB-only — moved to
 * ports/pico_sdk_common/cmd_files_hooks.c. */
void MIPS16 disable_systemspi(void){
    if(!IsInvalidPin(Option.SYSTEM_MOSI))ExtCurrentConfig[Option.SYSTEM_MOSI] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_MISO))ExtCurrentConfig[Option.SYSTEM_MISO] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_CLK))ExtCurrentConfig[Option.SYSTEM_CLK] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_MOSI))ExtCfg(Option.SYSTEM_MOSI, EXT_NOT_CONFIG, 0);
    if(!IsInvalidPin(Option.SYSTEM_MISO))ExtCfg(Option.SYSTEM_MISO, EXT_NOT_CONFIG, 0);
    if(!IsInvalidPin(Option.SYSTEM_CLK))ExtCfg(Option.SYSTEM_CLK, EXT_NOT_CONFIG, 0);
    /* PICOMITE+rp2350 has dedicated LCD_CLK/MOSI/MISO Option fields
     * that may share pins with SYSTEM_CLK; clear them too if so.
     * No-op on every other build (host stub). */
    port_clear_lcd_spi_if_shares_system();
    Option.SYSTEM_MOSI=0;
    Option.SYSTEM_MISO=0;
    Option.SYSTEM_CLK=0;
}
/* disable_lcdspi (PICOMITE+rp2350-only) lives in
 * ports/pico_rp2350/port_defaults.c — only that port has LCD_CLK
 * Option fields. */
void MIPS16 disable_systemi2c(void){
    if(!IsInvalidPin(Option.SYSTEM_I2C_SCL))ExtCurrentConfig[Option.SYSTEM_I2C_SCL] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_I2C_SDA))ExtCurrentConfig[Option.SYSTEM_I2C_SDA] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_I2C_SCL))ExtCfg(Option.SYSTEM_I2C_SCL, EXT_NOT_CONFIG, 0);
    if(!IsInvalidPin(Option.SYSTEM_I2C_SDA))ExtCfg(Option.SYSTEM_I2C_SDA, EXT_NOT_CONFIG, 0);
    Option.SYSTEM_I2C_SCL=0;
    Option.SYSTEM_I2C_SDA=0;
}
void MIPS16 disable_sd(void){
    if(!IsInvalidPin(Option.SD_CS))ExtCurrentConfig[Option.SD_CS] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SD_CS))ExtCfg(Option.SD_CS, EXT_NOT_CONFIG, 0);
    Option.SD_CS=0;
    if(!IsInvalidPin(Option.SD_CLK_PIN))ExtCurrentConfig[Option.SD_CLK_PIN] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SD_CLK_PIN))ExtCfg(Option.SD_CLK_PIN, EXT_NOT_CONFIG, 0);
    Option.SD_CLK_PIN=0;
    if(!IsInvalidPin(Option.SD_MOSI_PIN))ExtCurrentConfig[Option.SD_MOSI_PIN] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SD_MOSI_PIN))ExtCfg(Option.SD_MOSI_PIN, EXT_NOT_CONFIG, 0);
    Option.SD_MOSI_PIN=0;
    if(!IsInvalidPin(Option.SD_MISO_PIN))ExtCurrentConfig[Option.SD_MISO_PIN] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SD_MISO_PIN))ExtCfg(Option.SD_MISO_PIN, EXT_NOT_CONFIG, 0);
    Option.SD_MISO_PIN=0;
    Option.CombinedCS=0;
    /* VGA targets share SYSTEM_CLK/MOSI/MISO with the SD card (the
     * non-VGA SDCARD setter wires SD to dedicated pins; VGA reuses
     * the system SPI). Clear them too on VGA so the SDCARD-disable
     * fully releases the bus. */
    if (HAL_PORT_IS_VGA) {
        if(!IsInvalidPin(Option.SYSTEM_CLK))ExtCurrentConfig[Option.SYSTEM_CLK] = EXT_DIG_IN;
        if(!IsInvalidPin(Option.SYSTEM_CLK))ExtCfg(Option.SYSTEM_CLK, EXT_NOT_CONFIG, 0);
        Option.SYSTEM_CLK=0;
        if(!IsInvalidPin(Option.SYSTEM_MISO))ExtCurrentConfig[Option.SYSTEM_MISO] = EXT_DIG_IN;
        if(!IsInvalidPin(Option.SYSTEM_MISO))ExtCfg(Option.SYSTEM_MISO, EXT_NOT_CONFIG, 0);
        Option.SYSTEM_MISO=0;
        if(!IsInvalidPin(Option.SYSTEM_MOSI))ExtCurrentConfig[Option.SYSTEM_MOSI] = EXT_DIG_IN;
        if(!IsInvalidPin(Option.SYSTEM_MOSI))ExtCfg(Option.SYSTEM_MOSI, EXT_NOT_CONFIG, 0);
        Option.SYSTEM_MOSI=0;
    }
}
void disable_audio(void){
    if(!IsInvalidPin(Option.AUDIO_L))ExtCurrentConfig[Option.AUDIO_L] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.AUDIO_L))ExtCfg(Option.AUDIO_L, EXT_NOT_CONFIG, 0);

    if(!IsInvalidPin(Option.AUDIO_R))ExtCurrentConfig[Option.AUDIO_R] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.AUDIO_R))ExtCfg(Option.AUDIO_R, EXT_NOT_CONFIG, 0);

    if(!IsInvalidPin(Option.AUDIO_CLK_PIN))ExtCurrentConfig[Option.AUDIO_CLK_PIN] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.AUDIO_CLK_PIN))ExtCfg(Option.AUDIO_CLK_PIN, EXT_NOT_CONFIG, 0);

    if(!IsInvalidPin(Option.AUDIO_CS_PIN))ExtCurrentConfig[Option.AUDIO_CS_PIN] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.AUDIO_CS_PIN))ExtCfg(Option.AUDIO_CS_PIN, EXT_NOT_CONFIG, 0);

    if(!IsInvalidPin(Option.AUDIO_DCS_PIN))ExtCurrentConfig[Option.AUDIO_DCS_PIN] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.AUDIO_DCS_PIN))ExtCfg(Option.AUDIO_DCS_PIN, EXT_NOT_CONFIG, 0);

    if(!IsInvalidPin(Option.AUDIO_DREQ_PIN))ExtCurrentConfig[Option.AUDIO_DREQ_PIN] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.AUDIO_DREQ_PIN))ExtCfg(Option.AUDIO_DREQ_PIN, EXT_NOT_CONFIG, 0);

    if(!IsInvalidPin(Option.AUDIO_MOSI_PIN))ExtCurrentConfig[Option.AUDIO_MOSI_PIN] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.AUDIO_MOSI_PIN))ExtCfg(Option.AUDIO_MOSI_PIN, EXT_NOT_CONFIG, 0);

    if(!IsInvalidPin(Option.AUDIO_MISO_PIN))ExtCurrentConfig[Option.AUDIO_MISO_PIN] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.AUDIO_MISO_PIN))ExtCfg(Option.AUDIO_MISO_PIN, EXT_NOT_CONFIG, 0);

    if(!IsInvalidPin(Option.AUDIO_RESET_PIN))ExtCurrentConfig[Option.AUDIO_RESET_PIN] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.AUDIO_RESET_PIN))ExtCfg(Option.AUDIO_RESET_PIN, EXT_NOT_CONFIG, 0);

    if(!IsInvalidPin(Option.audio_i2s_bclk))ExtCurrentConfig[Option.audio_i2s_bclk] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.audio_i2s_bclk))ExtCfg(Option.audio_i2s_bclk, EXT_NOT_CONFIG, 0);


    if(!IsInvalidPin(PINMAP[PinDef[Option.audio_i2s_bclk].GPno+1]))ExtCurrentConfig[PINMAP[PinDef[Option.audio_i2s_bclk].GPno+1]] = EXT_DIG_IN ;   
    if(!IsInvalidPin(PINMAP[PinDef[Option.audio_i2s_bclk].GPno+1]))ExtCfg(PINMAP[PinDef[Option.audio_i2s_bclk].GPno+1], EXT_NOT_CONFIG, 0);


    if(!IsInvalidPin(Option.audio_i2s_data))ExtCurrentConfig[Option.audio_i2s_data] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.audio_i2s_data))ExtCfg(Option.audio_i2s_data, EXT_NOT_CONFIG, 0);

    Option.AUDIO_L=0;
    Option.AUDIO_R=0;
    Option.AUDIO_CLK_PIN=0;
    Option.AUDIO_CS_PIN=0;
    Option.AUDIO_DCS_PIN=0;
    Option.AUDIO_DREQ_PIN=0;
    Option.AUDIO_RESET_PIN=0;
    Option.AUDIO_MOSI_PIN=0;
    Option.AUDIO_MISO_PIN=0;
    Option.audio_i2s_bclk=0;
    Option.audio_i2s_data=0;
    Option.AUDIO_SLICE=99;
}
/* ConfigDisplayUser + clear320 (SPI-LCD-only) live in
 * ports/pico_sdk_common/spi_lcd_options.c. */

void MIPS16 configure(unsigned char *p){
    if(!*p){
        ResetOptions(false);
        _excep_code = RESET_COMMAND;
        SoftReset();
    } else {
        if(checkstring(p,(unsigned char *) "LIST")){
            port_print_supported_boards();
            return;
        }       
        if (port_factory_reset_board(p)) return;
        error("Invalid board for this firmware");
    }
}
void cmd_configure(void){
    configure(cmdline);
}
void MIPS16 cmd_option(void) {
    unsigned char *tp;
 
	tp = checkstring(cmdline, (unsigned char *)"NOCHECK");
	if(tp) {
		if(checkstring(tp, (unsigned char *)"ON"))	{ OptionNoCheck=true; return; }
		if(checkstring(tp, (unsigned char *)"OFF"))	{ OptionNoCheck=false; return; }
        return;
	}

    tp = checkstring(cmdline, (unsigned char *)"BASE");
    if(tp) {
        if(g_DimUsed) error("Must be before DIM or LOCAL");
        g_OptionBase = getint(tp, 0, 1);
        return;
    }

    tp = checkstring(cmdline, (unsigned char *)"EXPLICIT");
    if(tp) {
        OptionExplicit = true;
        return;
    }
	tp = checkstring(cmdline, (unsigned char *)"ANGLE");
	if(tp) {
		if(checkstring(tp, (unsigned char *)"DEGREES"))	{ optionangle=RADCONV; useoptionangle=true;return; }
		if(checkstring(tp, (unsigned char *)"RADIANS"))	{ optionangle=1.0; useoptionangle=false; return; }
	}
	tp = checkstring(cmdline, (unsigned char *)"FAST AUDIO");
	if(tp) {
		if(checkstring(tp, (unsigned char *)"OFF"))	{ optionfastaudio=0; return; }
		if(checkstring(tp, (unsigned char *)"ON"))	{ optionfastaudio=1; return; }
	}
	tp = checkstring(cmdline, (unsigned char *)"MILLISECONDS");
	if(tp) {
		if(checkstring(tp, (unsigned char *)"OFF"))	{ optionfulltime=0; return; }
		if(checkstring(tp, (unsigned char *)"ON"))	{ optionfulltime=1; return; }
	}

    if (port_lcd320_option_setter(cmdline)) return;
    tp = checkstring(cmdline, (unsigned char *)"ESCAPE");
    if(tp) {
        OptionEscape = true;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"CONSOLE");
    if(tp) {
        if(checkstring(tp,(unsigned char *) "BOTH"))OptionConsole=3;
        else if(checkstring(tp,(unsigned char *) "SERIAL"))OptionConsole=1;
        else if(checkstring(tp,(unsigned char *) "SCREEN"))OptionConsole=2;
        else if(checkstring(tp,(unsigned char *) "NONE"))OptionConsole=0;
        else error("Syntax");
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"DEFAULT");
    if(tp) {
        if(checkstring(tp,(unsigned char *) "INTEGER"))  { DefaultType = T_INT;  return; }
        if(checkstring(tp,(unsigned char *) "FLOAT"))    { DefaultType = T_NBR;  return; }
        if(checkstring(tp, (unsigned char *)"STRING"))   { DefaultType = T_STR;  return; }
        if(checkstring(tp, (unsigned char *)"NONE"))     { DefaultType = T_NOTYPE;   return; }
    }

	tp = checkstring(cmdline, (unsigned char *)"LOGGING");
	if(tp) {
		if(checkstring(tp, (unsigned char *)"OFF"))	{ optionlogging=0; return; }
		if(checkstring(tp, (unsigned char *)"ON"))	{ optionlogging=1; return; }
	}

    tp = checkstring(cmdline, (unsigned char *)"BREAK");
    if(tp) {
        BreakKey = getinteger(tp);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"F1");
	if(tp) {
		char p[STRINGSIZE];
		strcpy(p,(char *)getCstring(tp));
		if(strlen(p)>=sizeof(Option.F1key))error("Maximum 63 characters");
		else strcpy((char *)Option.F1key, p);
		SaveOptions();
		return;
	}
    tp = checkstring(cmdline, (unsigned char *)"F5");
	if(tp) {
		char p[STRINGSIZE];
		strcpy(p,(char *)getCstring(tp));
		if(strlen(p)>=sizeof(Option.F5key))error("Maximum % characters",MAXKEYLEN-1);
		else strcpy((char *)Option.F5key, p);
		SaveOptions();
		return;
	}
    tp = checkstring(cmdline, (unsigned char *)"F6");
	if(tp) {
		char p[STRINGSIZE];
		strcpy(p,(char *)getCstring(tp));
		if(strlen(p)>=sizeof(Option.F6key))error("Maximum % characters",MAXKEYLEN-1);
		else strcpy((char *)Option.F6key, p);
		SaveOptions();
		return;
	}
    tp = checkstring(cmdline, (unsigned char *)"F7");
	if(tp) {
		char p[STRINGSIZE];
		strcpy(p,(char *)getCstring(tp));
		if(strlen(p)>=sizeof(Option.F7key))error("Maximum % characters",MAXKEYLEN-1);
		else strcpy((char *)Option.F7key, p);
		SaveOptions();
		return;
	}
    tp = checkstring(cmdline, (unsigned char *)"F8");
	if(tp) {
		char p[STRINGSIZE];
		strcpy(p,(char *)getCstring(tp));
		if(strlen(p)>=sizeof(Option.F8key))error("Maximum % characters",MAXKEYLEN-1);
		else strcpy((char *)Option.F8key, p);
		SaveOptions();
		return;
	}
    tp = checkstring(cmdline, (unsigned char *)"F9");
	if(tp) {
		char p[STRINGSIZE];
		strcpy(p,(char *)getCstring(tp));
		if(strlen(p)>=sizeof(Option.F9key))error("Maximum % characters",MAXKEYLEN-1);
		else strcpy((char *)Option.F9key, p);
		SaveOptions();
		return;
	}
    tp = checkstring(cmdline, (unsigned char *)"PLATFORM");
	if(tp) {
		char p[STRINGSIZE];
		strcpy(p,(char *)getCstring(tp));
		if(strlen(p)>=sizeof(Option.platform))error("Maximum % characters",sizeof(Option.platform)-1);
		else {
            if(checkstring((unsigned char *)p,(unsigned char *) "GAMEMITE"))  strcpy((char *)Option.platform, "Game*Mite");
            else strcpy((char *)Option.platform, p);
        }
		SaveOptions();
		return;
	}
    if (port_misc_option_setter(cmdline)) return;
    if (port_keyboard_option_setter(cmdline)) return;

    tp = checkstring(cmdline, (unsigned char *)"BAUDRATE");
    if(tp) {
        if(CurrentLinePtr) error("Invalid in a program");
        int i;
        i = getint(tp,Option.CPU_Speed*1000/16/65535,921600);	
        if(i < 100) error("Number out of bounds");
        Option.Baudrate = i;
        SaveOptions();
        if(!Option.SerialConsole)MMPrintString("Value saved but Serial Console not enabled");
        else MMPrintString("Restart to activate");                // set the console baud rate
        return;
    }

    tp = checkstring(cmdline, (unsigned char *)"SERIAL CONSOLE");
    if(tp) {
   	    if(CurrentLinePtr) error("Invalid in a program");
//        unsigned char *p=NULL;
        if(checkstring(tp, (unsigned char *)"DISABLE")) {
            Option.SerialTX=0;
            Option.SerialRX=0;
            Option.SerialConsole = 0; 
            SaveOptions(); 
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;
        } else {
            int pin,pin2;
            getargs(&tp,5,(unsigned char *)",");
            if(!(argc==3 || argc==5))error("Syntax");
            char code;
            if(!(code=codecheck(argv[0])))argv[0]+=2;
            pin = getinteger(argv[0]);
            if(!code)pin=codemap(pin);
            if(!(code=codecheck(argv[2])))argv[2]+=2;
            pin2 = getinteger(argv[2]);
            if(!code)pin2=codemap(pin2);
            if(ExtCurrentConfig[pin] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin,pin);
            if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin2,pin2);
            if(PinDef[pin].mode & UART0TX)Option.SerialTX = pin;
            else if(PinDef[pin].mode & UART0RX)Option.SerialRX = pin;
            else goto checkcom2;
            if(PinDef[pin2].mode & UART0TX)Option.SerialTX = pin2;
            else if(PinDef[pin2].mode & UART0RX)Option.SerialRX = pin2;
            else error("Invalid configuration");
            if(Option.SerialTX==Option.SerialRX)error("Invalid configuration");
            Option.SerialConsole = 1; 
            if(argc==5)Option.SerialConsole=(checkstring(argv[4],(unsigned char *)"B") ? 5: 1);
            SaveOptions(); 
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;
        checkcom2:
            if(PinDef[pin].mode & UART1TX)Option.SerialTX = pin;
            else if(PinDef[pin].mode & UART1RX)Option.SerialRX = pin;
            else error("Invalid configuration");
            if(PinDef[pin2].mode & UART1TX)Option.SerialTX = pin2;
            else if(PinDef[pin2].mode & UART1RX)Option.SerialRX = pin2;
            else error("Invalid configuration");
            if(Option.SerialTX==Option.SerialRX)error("Invalid configuration");
            Option.SerialConsole = 2; 
            if(argc==5)Option.SerialConsole=(checkstring(argv[4],(unsigned char *)"B") ? 6: 2);
            SaveOptions(); 
            _excep_code = RESET_COMMAND;
            SoftReset();
        }  
    }

    tp = checkstring(cmdline, (unsigned char *)"AUTORUN");
    if(tp) {
        getargs(&tp,3,(unsigned char *)",");
		Option.NoReset=0;
        if(argc==3){
            if(checkstring(argv[2], (unsigned char *)"NORESET"))Option.NoReset=1;
            else error("Syntax");
        }
        if(checkstring(argv[0], (unsigned char *)"OFF"))      { Option.Autorun = 0; SaveOptions(); return;  }
        if(checkstring(argv[0], (unsigned char *)"ON"))      { Option.Autorun = MAXFLASHSLOTS+1; SaveOptions(); return;  }
        Option.Autorun=getint(argv[0],0,MAXFLASHSLOTS);
        SaveOptions(); return; 
    } 
    if (port_pico_pins_option_setter(cmdline)) return;
tp = checkstring(cmdline, (unsigned char *)"DEFAULT COLOURS");
if(tp==NULL)tp = checkstring(cmdline, (unsigned char *)"DEFAULT COLORS");
if(tp){
    int DefaultFC=WHITE;
    int DefaultBC=BLACK;
    getargs(&tp,3, (unsigned char *)",");
    if(checkstring(argv[0], (unsigned char *)"WHITE"))        { DefaultFC=WHITE;}
    else if(checkstring(argv[0], (unsigned char *)"YELLOW"))  { DefaultFC=YELLOW;}
    else if(checkstring(argv[0], (unsigned char *)"LILAC"))   { DefaultFC=LILAC;}
    else if(checkstring(argv[0], (unsigned char *)"BROWN"))   { DefaultFC=BROWN;}
    else if(checkstring(argv[0], (unsigned char *)"FUCHSIA")) { DefaultFC=FUCHSIA;}
    else if(checkstring(argv[0], (unsigned char *)"RUST"))    { DefaultFC=RUST;}
    else if(checkstring(argv[0], (unsigned char *)"MAGENTA")) { DefaultFC=MAGENTA;}
    else if(checkstring(argv[0], (unsigned char *)"RED"))     { DefaultFC=RED;}
    else if(checkstring(argv[0], (unsigned char *)"CYAN"))    { DefaultFC=CYAN;}
    else if(checkstring(argv[0], (unsigned char *)"GREEN"))   { DefaultFC=GREEN;}
    else if(checkstring(argv[0], (unsigned char *)"CERULEAN")){ DefaultFC=CERULEAN;}
    else if(checkstring(argv[0], (unsigned char *)"MIDGREEN")){ DefaultFC=MIDGREEN;}
    else if(checkstring(argv[0], (unsigned char *)"COBALT"))  { DefaultFC=COBALT;}
    else if(checkstring(argv[0], (unsigned char *)"MYRTLE"))  { DefaultFC=MYRTLE;}
    else if(checkstring(argv[0], (unsigned char *)"BLUE"))    { DefaultFC=BLUE;}
    else if(checkstring(argv[0], (unsigned char *)"BLACK"))   { DefaultFC=BLACK;}
    else error("Invalid colour: $", argv[0]); 
    if(argc==3){
        if(checkstring(argv[2], (unsigned char *)"WHITE"))        { DefaultBC=WHITE;}
        else if(checkstring(argv[2], (unsigned char *)"YELLOW"))  { DefaultBC=YELLOW;}
        else if(checkstring(argv[2], (unsigned char *)"LILAC"))   { DefaultBC=LILAC;}
        else if(checkstring(argv[2], (unsigned char *)"BROWN"))   { DefaultBC=BROWN;}
        else if(checkstring(argv[2], (unsigned char *)"FUCHSIA")) { DefaultBC=FUCHSIA;}
        else if(checkstring(argv[2], (unsigned char *)"RUST"))    { DefaultBC=RUST;}
        else if(checkstring(argv[2], (unsigned char *)"MAGENTA")) { DefaultBC=MAGENTA;}
        else if(checkstring(argv[2], (unsigned char *)"RED"))     { DefaultBC=RED;}
        else if(checkstring(argv[2], (unsigned char *)"CYAN"))    { DefaultBC=CYAN;}
        else if(checkstring(argv[2], (unsigned char *)"GREEN"))   { DefaultBC=GREEN;}
        else if(checkstring(argv[2], (unsigned char *)"CERULEAN")){ DefaultBC=CERULEAN;}
        else if(checkstring(argv[2], (unsigned char *)"MIDGREEN")){ DefaultBC=MIDGREEN;}
        else if(checkstring(argv[2], (unsigned char *)"COBALT"))  { DefaultBC=COBALT;}
        else if(checkstring(argv[2], (unsigned char *)"MYRTLE"))  { DefaultBC=MYRTLE;}
        else if(checkstring(argv[2], (unsigned char *)"BLUE"))    { DefaultBC=BLUE;}
        else if(checkstring(argv[2], (unsigned char *)"BLACK"))   { DefaultBC=BLACK;}
        else error("Invalid colour: $", argv[2]); 
    }      
    if(DefaultBC==DefaultFC)error("Foreground and Background colours are the same");
    Option.DefaultBC=DefaultBC;
    Option.DefaultFC=DefaultFC;
    SaveOptions();
    ResetDisplay();
    if(Option.DISPLAY_TYPE!=SCREENMODE1)ClearScreen(gui_bcolour);
    return;
}
    if (port_heartbeat_option_setter(cmdline)) return;
    tp = checkstring(cmdline, (unsigned char *)"LCDPANEL NOCONSOLE");
    if(tp){
   	    if(CurrentLinePtr) error("Invalid in a program");
        Option.Height = SCREENHEIGHT; Option.Width = SCREENWIDTH;
        Option.DISPLAY_CONSOLE = 0;
        Option.DefaultFC = WHITE;
        Option.DefaultBC = BLACK;
        SetFont((Option.DefaultFont = (Option.DISPLAY_TYPE==SCREENMODE2? (6<<4) | 1 : 0x01 )));
        Option.BackLightLevel = 100;
        Option.NoScroll=0;
        Option.Height = SCREENHEIGHT;
        Option.Width = SCREENWIDTH;
        SaveOptions();
        setterminal(Option.Height,Option.Width);
        ClearScreen(Option.DefaultBC);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"LCDPANEL CONSOLE");
    if(tp) {
   	    if(CurrentLinePtr) error("Invalid in a program");
        Option.NoScroll = 0;
        if(!(Option.DISPLAY_TYPE==ST7789B || Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE == ST7796SP  || Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ILI9488P || Option.DISPLAY_TYPE==ILI9341 || Option.DISPLAY_TYPE>=VGADISPLAY))Option.NoScroll=1;
        if(!(Option.DISPLAY_ORIENTATION == DISPLAY_LANDSCAPE) && Option.DISPLAY_TYPE==SSDTYPE) error("Landscape only");
        skipspace(tp);
        Option.DefaultFC = WHITE;
        Option.DefaultBC = BLACK;
        int font;
        Option.BackLightLevel = 100;
        if(!(*tp == 0 || *tp == '\'')) {
            getargs(&tp, 9, (unsigned char *)",");                              // this is a macro and must be the first executable stmt in a block
            if(argc > 0) {
                if(*argv[0] == '#') argv[0]++;                  // skip the hash if used
                font = (((getint(argv[0], 1, FONT_BUILTIN_NBR) - 1) << 4) | 1);
                if(FontTable[font >> 4][0]*29>HRes)error("Font too wide for console mode");
                Option.DefaultFont=font;
            }
            if(argc > 2 && *argv[2]) Option.DefaultFC = getint(argv[2], BLACK, WHITE);
            if(argc > 4 && *argv[4]) Option.DefaultBC = getint(argv[4], BLACK, WHITE);
            if(Option.DefaultFC == Option.DefaultBC) error("Same colours");
            if(argc > 6 && *argv[6]) {
                if(!Option.DISPLAY_BL)error("Backlight not available on this display");
                Option.BackLightLevel = getint(argv[6], 0, 100);
            }
            if(argc==9){
                if(checkstring(argv[8],(unsigned char *)"NOSCROLL")){
                    if(!FASTSCROLL)Option.NoScroll=1;
                    else error("Invalid for this display");
                } else error("Syntax");
            }
        }
        if(Option.DISPLAY_BL){
			MMFLOAT frequency=1000.0,duty=Option.BackLightLevel;
            int wrap=(Option.CPU_Speed*1000)/frequency;
            int high=(int)((MMFLOAT)Option.CPU_Speed/frequency*duty*10.0);
            int div=1;
            while(wrap>65535){
                wrap>>=1;
                if(duty>=0.0)high>>=1;
                div<<=1;
            }
            wrap--;
            if(div!=1)pwm_set_clkdiv(BacklightSlice,(float)div);
            pwm_set_wrap(BacklightSlice, wrap);
            pwm_set_chan_level(BacklightSlice, BacklightChannel, high);
        }
        port_apply_default_console_colors(Option.DefaultFC, Option.DefaultBC);
        Option.DISPLAY_CONSOLE = true; 
        if(!CurrentLinePtr) {
            ResetDisplay();
            //Only setterminal if console is bigger than 80*24
            if  (Option.Width > SCREENWIDTH || Option.Height > SCREENHEIGHT){ 
               setterminal((Option.Height > SCREENHEIGHT)?Option.Height:SCREENHEIGHT,(Option.Width >= SCREENWIDTH)?Option.Width:SCREENWIDTH);                                                    // or height is > 24
            }
            SaveOptions();
            if(!(Option.DISPLAY_TYPE==SCREENMODE1 || Option.DISPLAY_TYPE==SCREENMODE2))ClearScreen(Option.DefaultBC);
        }
        return;
    }

    tp = checkstring(cmdline, (unsigned char *)"LEGACY");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"OFF"))      { CMM1=0; return;  }
        if(checkstring(tp, (unsigned char *)"ON"))      { CMM1=1; return;  }
        error("Syntax");
    }
    tp = checkstring(cmdline, (unsigned char *)"BACKLIGHT KB");
    if(tp) {
        getargs(&tp,1,(unsigned char *)",");
        int level = getint(argv[0],0,255);
        port_picocalc_set_keyboard_backlight(level);
        SaveOptions();
        return;
    }
    if (port_web_option_setter(cmdline)) return;

    if (port_display_option_setter(cmdline)) return;
    tp = checkstring(cmdline, (unsigned char *)"DISPLAY");
    if(tp) {
        getargs(&tp, 3, (unsigned char *)",");
        if(Option.DISPLAY_CONSOLE && argc>0 ) error("Cannot change LCD console");
        if(argc >= 1) Option.Height = getint(argv[0], 5, 100);
        if(argc == 3) Option.Width = getint(argv[2], 37, 240);
        if (Option.DISPLAY_CONSOLE) {
           setterminal((Option.Height > SCREENHEIGHT)?Option.Height:SCREENHEIGHT,(Option.Width > SCREENWIDTH)?Option.Width:SCREENWIDTH);                                                    // or height is > 24
        }else{
           setterminal(Option.Height,Option.Width);
        }
        if(argc >= 1 )SaveOptions();  //Only save if necessary
        return;
    }
#ifdef GUICONTROLS
    tp = checkstring(cmdline,(unsigned char *)"GUI CONTROLS");
    if(tp) {
        getargs(&tp, 1, (unsigned char *)",");
    	if(CurrentLinePtr) error("Invalid in a program");
        Option.MaxCtrls=getint(argv[0],0,MAXCONTROLS-1);
        if(Option.MaxCtrls)Option.MaxCtrls++;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
    }
#endif
    tp = checkstring(cmdline, (unsigned char *)"CASE");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"LOWER"))    { Option.Listcase = CONFIG_LOWER; SaveOptions(); return; }
        if(checkstring(tp, (unsigned char *)"UPPER"))    { Option.Listcase = CONFIG_UPPER; SaveOptions(); return; }
        if(checkstring(tp, (unsigned char *)"TITLE"))    { Option.Listcase = CONFIG_TITLE; SaveOptions(); return; }
    }

    tp = checkstring(cmdline, (unsigned char *)"TAB");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"2"))        { Option.Tab = 2; SaveOptions(); return; }
		if(checkstring(tp, (unsigned char *)"3"))		{ Option.Tab = 3; SaveOptions(); return; }
        if(checkstring(tp, (unsigned char *)"4"))        { Option.Tab = 4; SaveOptions(); return; }
        if(checkstring(tp, (unsigned char *)"8"))        { Option.Tab = 8; SaveOptions(); return; }
    }
    tp = checkstring(cmdline, (unsigned char *)"VCC");
    if(tp) {
        MMFLOAT f;
        f = getnumber(tp);
        if(f > 3.6) error("VCC too high");
        if(f < 1.8) error("VCC too low");
        VCC=f;
        return;
    }

    tp = checkstring(cmdline, (unsigned char *)"PIN");
    if(tp) {
        int i;
        i = getint(tp, 0, 99999999);
        Option.PIN = i;
        SaveOptions();
        return;
    }

    tp = checkstring(cmdline, (unsigned char *)"POWER");
    if(tp) {
        /* RP2350B (rp2350a==false) has no PWR_EN power-control pin. */
        if (HAL_PORT_PWM_SLICE_COUNT > 8 && !rp2350a) error("Invalid for RP2350B");
        if(Option.AllPins)error("OPTION PICO set");
        if(checkstring(tp, (unsigned char *)"PWM"))  Option.PWM = true;
        if(checkstring(tp, (unsigned char *)"PFM"))  Option.PWM = false;
        SaveOptions();
        hal_pin_set_mode(23, HAL_PIN_MODE_OUTPUT);
        hal_pin_write(23, Option.PWM);
        return;
    }

    tp = checkstring(cmdline, (unsigned char *)"COLOURCODE");
    if(tp == NULL) tp = checkstring(cmdline, (unsigned char *)"COLORCODE");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"ON"))       { Option.ColourCode = true; SaveOptions(); return; }
        else if(checkstring(tp, (unsigned char *)"OFF"))      { Option.ColourCode = false; SaveOptions(); return;  }
        else error("Syntax");
    }

    tp = checkstring(cmdline, (unsigned char *)"RTC AUTO");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"ENABLE"))       { Option.RTC = true; SaveOptions(); RtcGetTime(0); return; }
        if(checkstring(tp, (unsigned char *)"DISABLE"))      { Option.RTC = false; SaveOptions(); return;  }
    }
    tp = checkstring(cmdline, (unsigned char *)"CONTINUATION LINES");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"ENABLE"))       { Option.continuation = '_'; SaveOptions(); return; }
        else if(checkstring(tp, (unsigned char *)"DISABLE"))      { Option.continuation = false; SaveOptions(); return;  }
        else if(checkstring(tp, (unsigned char *)"ON"))      { Option.continuation = '_'; SaveOptions(); return;  }
        else if(checkstring(tp, (unsigned char *)"OFF"))      { Option.continuation = false; SaveOptions(); return;  }
        else error("Syntax");
    }

    tp = checkstring(cmdline, (unsigned char *)"MODBUFF");
    if(tp) {
        unsigned char *p=NULL;
        int i, size=0;
    	if(CurrentLinePtr) error("Invalid in a program");
        if((p=checkstring(tp, (unsigned char *)"ENABLE"))){
            if(!Option.modbuff)       { 
                getargs(&p,1,(unsigned char *)",");
                if(argc){
                    size=getint(argv[0],16,(Option.FlashSize-RoundUpK4(TOP_OF_SYSTEM_FLASH))/1024-132);
                    if(size & 3)error("Must be a multiple of 4");
                }
                MMPrintString("\r\nThis will erase everything in flash including the A: drive - are you sure (Y/N) ? ");
                while((i = MMInkey())==-1){};
                putConsole(i,1);
                if(toupper(i)!='Y'){
                    memset(inpbuf,0,STRINGSIZE);
                    longjmp(mark,1);
                }
                if(argc)Option.modbuffsize=size;
                else Option.modbuffsize=128;
                Option.modbuff = true; 
                SaveOptions(); 
                ResetFlashStorage(1); 
                modbuff=(char *)(XIP_BASE + RoundUpK4(TOP_OF_SYSTEM_FLASH));
                _excep_code = RESET_COMMAND;
                SoftReset();
                }
            else error("Already enabled");
        }
        if(checkstring(tp, (unsigned char *)"DISABLE")){
            if(Option.modbuff)      { 
                MMPrintString("\r\nThis will erase everything in flash including the A: drive - are you sure (Y/N) ? ");
                while((i = MMInkey())==-1){};
                putConsole(i,1);
                if(toupper(i)!='Y'){
                    memset(inpbuf,0,STRINGSIZE);
                    longjmp(mark,1);
                }
                Option.modbuff = false; 
                Option.modbuffsize=0;
                SaveOptions(); 
                ResetFlashStorage(1); 
                modbuff=NULL;
                _excep_code = RESET_COMMAND;
                SoftReset();
            }
            else error("Not enabled");
        }
    }

	tp = checkstring(cmdline, (unsigned char *)"LIST");
    if(tp) {
    	printoptions();
    	return;
    }
    tp = checkstring(cmdline, (unsigned char *)"AUDIO");
    if(tp) {
        int pin1,pin2, slice;
        unsigned char *p;
    	if(CurrentLinePtr) error("Invalid in a program");
        if(checkstring(tp, (unsigned char *)"DISABLE")){
            disable_audio();
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;                                // this will restart the processor ? only works when not in debug
        }
        if((p=checkstring(tp, (unsigned char *)"VS1053"))){
            int pin1,pin2,pin3,pin4,pin5,pin6,pin7;
            getargs(&p,13,(unsigned char *)",");
            if(argc!=13)error("Syntax");
            if(Option.AUDIO_CLK_PIN || Option.AUDIO_L)error("Audio already configured");
            unsigned char code;
//
            if(!(code=codecheck(argv[0])))argv[0]+=2;
            pin1 = getinteger(argv[0]);
            if(!code)pin1=codemap(pin1);
            if(IsInvalidPin(pin1)) error("Invalid pin");
            if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin1,pin1);
//
            if(!(code=codecheck(argv[2])))argv[2]+=2;
            pin2 = getinteger(argv[2]);
            if(!code)pin2=codemap(pin2);
            if(IsInvalidPin(pin2)) error("Invalid pin");
            if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin2,pin2);
//
            if(!(code=codecheck(argv[4])))argv[4]+=2;
            pin3 = getinteger(argv[4]);
            if(!code)pin3=codemap(pin3);
            if(IsInvalidPin(pin3)) error("Invalid pin");
            if(ExtCurrentConfig[pin3] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin3,pin3);
//
            if(!(code=codecheck(argv[6])))argv[6]+=2;
            pin4 = getinteger(argv[6]);
            if(!code)pin4=codemap(pin4);
            if(IsInvalidPin(pin4)) error("Invalid pin");
            if(ExtCurrentConfig[pin4] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin4,pin4);
//
            if(!(code=codecheck(argv[8])))argv[8]+=2;
            pin5 = getinteger(argv[8]);
            if(!code)pin5=codemap(pin5);
            if(IsInvalidPin(pin5)) error("Invalid pin");
            if(ExtCurrentConfig[pin5] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin5,pin5);
//
            if(!(code=codecheck(argv[10])))argv[10]+=2;
            pin6 = getinteger(argv[10]);
            if(!code)pin6=codemap(pin6);
            if(IsInvalidPin(pin6)) error("Invalid pin");
            if(ExtCurrentConfig[pin6] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin6,pin6);
//
            if(!(code=codecheck(argv[12])))argv[12]+=2;
            pin7 = getinteger(argv[12]);
            if(!code)pin7=codemap(pin7);
            if(IsInvalidPin(pin7)) error("Invalid pin");
            if(ExtCurrentConfig[pin7] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin7,pin7);
//
            if(!(PinDef[pin1].mode & SPI0SCK && PinDef[pin2].mode & SPI0TX && PinDef[pin3].mode & SPI0RX) &&
            !(PinDef[pin1].mode & SPI1SCK && PinDef[pin2].mode & SPI1TX && PinDef[pin3].mode & SPI1RX))error("Not valid SPI pins");
            if(PinDef[pin1].mode & SPI0SCK && SPI0locked)error("SPI channel already configured for System SPI");
            if(PinDef[pin1].mode & SPI1SCK && SPI1locked)error("SPI channel already configured for System SPI");
            Option.AUDIO_CLK_PIN=pin1;
            Option.AUDIO_MOSI_PIN=pin2;
            Option.AUDIO_MISO_PIN=pin3;
            Option.AUDIO_CS_PIN=pin4;
            Option.AUDIO_DCS_PIN=pin5;
            Option.AUDIO_DREQ_PIN=pin6;
            Option.AUDIO_RESET_PIN=pin7;
            slice=checkslice(pin2,pin2, 1);
            if((PinDef[Option.DISPLAY_BL].slice & 0x7f) == slice) error("Channel in use for backlight");
            Option.AUDIO_SLICE=slice;
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;
        }
        if((p=checkstring(tp, (unsigned char *)"SPI"))){
            int pin1,pin2,pin3;
            getargs(&p,5,(unsigned char *)",");
            if(argc!=5)error("Syntax");
            if(Option.AUDIO_CLK_PIN || Option.AUDIO_L)error("Audio already configured");
            unsigned char code;
//
            if(!(code=codecheck(argv[0])))argv[0]+=2;
            pin1 = getinteger(argv[0]);
            if(!code)pin1=codemap(pin1);
            if(IsInvalidPin(pin1)) error("Invalid pin");
            if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin1,pin1);
//
            if(!(code=codecheck(argv[2])))argv[2]+=2;
            pin2 = getinteger(argv[2]);
            if(!code)pin2=codemap(pin2);
            if(IsInvalidPin(pin2)) error("Invalid pin");
            if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin2,pin2);
//
            if(!(code=codecheck(argv[4])))argv[4]+=2;
            pin3 = getinteger(argv[4]);
            if(!code)pin3=codemap(pin3);
            if(IsInvalidPin(pin3)) error("Invalid pin");
            if(ExtCurrentConfig[pin3] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin3,pin3);
//
            if(!(PinDef[pin2].mode & SPI0SCK && PinDef[pin3].mode & SPI0TX) &&
            !(PinDef[pin2].mode & SPI1SCK && PinDef[pin3].mode & SPI1TX))error("Not valid SPI pins");
            Option.AUDIO_CS_PIN=pin1;
            Option.AUDIO_CLK_PIN=pin2;
            Option.AUDIO_MOSI_PIN=pin3;
            /* RP2350A (rp2350a==true) and rp2040 (rp2350a default-true)
             * use checkslice; RP2350B reserves slice 11 for audio. */
            if (HAL_PORT_PWM_SLICE_COUNT > 8 && rp2350a) slice = 11;
            else slice = checkslice(pin2, pin2, 1);
            if((PinDef[Option.DISPLAY_BL].slice & 0x7f) == slice) error("Channel in use for backlight");
            Option.AUDIO_SLICE=slice;
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;
        }
        if((p=checkstring(tp, (unsigned char *)"I2S"))){
            int pin1,pin2,pin3;
            getargs(&p,3,(unsigned char *)",");
            if(argc!=3)error("Syntax");
            if(Option.AUDIO_CLK_PIN || Option.AUDIO_L || Option.audio_i2s_bclk)error("Audio already configured");
            unsigned char code;
//
            if(!(code=codecheck(argv[0])))argv[0]+=2;
            pin1 = getinteger(argv[0]);
            if(!code)pin1=codemap(pin1);
            if(IsInvalidPin(pin1)) error("Invalid pin");
            if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin1,pin1);
//
            pin3 = PINMAP[PinDef[pin1].GPno+1];
            if(IsInvalidPin(pin3)) error("Invalid pin");
            if(ExtCurrentConfig[pin3] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin3,pin3);
//
            if(!(code=codecheck(argv[2])))argv[2]+=2;
            pin2 = getinteger(argv[2]);
            if(!code)pin2=codemap(pin2);
            if(IsInvalidPin(pin2)) error("Invalid pin");
            if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG || pin2==pin1 || pin2==pin3)  error("Pin %/| is in use",pin2,pin2);

            slice = port_audio_i2s_pio_slice(pin1, pin2);
            Option.audio_i2s_bclk=pin1;
            Option.audio_i2s_data=pin2;
            if((PinDef[Option.DISPLAY_BL].slice & 0x7f) == slice) error("Channel in use for backlight");
            Option.AUDIO_SLICE=slice;
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;
        }
    	getargs(&tp,3,(unsigned char *)",");
         if(argc!=3)error("Syntax");
        if(Option.AUDIO_CLK_PIN || Option.AUDIO_L)error("Audio already configured");
        unsigned char code;
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        pin1 = getinteger(argv[0]);
        if(!code)pin1=codemap(pin1);
        if(IsInvalidPin(pin1)) error("Invalid pin");
        if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin1,pin1);
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin2 = getinteger(argv[2]);
        if(!code)pin2=codemap(pin2);
        if(IsInvalidPin(pin2)) error("Invalid pin");
        if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin2,pin2);
        slice=checkslice(pin1,pin2, 0);
        if((PinDef[Option.DISPLAY_BL].slice & 0x7f) == slice) error("Channel in use for backlight");
        Option.AUDIO_L=pin1;
        Option.AUDIO_R=pin2;
        Option.AUDIO_SLICE=slice;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }

    tp = checkstring(cmdline, (unsigned char *)"SYSTEM I2C");
    if(tp) {
        int pin1,pin2,channel=-1;
        if(checkstring(tp, (unsigned char *)"DISABLE")){
   	    if(CurrentLinePtr) error("Invalid in a program");
        /* Non-VGA targets also disallow disabling I2C if the SSD1306
         * I2C panel is using it; VGA never has SSD1306I2C. */
        if (Option.RTC_Clock || Option.RTC_Data) error("In use");
        if (!HAL_PORT_IS_VGA &&
            (Option.DISPLAY_TYPE == SSD1306I2C || Option.DISPLAY_TYPE == SSD1306I2C32))
            error("In use");
            disable_systemi2c();
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;                                // this will restart the processor ? only works when not in debug
        }
    	getargs(&tp,5,(unsigned char *)",");
   	    if(CurrentLinePtr) error("Invalid in a program");
         if(argc<3)error("Syntax");
        if(Option.SYSTEM_I2C_SCL)error("I2C already configured");
        unsigned char code;
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        pin1 = getinteger(argv[0]);
        if(!code)pin1=codemap(pin1);
        if(IsInvalidPin(pin1)) error("Invalid pin");
        if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin1,pin1);
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin2 = getinteger(argv[2]);
        if(!code)pin2=codemap(pin2);
        if(IsInvalidPin(pin2)) error("Invalid pin");
        if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin2,pin2);
        if(PinDef[pin1].mode & I2C0SDA && PinDef[pin2].mode & I2C0SCL)channel=0;
        if(PinDef[pin1].mode & I2C1SDA && PinDef[pin2].mode & I2C1SCL)channel=1;
        if(channel==-1)error("Invalid I2C pins");
        Option.SYSTEM_I2C_SLOW=0;
        if(argc==5){
            if(checkstring(argv[4], (unsigned char *)"SLOW"))Option.SYSTEM_I2C_SLOW=1;
            else if(checkstring(argv[4],(unsigned char *)"FAST"))Option.SYSTEM_I2C_SLOW=0;
            else error("Syntax");
        }
        Option.SYSTEM_I2C_SDA=pin1;
        Option.SYSTEM_I2C_SCL=pin2;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"COUNT");
    if(tp) {
        int pin1,pin2,pin3,pin4;
        if(CallBackEnabled==2) picomite_gpio_irq_set_enabled(PinDef[Option.INT1pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        else if(CallBackEnabled & 2){
            hal_pin_irq_set_edge(PinDef[Option.INT1pin].GPno, HAL_PIN_EDGE_BOTH, false);
            CallBackEnabled &= (~2);
        }
        if(CallBackEnabled==4) picomite_gpio_irq_set_enabled(PinDef[Option.INT2pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        else if(CallBackEnabled & 4){
            hal_pin_irq_set_edge(PinDef[Option.INT2pin].GPno, HAL_PIN_EDGE_BOTH, false);
            CallBackEnabled &= (~4);
        }
        if(CallBackEnabled==8) picomite_gpio_irq_set_enabled(PinDef[Option.INT3pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        else  if(CallBackEnabled & 8){
            hal_pin_irq_set_edge(PinDef[Option.INT3pin].GPno, HAL_PIN_EDGE_BOTH, false);
            CallBackEnabled &= (~8);
        }
        if(CallBackEnabled==16) picomite_gpio_irq_set_enabled(PinDef[Option.INT4pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        else  if(CallBackEnabled & 16){
            hal_pin_irq_set_edge(PinDef[Option.INT4pin].GPno, HAL_PIN_EDGE_BOTH, false);
            CallBackEnabled &= (~16);
        }
    	getargs(&tp,7,(unsigned char *)",");
        if(argc!=7)error("Syntax");
        unsigned char code;
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        pin1 = getinteger(argv[0]);
        if(!code)pin1=codemap(pin1);
        if(IsInvalidPin(pin1)) error("Invalid pin");
        if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin1,pin1);
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin2 = getinteger(argv[2]);
        if(!code)pin2=codemap(pin2);
        if(IsInvalidPin(pin2)) error("Invalid pin");
        if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin2,pin2);
        if(!(code=codecheck(argv[4])))argv[4]+=2;
        pin3 = getinteger(argv[4]);
        if(!code)pin3=codemap(pin3);
        if(IsInvalidPin(pin3)) error("Invalid pin");
        if(ExtCurrentConfig[pin3] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin3,pin3);
        if(!(code=codecheck(argv[6])))argv[6]+=2;
        pin4 = getinteger(argv[6]);
        if(!code)pin4=codemap(pin4);
        if(IsInvalidPin(pin4)) error("Invalid pin");
        if(ExtCurrentConfig[pin4] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin4,pin4);
        if(pin1==pin2 || pin1==pin3 || pin1==pin4 || pin2==pin3 || pin2==pin4 || pin3==pin4)error("Pins must be unique");
        Option.INT1pin=pin1;
        Option.INT2pin=pin2;
        Option.INT3pin=pin3;
        Option.INT4pin=pin4;
        SaveOptions();
        return;
    }
    if (port_system_lcd_spi_option_setter(cmdline)) return;
	tp = checkstring(cmdline, (unsigned char *)"SDCARD");
    int pin1, pin2, pin3, pin4;
    if(CurrentLinePtr) error("Invalid in a program");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"DISABLE")){
            FatFSFileSystem=0;
            disable_sd();
            SaveOptions();
            return;                                // this will restart the processor ? only works when not in debug
        }
        /* COMBINED CS: SD shares the touch-controller's CS pin to free
         * up an external pin. Not available on VGA — VGA uses dedicated
         * SD pins from the SDIO lines. */
        if (!HAL_PORT_IS_VGA && checkstring(tp, (unsigned char *)"COMBINED CS")) {
            if (Option.SD_CS || Option.CombinedCS) error("SDcard already configured");
            if (!Option.SYSTEM_CLK) error("System SPI not configured");
            if (!Option.TOUCH_CS) error("Touch CS pin not configured");
            Option.CombinedCS = 1;
            Option.SD_CS = 0;
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;
        }
    	getargs(&tp,7,(unsigned char *)",");
        /* VGA forces all 7 args (no implicit SYSTEM SPI sharing); other
         * ports allow 1-arg form (CS only) or full 7-arg. */
        if (HAL_PORT_IS_VGA) {
            if (argc != 7) error("Syntax");
        } else {
            if (!(argc == 1 || argc == 7)) error("Syntax");
        }
         if(Option.SD_CS || Option.CombinedCS)error("SDcard already configured");
        if(argc==1 && !Option.SYSTEM_CLK)error("System SPI not configured");
        unsigned char code;
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        pin4 = getinteger(argv[0]);
        if(!code)pin4=codemap(pin4);
        if(IsInvalidPin(pin4)) error("Invalid pin");
        if(ExtCurrentConfig[pin4] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin4,pin4);
        Option.SD_CS=pin4;
        Option.SDspeed=12;
        Option.SD_CLK_PIN=0;
        Option.SD_MOSI_PIN=0;
        Option.SD_MISO_PIN=0;
        if(argc>1){
            if(!(code=codecheck(argv[2])))argv[2]+=2;
            pin1 = getinteger(argv[2]);
            if(!code)pin1=codemap(pin1);
            if(IsInvalidPin(pin1)) error("Invalid pin");
            if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin1,pin1);
            if(!(code=codecheck(argv[4])))argv[4]+=2;
            pin2 = getinteger(argv[4]);
            if(!code)pin2=codemap(pin2);
            if(IsInvalidPin(pin2)) error("Invalid pin");
            if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin2,pin2);
            if(!(code=codecheck(argv[6])))argv[6]+=2;
            pin3 = getinteger(argv[6]);
            if(!code)pin3=codemap(pin3);
            if(IsInvalidPin(pin3)) error("Invalid pin");
            if(ExtCurrentConfig[pin3] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin3,pin3);
            /* VGA shares SPI0/SPI1 hw pins with SD when the chosen pins
             * match an SPI peripheral. Other ports always assign
             * dedicated SD pins. */
            int sdcard_via_system_spi = 0;
            if (HAL_PORT_IS_VGA && !Option.SYSTEM_CLK) {
                if (PinDef[pin1].mode & SPI0SCK && PinDef[pin2].mode & SPI0TX && PinDef[pin3].mode & SPI0RX) {
                    Option.SYSTEM_CLK=pin1;
                    Option.SYSTEM_MOSI=pin2;
                    Option.SYSTEM_MISO=pin3;
                    MMPrintString("SPI channel 0 in use for SDcard\r\n");
                    sdcard_via_system_spi = 1;
                } else if (PinDef[pin1].mode & SPI1SCK && PinDef[pin2].mode & SPI1TX && PinDef[pin3].mode & SPI1RX) {
                    Option.SYSTEM_CLK=pin1;
                    Option.SYSTEM_MOSI=pin2;
                    Option.SYSTEM_MISO=pin3;
                    MMPrintString("SPI channel 1 in use for SDcard\r\n");
                    sdcard_via_system_spi = 1;
                }
            }
            if (!sdcard_via_system_spi) {
                Option.SD_CLK_PIN=pin1;
                Option.SD_MOSI_PIN=pin2;
                Option.SD_MISO_PIN=pin3;
            }
        }
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
	tp = checkstring(cmdline, (unsigned char *)"DISK SAVE");
    if(tp){
        getargs(&tp,1,(unsigned char *)",");
        if(!(argc==1))error("Syntax");
        if(CurrentLinePtr) error("Invalid in a program");
        int fnbr = FindFreeFileNbr();
        if (!InitSDCard())  return;
        char *pp = (char *)getFstring(argv[0]);
        if (strchr((char *)pp, '.') == NULL)
            strcat((char *)pp, ".opt");
        if (!BasicFileOpen((char *)pp, fnbr, FA_WRITE | FA_CREATE_ALWAYS)) return;
        int i = sizeof(Option);
        char *s = (char *)&Option.Magic;
        while(i--){
            FilePutChar(*s++,fnbr);
        }
        FileClose(fnbr);
        return;
    }
	tp = checkstring(cmdline, (unsigned char *)"DISK LOAD");
    if(tp){
        getargs(&tp,1,(unsigned char *)",");
    	if(CurrentLinePtr) error("Invalid in a program");
        if(!(argc==1))error("Syntax");
        int fnbr = FindFreeFileNbr();
        int fsize;
        if (!InitSDCard())  return;
        char *pp = (char *)getFstring(argv[0]);
        if (strchr((char *)pp, '.') == NULL)
            strcat((char *)pp, ".opt");
        if (!BasicFileOpen((char *)pp, fnbr, FA_READ)) return;
		fsize = (int)hal_fs_size(hal_fds[fnbr]);
        if(!(fsize==sizeof(Option) || fsize==sizeof(Option)-128))error("File size incorrect");
        char *s=(char *)&Option.Magic;
        for(int k = 0; k < fsize; k++){        // write to the flash byte by byte
           *s++=FileGetChar(fnbr);
        }
        Option.Magic=MagicKey; //This isn't ideal but it improves the chances of a older config working in a new build
        FileClose(fnbr);
        uSec(100000);
        disable_interrupts_pico();
        hal_flash_write_options(&Option, sizeof(struct option_s));
        enable_interrupts_pico();
        _excep_code = RESET_COMMAND;
        SoftReset();
    }
	tp = checkstring(cmdline, (unsigned char *)"RESET");
    if(tp) {
   	    if(CurrentLinePtr) error("Invalid in a program");
        if(Option.LIBRARY_FLASH_SIZE==MAX_PROG_SIZE) {
          uint32_t j = FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE + SAVEDVARS_FLASH_SIZE + ((MAXFLASHSLOTS - 1) * MAX_PROG_SIZE);
          uSec(250000);
          disable_interrupts_pico();
          hal_flash_erase(j, MAX_PROG_SIZE);
          enable_interrupts_pico();
        }
        configure(tp);
        return;
    }
    error("Invalid Option");
}

void fun_device(void){
    sret = GetTempMemory(STRINGSIZE);
    /* Device name string baked in at compile time per COMPILE variant
     * (CMakeLists.txt + host/port_config.h). RP2350A/B suffix appended at
     * runtime when applicable; rp2350a flag is true on RP2040 too, so the
     * suffix only fires for actual RP2350 builds via Option.CPU_Speed
     * range or runtime probe (kept in vm_sys_pin.c). On non-rp2350 builds
     * the conditional becomes dead at compile time. */
    strcpy((char *)sret, HAL_PORT_DEVICE_NAME);
    /* Suffix the chip variant on RP2350 ports. PIO count distinguishes
     * RP2040 (2 PIOs) from RP2350 (3 PIOs); the rp2350a flag is true
     * on RP2040 by default but only consulted on rp2350 ports. */
    if (HAL_PORT_PIO_COUNT > 2) {
        strcat((char *)sret, rp2350a ? " RP2350A" : " RP2350B");
    }
    CtoM(sret);
    targ = T_STR;
}

uint32_t __get_MSP(void)
{
  uint32_t result;

  __asm volatile ("MRS %0, msp" : "=r" (result) );
  return(result);
}
int FileSize(char *p){
    char q[FF_MAX_LFN]={0};
    int retval=0;
    int waste=0, t=FatFSFileSystem+1;
    int localfilesystemsave=FatFSFileSystem;
    t = drivecheck(p,&waste);
    p+=waste;
    getfullfilename(p,q);
    FatFSFileSystem=t-1;
    if(FatFSFileSystem==0){
        struct lfs_info lfsinfo;
        memset(&lfsinfo,0,sizeof(DIR));
        FSerror = lfs_stat(&lfs, q, &lfsinfo);
        if(lfsinfo.type==LFS_TYPE_REG)retval= lfsinfo.size;
    } else {
        DIR djd;
        FILINFO fnod;
        memset(&djd,0,sizeof(DIR));
        memset(&fnod,0,sizeof(FILINFO));
        if(!InitSDCard()) return -1;
        FSerror = f_stat(q, &fnod);
        if(FSerror != FR_OK)iret=0;
        else if(!(fnod.fattrib & AM_DIR))retval=fnod.fsize;
    }
    FatFSFileSystem=localfilesystemsave;
    return retval;
}
int ExistsFile(char *p){
    char q[FF_MAX_LFN]={0};
    int retval=0;
    int waste=0, t=FatFSFileSystem+1;
    int localfilesystemsave=FatFSFileSystem;
    t = drivecheck(p,&waste);
    p+=waste;
    getfullfilename(p,q);
    FatFSFileSystem=t-1;
    if(FatFSFileSystem==0){
        struct lfs_info lfsinfo;
        memset(&lfsinfo,0,sizeof(DIR));
        FSerror = lfs_stat(&lfs, q, &lfsinfo);
        if(lfsinfo.type==LFS_TYPE_REG)retval= 1;
    } else {
        DIR djd;
        FILINFO fnod;
        memset(&djd,0,sizeof(DIR));
        memset(&fnod,0,sizeof(FILINFO));
        if(!InitSDCard()) return -1;
        FSerror = f_stat(q, &fnod);
        if(FSerror != FR_OK)iret=0;
        else if(!(fnod.fattrib & AM_DIR))retval=1;
    }
    FatFSFileSystem=localfilesystemsave;
    return retval;
}
int ExistsDir(char *p, char *q, int *filesystem){
    int ireturn=0;
    ireturn=0;
    int localfilesystemsave=FatFSFileSystem;
    int waste=0, t=FatFSFileSystem+1;
    t = drivecheck(p,&waste);
    p+=waste;
    getfullfilename(p,q);
    FatFSFileSystem=t-1;
    *filesystem=FatFSFileSystem;
    if(strcmp(q,"/")==0 || strcmp(q,"/.")==0 || strcmp(q,"/..")==0 ){FatFSFileSystem=localfilesystemsave;ireturn= 1; return ireturn;}
    if(FatFSFileSystem==0){
        struct lfs_info lfsinfo;
        memset(&lfsinfo,0,sizeof(DIR));
        FSerror = lfs_stat(&lfs, q, &lfsinfo);
        if(lfsinfo.type==LFS_TYPE_DIR)ireturn= 1;
    } else {
        DIR djd;
        FILINFO fnod;
        memset(&djd,0,sizeof(DIR));
        memset(&fnod,0,sizeof(FILINFO));
        if(q[strlen(q)-1]=='/')strcat(q,".");
        if(!InitSDCard()) {FatFSFileSystem=localfilesystemsave;ireturn= -1; return ireturn;}
        FSerror = f_stat(q, &fnod);
        if(FSerror != FR_OK)ireturn=0;
        else if((fnod.fattrib & AM_DIR))ireturn=1;
    }
    FatFSFileSystem=localfilesystemsave;
    return ireturn;
}

void MIPS16 fun_info(void){
    unsigned char *tp;
    sret = GetTempMemory(STRINGSIZE);                                  // this will last for the life of the command
    if(checkstring(ep, (unsigned char *)"AUTORUN")){
        if(Option.Autorun == false)strcpy((char *)sret,"Off");
        else strcpy((char *)sret,"On");
        CtoM(sret);
        targ=T_STR;
        return;
    } else if((tp=checkstring(ep, (unsigned char *)"TILE HEIGHT"))){
        /* ytileheight is defined in Memory.c on every device build (VGA
         * tile renderer height + non-VGA SPI-LCD scroll-block height). */
        iret=ytileheight;
        targ=T_INT;
        return;
    } else if((tp=checkstring(ep, (unsigned char *)"ADC DMA"))){
        targ=T_INT;
        iret=ADCDualBuffering | dmarunning;
        return;
    } else if((tp=checkstring(ep, (unsigned char *)"ADC"))){
        targ=T_INT;
        iret=((adcint==adcint1 && adcint) ? 1 : ((adcint==adcint2 && adcint) ? 2 : 0));
        return;
    } else if((tp=checkstring(ep, (unsigned char *)"BATTERY"))){
        iret = port_picocalc_battery_pct();
        targ=T_INT;
        return;
    } else if(checkstring(ep, (unsigned char *)"BCOLOUR") || checkstring(ep, (unsigned char *)"BCOLOR")){
            iret=gui_bcolour;
            targ=T_INT;
            return;
    } else if(checkstring(ep, (unsigned char *)"BOOT COUNT")){
        int boot_count;
        int FatFSFileSystemSave=FatFSFileSystem;
        FatFSFileSystem=0;
        int fnbr=FindFreeFileNbr();
        BasicFileOpen("bootcount",fnbr,FA_READ);
        {
            ssize_t rc = hal_fs_read(hal_fds[fnbr], &boot_count, sizeof(boot_count));
            if (rc < 0) { FSerror = (int)rc; ErrorCheck(fnbr); }
            else FSerror = 0;
        }
        FileClose(fnbr);
        FatFSFileSystem=FatFSFileSystemSave;
        iret=boot_count;
        targ=T_INT;
    } else if(checkstring(ep, (unsigned char *)"BOOT")){
        const char *boot_label = NULL;
        if(restart_reason==0xFFFFFFFF) boot_label = "Restart";
        else if(restart_reason==0xFFFFFFFE) boot_label = "S/W Watchdog";
        else if(restart_reason==0xFFFFFFFD) boot_label = "H/W Watchdog";
        else if(restart_reason==0xFFFFFFFC) boot_label = "Firmware update";
        else if (HAL_PORT_PWM_SLICE_COUNT > 8) {
            /* rp2350: bit-mask reset reasons. */
            if      (restart_reason & 0x30000)  boot_label = "Power On";
            else if (restart_reason & 0x40000)  boot_label = "Reset Switch";
            else if (restart_reason & 0x280000) boot_label = "Debug";
        } else {
            /* rp2040: exact-match reset reasons. */
            if      (restart_reason == 0x100)    boot_label = "Power On";
            else if (restart_reason == 0x10000)  boot_label = "Reset Switch";
            else if (restart_reason == 0x100000) boot_label = "Debug";
        }
        if (boot_label) strcpy((char *)sret, boot_label);
        else sprintf((char *)sret, "Unknown code %X",(unsigned int)restart_reason);
        CtoM(sret);
        targ=T_STR;
        return;
    } else if(*ep=='c' || *ep=='C'){
        if(checkstring(ep, (unsigned char *)"CALLTABLE")){
            iret = (int64_t)(uint32_t)CallTable;
            targ = T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"CHARGING"))){
            iret = port_picocalc_is_charging();
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"CPUSPEED")){
            IntToStr((char *)sret,Option.CPU_Speed*1000,10);
            CtoM(sret);
            targ=T_STR;
            return;
        } else if(checkstring(ep, (unsigned char *)"CURRENT")){
            if(ProgMemory[0]==1 && ProgMemory[1]==39 && ProgMemory[2]==35){
                strcpy((char *)sret,(char *)&ProgMemory[3]);
            } else strcpy((char *)sret,"NONE");
            CtoM(sret);
            targ=T_STR;
            return;
        } else error("Syntax");
    }  else if(*ep=='d' || *ep=='D'){
        if(checkstring(ep, (unsigned char *)"DEVICE")){
            fun_device();
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"DRIVE"))){
            strcpy((char *)sret,FatFSFileSystem ? "B:":"A:");
            CtoM(sret);
            targ=T_STR;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"DEBUG"))){
            iret=abs(hal_time_us_64()-mSecTimer*1000);
            targ=T_INT;
            return; 
        } else if(checkstring(ep, (unsigned char *)"DISK SIZE")){
            if(FatFSFileSystem){
                if(!InitSDCard()) error((char *)FErrorMsg[20]);					// setup the SD card
                FATFS *fs;
                DWORD fre_clust;
                /* Get volume information and free clusters of drive 1 */
                f_getfree("0:", &fre_clust, &fs);
                /* Get total sectors and free sectors */
                iret= (uint64_t)(fs->n_fatent - 2) * (uint64_t)fs->csize *(uint64_t)FF_MAX_SS;
            } else {
                iret=(Option.FlashSize-(Option.modbuff ? 1024*Option.modbuffsize : 0)-RoundUpK4(TOP_OF_SYSTEM_FLASH));
            }
            targ=T_INT;
            return;
        } else error("Syntax");
    } else if(*ep=='e' || *ep=='E'){
        if(checkstring(ep, (unsigned char *)"ERRNO")){
            iret = MMerrno;
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"EXISTS DIR"))){
            char dir[FF_MAX_LFN]={0};
            char *p = (char *)getFstring(tp);
            int filesystem;
            targ=T_INT;
            iret=ExistsDir(p,dir,&filesystem);
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"EXISTS FILE"))){
            char *p = (char *)getFstring(tp);
            iret=ExistsFile(p);
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"ERRMSG")){
            int i=OptionFileErrorAbort;
            strcpy((char *)sret, MMErrMsg);
            CtoM(sret);
            targ=T_STR;
            OptionFileErrorAbort=i;
            return;
        } else error("Syntax");
    } else if(*ep=='f' || *ep=='F'){
        if((tp=checkstring(ep, (unsigned char *)"FONT POINTER"))){
            iret=(int64_t)((uint32_t)&FontTable[getint(tp,1,FONT_TABLE_SIZE)-1]);
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"FONT ADDRESS"))){
            iret=(int64_t)((uint32_t)FontTable[getint(tp,1,FONT_TABLE_SIZE)-1]);
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"FLASH ADDRESS"))){
            /* uintptr_t round-trip — `(unsigned int)` truncates pointers
             * on 64-bit hosts (macOS / wasm64) and silently zeroes the
             * upper bits, returning 0 to BASIC. */
            iret=(int64_t)(uintptr_t)(flash_target_contents + (getint(tp,1,MAXFLASHSLOTS) - 1) * MAX_PROG_SIZE);
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"FILESIZE"))){
            char *p = (char *)getFstring(tp);
            iret=FileSize(p);
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"FREE SPACE")){
            if(FatFSFileSystem){
                if(!InitSDCard()) error((char *)FErrorMsg[20]);					// setup the SD card
                FATFS *fs;
                DWORD fre_clust;
                /* Get volume information and free clusters of drive 1 */
                f_getfree("0:", &fre_clust, &fs);
                /* Get total sectors and free sectors */
                iret = (uint64_t)fre_clust * (uint64_t)fs->csize  *(uint64_t)FF_MAX_SS;
            } else {
                iret=Option.FlashSize-(Option.modbuff ? 1024*Option.modbuffsize : 0)-RoundUpK4(TOP_OF_SYSTEM_FLASH)-lfs_fs_size(&lfs)*4096;
            }
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"FLASHTOP")){
            iret = (int64_t)(uint32_t)TOP_OF_SYSTEM_FLASH ;
            targ = T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"FONTWIDTH")){
            iret = FontTable[gui_font >> 4][0] * (gui_font & 0b1111);
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"FLASH")){
            iret=FlashLoad;
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"FCOLOUR") || checkstring(ep, (unsigned char *)"FCOLOR") ){
            iret=gui_fcolour;
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"FONT")){
            iret=(gui_font >> 4)+1;
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"FONTHEIGHT")){
            iret = FontTable[gui_font >> 4][1] * (gui_font & 0b1111);
            targ=T_INT;
            return;
        } else error("Syntax");
    } else if(*ep=='h' || *ep=='H'){
        if(checkstring(ep, (unsigned char *)"HEAP")){
            iret=FreeSpaceOnHeap();
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"HPOS")){
            iret = CurrentX;
            targ=T_INT;
            return;
        } else error("Syntax");
    } else if(checkstring(ep,(unsigned char *)"ID")){  
        strcpy((char *)sret,id_out);
        CtoM(sret);
        targ=T_STR;
        return;
    } else if (port_web_mminfo(ep, &iret, sret, &targ)) {
        return;
    }
    else if (checkstring(ep, (unsigned char *)"INTERRUPTS")) {
        if (port_mminfo_interrupts(&iret)) { targ = T_INT; return; }
    }
    /* LCDPANEL / LCD320 are SPI-LCD-port concepts — VGA has no
     * runtime panel switcher. Macros (display_details, SSD16TYPE)
     * are defined everywhere, so this is a runtime gate. */
    else if (!HAL_PORT_IS_VGA && checkstring(ep, (unsigned char *)"LCDPANEL")) {
        strcpy((char *)sret, display_details[Option.DISPLAY_TYPE].name);
        CtoM(sret);
        targ = T_STR;
        return;
    }
    else if (!HAL_PORT_IS_VGA && checkstring(ep, (unsigned char *)"LCD320")) {
        iret = (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16);
        targ = T_INT;
        return;
    }
    /* USB device info: routed through port_usb_* hooks so the HID[4]
     * array (≈336 B BSS) only exists on USB device builds. */
    else if((tp=checkstring(ep, (unsigned char *)"USB VID"))){
        int n=getint((unsigned char *)tp,1,4);
        iret=port_usb_hid_field(n, 0);
        targ=T_INT;
        return;
    }
    else if((tp=checkstring(ep, (unsigned char *)"USB PID"))){
        int n=getint((unsigned char *)tp,1,4);
        iret=port_usb_hid_field(n, 1);
        targ=T_INT;
        return;
    }
    else if((tp=checkstring(ep, (unsigned char *)"USB"))){
        int n=getint((unsigned char *)tp,0,4);
        iret = (n==0) ? port_usb_count() : port_usb_hid_field(n, 2);
        targ=T_INT;
        return;
    }
    else if (checkstring(ep, (unsigned char *)"LINE")) {
        if (!CurrentLinePtr) {
            strcpy((char *)sret, "UNKNOWN");
        } else if (CurrentLinePtr >= ProgMemory + MAX_PROG_SIZE) {
            strcpy((char *)sret, "LIBRARY");
        } else {
            sprintf((char *)sret, "%d", CountLines(CurrentLinePtr));
        }
        CtoM(sret);
        targ=T_STR;
        return;
    }	
    else if((tp=checkstring(ep,(unsigned char *)"MODBUFF ADDRESS"))){
        iret=(int64_t)((uint32_t)(char *)(XIP_BASE + RoundUpK4(TOP_OF_SYSTEM_FLASH)));
        targ=T_INT;
        return;
    }
    else if((tp=checkstring(ep, (unsigned char *)"MODIFIED"))){
//		int i,j;
	    DIR djd;
	    FILINFO fnod;
        sret = GetTempMemory(STRINGSIZE);                                    // this will last for the life of the command
        targ=T_STR; 
		memset(&djd,0,sizeof(DIR));
		memset(&fnod,0,sizeof(FILINFO));
		char *p = (char *)getFstring(tp);
        char q[FF_MAX_LFN]={0};
        int waste=0, t=FatFSFileSystem+1;
        t = drivecheck(p,&waste);
        p+=waste;
        getfullfilename(p,q);
        FatFSFileSystem=t-1;
        if(FatFSFileSystem==0){
            int dt;
            FSerror=lfs_getattr(&lfs, q, 'A', &dt,    4);
            if(FSerror!=4) return;
            else {
                WORD *p=(WORD *)&dt;
                fnod.fdate=(WORD)p[1];
                fnod.ftime=(WORD)p[0];
            }
        } else {
			if(!InitSDCard()) {iret= -1; return;}
            FSerror = f_stat(p, &fnod);
            if(FSerror != FR_OK) return;
        }
	    IntToStr((char *)sret , ((fnod.fdate>>9)&0x7F)+1980, 10);
	    sret[4] = '-'; IntToStrPad((char *)sret + 5, (fnod.fdate>>5)&0xF, '0', 2, 10);
	    sret[7] = '-'; IntToStrPad((char *)sret + 8, fnod.fdate&0x1F, '0', 2, 10);
	    sret[10] = ' ';
	    IntToStrPad((char *)sret+11, (fnod.ftime>>11)&0x1F, '0', 2, 10);
	    sret[13] = ':'; IntToStrPad((char *)sret + 14, (fnod.ftime>>5)&0x3F, '0', 2, 10);
	    sret[16] = ':'; IntToStrPad((char *)sret + 17, (fnod.ftime&0x1F)*2, '0', 2, 10);
        FatFSFileSystem=FatFSFileSystemSave;
		CtoM(sret);
	    targ=T_STR;
		return;
	} else if((tp=checkstring(ep, (unsigned char *)"ONEWIRE"))){
        fun_mmOW();
        return;
	} else if((tp=checkstring(ep, (unsigned char *)"OPTION"))){
        if(checkstring(tp, (unsigned char *)"AUTORUN")){
			if(Option.Autorun == false)strcpy((char *)sret,"Off");
            else if(Option.Autorun==MAXFLASHSLOTS+1)strcpy((char *)sret,"On");
			else {
                char b[10];
                IntToStr(b,Option.Autorun,10);
                strcpy((char *)sret,b);
            }
            CtoM(sret);
            targ=T_STR;
            return;
		} else if(checkstring(tp, (unsigned char *)"BASE")){
			if(g_OptionBase==1)iret=1;
			else iret=0;
			targ=T_INT;
			return;
		} else if(checkstring(tp, (unsigned char *)"AUDIO")){
            if(Option.AUDIO_L)strcpy((char *)sret,"PWM");
            else if(Option.AUDIO_MISO_PIN)strcpy((char *)sret,"VS1053");
            else if(Option.AUDIO_CLK_PIN)strcpy((char *)sret,"SPI");
			else if(Option.audio_i2s_bclk)strcpy((char *)sret,"I2S");
            else strcpy((char *)sret,"NONE");
            CtoM(sret);
            targ=T_STR;
			return;
		} else if(checkstring(tp, (unsigned char *)"BREAK")){
			iret=BreakKey;
			targ=T_INT;
			return;
		} else if(checkstring(tp, (unsigned char *)"AUTOREFRESH")){
			if(Option.Refresh)strcpy((char *)sret,"ON");
			else strcpy((char *)sret,"OFF");
            CtoM(sret);
            targ=T_STR;
            return;
		} else if(checkstring(tp, (unsigned char *)"ANGLE")){
			if(optionangle==1.0)strcpy((char *)sret,"RADIANS");
			else strcpy((char *)sret,"DEGREES");
            CtoM(sret);
            targ=T_STR;
            return;
 		} else if(checkstring(tp, (unsigned char *)"DEFAULT")){
			if(DefaultType == T_INT)strcpy((char *)sret,"Integer");
			else if(DefaultType == T_NBR)strcpy((char *)sret,"Float");
			else if(DefaultType == T_STR)strcpy((char *)sret,"String");
			else strcpy((char *)sret,"None");
            CtoM(sret);
            targ=T_STR;
            return;
 		} else if(checkstring(tp, (unsigned char *)"KEYBOARD")){
            /* USB builds populate Option.USBKeyboard; PS/2 builds populate
             * Option.KeyboardConfig. The other field stays zero. */
            int kb = Option.USBKeyboard ? Option.USBKeyboard : Option.KeyboardConfig;
            strcpy((char *)sret, (char *)KBrdList[kb]);
            CtoM(sret);
            targ=T_STR;
            return;
 		} else if(checkstring(tp, (unsigned char *)"EXPLICIT")){
			if(OptionExplicit == false)strcpy((char *)sret,"Off");
			else strcpy((char *)sret,"On");
            CtoM(sret);
            targ=T_STR;
            return;
		} else if(checkstring(tp, (unsigned char *)"FLASH SIZE")){
            uint8_t rxbuf[4] = {0};
            hal_flash_read_jedec_id(rxbuf);
            iret= 1 << rxbuf[3];
			targ=T_INT;
			return;
        } else if(checkstring(tp, (unsigned char *)"HEIGHT")){
            iret = Option.Height;
            targ = T_INT;
            return;
        } else if(checkstring(tp, (unsigned char *)"CONSOLE")){
			if(Option.DISPLAY_CONSOLE)strcpy((char *)sret,"Both");
			else strcpy((char *)sret,"Serial");
            CtoM(sret);
            targ=T_STR;
            return;
        } else if(checkstring(tp, (unsigned char *)"WIDTH")){
            iret = Option.Width;
            targ = T_INT;
            return;
		} else if(checkstring(tp, (unsigned char *)"SSID") && port_web_get_ssid(sret, &targ)) {
            return;
		} else error("Syntax");
    } else if(*ep=='p' || *ep=='P'){
        if((tp=checkstring(ep, (unsigned char *)"PINNO"))){
            int pin;
            MMFLOAT f;
            long long int i64;
            unsigned char *ss;
            int t=0;
            char code, *ptr;
            char *string=GetTempMemory(STRINGSIZE);
            skipspace(tp);
            iret = port_pinno_alias_for_name((const char *)tp);
            if (iret) { targ = T_INT; return; }
            if(codecheck(tp))evaluate(tp, &f, &i64, &ss, &t, false);
            if(t & T_STR ){
                ptr=(char *)getCstring(tp);
                strcpy(string,ptr);
            } else {
                strcpy(string,(char *)tp);
            }
            iret = port_pinno_alias_for_name((const char *)string);
            if (iret) { targ = T_INT; return; }
            if(!(code=codecheck( (unsigned char *)string)))string+=2;
            else error("Syntax");
            pin = getinteger((unsigned char *)string);
            if(!code)pin=codemap(pin);
            if (port_pin_is_reserved_alias(pin)) {
                iret = pin;
                targ = T_INT;
                return;
            }
            if(IsInvalidPin(pin))error("Invalid pin");
            iret=pin;
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"PERSISTENT"))){
            iret=_persistent;
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"PIO RX DMA"))){
            iret=dma_channel_is_busy(dma_rx_chan);
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"PIO TX DMA"))){
            iret=dma_channel_is_busy(dma_tx_chan);
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"PWM COUNT"))){
            /* rp2040: 8 slices (0..7). rp2350a==true (RP2350A or RP2040
             * runtime probe): 0..7. rp2350a==false (RP2350B): 0..11.
             * HAL_PORT_PWM_SLICE_COUNT is 8 on rp2040, 12 on rp2350. */
            int max_slice = HAL_PORT_PWM_SLICE_COUNT - 1;
            if (HAL_PORT_PWM_SLICE_COUNT > 8 && rp2350a) max_slice = 7;
            int channel=getint(tp,0,max_slice);
            iret=pwm_hw->slice[channel].top;
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"PWM DUTY"))){
            getargs(&tp,3,(unsigned char *)",");
            if(argc!=3)error("Syntax");
            int max_slice = HAL_PORT_PWM_SLICE_COUNT - 1;
            if (HAL_PORT_PWM_SLICE_COUNT > 8 && rp2350a) max_slice = 7;
            int channel=getint(argv[0],0,max_slice);
            int AorB=getint(argv[2],0,1);
            if(AorB)iret=((pwm_hw->slice[channel].cc) >> 16);
            else iret=(pwm_hw->slice[channel].cc & 0xFFFF);
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"PIN"))){
            int pin;
            char code;
            if(!(code=codecheck(tp)))tp+=2;
            pin = getinteger(tp);
            if(!code)pin=codemap(pin);
            {
                const char *reserved = port_pin_reserved_label(pin);
                if (reserved) {
                    strcpy((char *)sret, reserved);
                    CtoM(sret);
                    targ = T_STR;
                    return;
                }
            }
            if(IsInvalidPin(pin))strcpy((char *)sret,"Invalid");
            else strcpy((char *)sret,PinFunction[ExtCurrentConfig[pin] & 0xFF]);
            if(ExtCurrentConfig[pin] & EXT_BOOT_RESERVED){
                strcpy((char *)sret, "Boot Reserved : ");
                strcat((char *)sret,pinsearch(pin));
            }
            if(ExtCurrentConfig[pin] & EXT_COM_RESERVED)strcat((char *)sret, ": Reserved for function");
            if(ExtCurrentConfig[pin] & EXT_DS18B20_RESERVED)strcat((char *)sret, ": In use for DS18B20");
            CtoM(sret);
            targ=T_STR;
            return;
        } else if(checkstring(ep, (unsigned char *)"PROGRAM")){
            iret = (int64_t)(uint32_t)ProgMemory;
            targ = T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"PS2")){
            /* PS2code is 0 on USB-keyboard builds (USBKeyboard.c stubs it). */
            iret = (int64_t)(uint32_t)PS2code;
            targ = T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"PATH")){
//            strcpy((char *)sret,GetCWD());
//            if(sret[strlen((char *)sret)-1]!='/')strcat((char *)sret,"/");
            if(ProgMemory[0]==1 && ProgMemory[1]==39 && ProgMemory[2]==35){
                strcpy((char *)sret,(char *)&ProgMemory[3]);
                for(int i=strlen((char *)sret)-1;i>0;i--){
                    if(sret[i]!='/')sret[i]=0;
                    else break;
                }
            } else strcpy((char *)sret,"NONE");
            CtoM(sret);
            targ=T_STR;
            return;
        } else if(checkstring(ep, (unsigned char *)"PLATFORM")){
			strcpy((char *)sret,(char *)Option.platform);
            CtoM(sret);
            targ=T_STR;
            return;
        } else error("Syntax");
    } else if(*ep=='s' || *ep=='S'){
        if(checkstring(ep, (unsigned char *)"SDCARD")){
            int i=OptionFileErrorAbort;
            OptionFileErrorAbort=0;
            FatFSFileSystemSave = FatFSFileSystem;
            FatFSFileSystem=1;
            if(!(Option.SD_CS || Option.CombinedCS))strcpy((char *)sret,"Not Configured");
            else if(!InitSDCard())strcpy((char *)sret,"Not present");
            else  strcpy((char *)sret,"Ready");
            CtoM(sret);
            targ=T_STR;
            OptionFileErrorAbort=i;
            FatFSFileSystem = FatFSFileSystemSave;
            return;
        } else if(checkstring(ep, (unsigned char *)"SYSTEM I2C")){
            if(!Option.SYSTEM_I2C_SDA)strcpy((char *)sret,"Not set");
            else  if(I2C0locked) strcpy((char *)sret,"I2C");
            else strcpy((char *)sret,"I2C2");
            CtoM(sret);
            targ=T_STR;
            return;
        } else if(checkstring(ep, (unsigned char *)"SPI SPEED")){
            SPISpeedSet(Option.DISPLAY_TYPE);
            if(PinDef[Option.SYSTEM_CLK].mode & SPI0SCK){
                iret=spi_get_baudrate(spi0);
            } else if(PinDef[Option.SYSTEM_CLK].mode & SPI1SCK){
                iret=spi_get_baudrate(spi1);
            } else error("System SPI not configured");
           targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"STACK")){
            iret=(int64_t)((uint32_t)__get_MSP());
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"SYSTICK"))){
            iret = (int64_t)(uint32_t)systick_hw->cvr;
            targ = T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"SYSTEM HEAP")){
            iret = (int64_t)(uint32_t)getFreeHeap();
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"SOUND")){
            strcpy((char *)sret,PlayingStr[CurrentlyPlaying]);
            CtoM(sret);
            targ=T_STR;
            return;
        } else if (checkstring(ep, (unsigned char *)"SCROLL")) {
            if (port_mminfo_scroll_start(&iret)) { targ = T_INT; return; }
            error("Syntax");
        } else if (checkstring(ep, (unsigned char *)"SCREENBUFF")) {
            if (port_mminfo_screenbuff(&iret)) { targ = T_INT; return; }
            error("Syntax");
        } else error("Syntax");
    }
    else if (checkstring(ep, (unsigned char *)"TOUCH")) {
        if (port_mminfo_touch_status(sret)) {
            CtoM(sret);
            targ = T_STR;
            return;
        }
        error("Syntax");
    }
	else if(checkstring(ep, (unsigned char *)"TRACK")){
		if(CurrentlyPlaying == P_MP3 || CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_WAV|| CurrentlyPlaying == P_MOD || CurrentlyPlaying == P_MIDI) strcpy((char *)sret,WAVfilename);
		else strcpy((char *)sret,"OFF");
        CtoM(sret);
        targ=T_STR;
        return;
    }
    else if(*ep=='v' || *ep=='V'){
        if(checkstring(ep, (unsigned char *)"VARCNT")){
            iret=(int64_t)((uint32_t)g_varcnt);
            targ=T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"VERSION")){
            fun_version();
            return;
        } else if(checkstring(ep, (unsigned char *)"VPOS")){
            iret = CurrentY;
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"VALID CPUSPEED"))){
                iret=1;
                uint32_t speed=getint(tp,MIN_CPU,MAX_CPU);
                uint vco, postdiv1, postdiv2;
                if (!check_sys_clock_khz(speed, &vco, &postdiv1, &postdiv2))iret=0;
                targ=T_INT;
                return;
        } else error("Syntax");
	} else if(checkstring(ep, (unsigned char *)"WRITEBUFF")){
        iret=(int64_t)((uint32_t)WriteBuf);
        targ=T_INT;
        return;
    } else if((tp=checkstring(ep, (unsigned char *)"UPTIME"))){
        fret = (MMFLOAT)hal_time_us_64()/1000000.0;
        targ = T_NBR;
        return;
    } else if((tp=checkstring(ep, (unsigned char *)"MAX GP"))){
        /* rp2040: 30 GPIOs (0..29). rp2350a==true: 0..29. rp2350a==false
         * (RP2350B): 0..47. HAL_PORT_GPIO_COUNT is 30 on rp2040, 48 on
         * rp2350. */
        iret = (HAL_PORT_GPIO_COUNT > 30 && !rp2350a) ? 47 : 29;
        targ = T_INT;
        return;
    } else error("Syntax");
}


void cmd_watchdog(void) {
    int i;
    unsigned char *p;

    if((p=checkstring(cmdline, (unsigned char *)"HW"))){
        if(checkstring(p, (unsigned char *)"OFF") != NULL) {
            hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
            _excep_code=0;
        } else {
            i = getint(p,1,8331);
            watchdog_enable(i,1);
            _excep_code=POSSIBLE_WATCHDOG;
        }
    
    } else if(checkstring(cmdline, (unsigned char *)"OFF") != NULL) {
        WDTimer = 0;
    } else {
        i = getinteger(cmdline);
        if(i < 1) error("Invalid argument");
        WDTimer = i;
    }
}

void fun_restart(void) {
    iret = WatchdogSet;
    targ = T_INT;
}


void cmd_cpu(void) {
    unsigned char *p;
    if((p = checkstring(cmdline, (unsigned char *)"RESTART"))) {
        _excep_code = RESET_COMMAND;
//        while(ConsoleTxBufTail != ConsoleTxBufHead);
        uSec(10000);
        SoftReset();                                                // this will restart the processor ? only works when not in debug
    } else if((p = checkstring(cmdline, (unsigned char *)"SLEEP"))) {
//        	int pullup=0;
            MMFLOAT totalseconds;
            getargs(&p, 3, (unsigned char *)",");
            totalseconds=getnumber(p);
            if(totalseconds<=0.0)error("Invalid period");
            hal_time_sleep_us(totalseconds*1000000);

    } else error("Syntax");
}
void cmd_csubinterrupt(void){
    getargs(&cmdline,1,(unsigned char *)",");
    if(argc != 0){
        if(checkstring(argv[0],(unsigned char *)"0")){
            CSubInterrupt = NULL;
            CSubComplete=0;  
        } else {
            CSubInterrupt = (char *)GetIntAddress(argv[0]); 
            CSubComplete=0;  
            InterruptUsed = true;
        }
    } else CSubComplete=1;  
}
void cmd_cfunction(void) {
    unsigned char *p;
    unsigned short EndToken;
    CommandToken tkn;
    EndToken = GetCommandValue((unsigned char *)"End DefineFont");           // this terminates a DefineFont
    if(cmdtoken == cmdCSUB) EndToken = GetCommandValue((unsigned char *)"End CSub");                 // this terminates a CSUB
    p = cmdline;
    while(*p != 0xff) {
        if(*p == 0) p++;                                            // if it is at the end of an element skip the zero marker
        if(*p == 0) error("Missing END declaration");               // end of the program
        if(*p == T_NEWLINE) p++;                                    // skip over the newline token
        if(*p == T_LINENBR) p += 3;                                 // skip over the line number
        skipspace(p);
        if(*p == T_LABEL) {
            p += p[1] + 2;                                          // skip over the label
            skipspace(p);                                           // and any following spaces
        }
        tkn=commandtbl_decode(p);
        if(tkn == EndToken) {                                        // found an END token
            nextstmt = (unsigned char *)p;
            skipelement(nextstmt);
            return;
        }
        p++;
    }
}

// utility function used by cmd_poke() to validate an address
unsigned int GetPokeAddr(unsigned char *p) {
    unsigned int i;
    i = getinteger(p);
//    if(!POKERANGE(i)) error("Address");
    return i;
}


void cmd_poke(void) {
    unsigned char *p, *q;
    void *pp;
    if((p = checkstring(cmdline, (unsigned char *)"DISPLAY"))){
        if(!Option.DISPLAY_TYPE)error("Display not configured");
        if((q=checkstring(p,(unsigned char *)"HRES"))){ 
            HRes=getint(q,0,1920);
            return;
        } else if((q=checkstring(p,(unsigned char *)"VRES"))){
            VRes=getint(q,0,1200);
            return;
        } else if (port_poke_display_panel(p)) {
            return;
        } error("Syntax");
    } else {
        getargs(&cmdline, 5, (unsigned char *)",");
        if((p = checkstring(argv[0], (unsigned char *)"BYTE"))) {
            if(argc != 3) error("Argument count");
            uint32_t a=GetPokeAddr(p);
            uint8_t *padd=(uint8_t *)(a);
            *padd = getinteger(argv[2]);
            return;
        }
        if((p = checkstring(argv[0], (unsigned char *)"SHORT"))) {
            if(argc != 3) error("Argument count");
            uint32_t a=GetPokeAddr(p);
            if(a % 2)error("Address not divisible by 2");
            uint16_t *padd=(uint16_t *)(a);
            *padd = getinteger(argv[2]);
            return;
        }

        if((p = checkstring(argv[0], (unsigned char *)"WORD"))) {
            if(argc != 3) error("Argument count");
            uint32_t a=GetPokeAddr(p);
            if(a % 4)error("Address not divisible by 4");
            uint32_t *padd=(uint32_t *)(a);
            *padd = getinteger(argv[2]);
            return;
        }

        if((p = checkstring(argv[0], (unsigned char *)"INTEGER"))) {
            if(argc != 3) error("Argument count");
            uint32_t a=GetPokeAddr(p);
            if(a % 8)error("Address not divisible by 8");
            uint64_t *padd=(uint64_t *)(a);
            *padd = getinteger(argv[2]);
            return;
        }
        if((p = checkstring(argv[0], (unsigned char *)"FLOAT"))) {
            if(argc != 3) error("Argument count");
            uint32_t a=GetPokeAddr(p);
            if(a % 8)error("Address not divisible by 8");
            MMFLOAT *padd=(MMFLOAT *)(a);
            *padd = getnumber(argv[2]);
            return;
        }

        if(argc != 5) error("Argument count");

        if(checkstring(argv[0], (unsigned char *)"VARTBL")) {
            *((char *)g_vartbl + (unsigned int)getinteger(argv[2])) = getinteger(argv[4]);
            return;
        }
        if((p = checkstring(argv[0], (unsigned char *)"VAR"))) {
            pp = findvar(p, V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
            if(g_vartbl[g_VarIndex].type & T_CONST) error("Cannot change a constant");
            *((char *)pp + (unsigned int)getinteger(argv[2])) = getinteger(argv[4]);
            return;
        }
        // the default is the old syntax of:   POKE hiaddr, loaddr, byte
        *(char *)(((int)getinteger(argv[0]) << 16) + (int)getinteger(argv[2])) = getinteger(argv[4]);
    }
}


// function to find a CFunction
// only used by fun_peek() below
unsigned int GetCFunAddr(int *ip, int i,unsigned char *offset) {
    while(*ip != 0xffffffff) {
        //if(*ip++ == (unsigned int)(subfun[i]-ProgMemory)) {                      // if we have a match
        if(*ip++ == (unsigned int)(subfun[i]-offset)) {                      // if we have a match
            ip++;                                                   // step over the size word
            i = *ip++;                                              // get the offset
            return (unsigned int)(ip + i);                          // return the entry point
        }
        ip += (*ip + 4) / sizeof(unsigned int);
    }
    return 0;
}





// utility function used by fun_peek() to validate an address
unsigned int __not_in_flash_func(GetPeekAddr)(unsigned char *p) {
    unsigned int i;
    i = getinteger(p);
//    if(!PEEKRANGE(i)) error("Address");
    return i;
}

#define SPIsend(a) {uint8_t b=a;xmit_byte_multi(&b,1);}
#define SPIsend2(a) {SPIsend(0);SPIsend(a);}

// Will return a byte within the PIC32 virtual memory space.
void fun_peek(void) {
    unsigned char *p;
    void *pp;
    getargs(&ep, 3, (unsigned char *)",");
    if((p = checkstring(argv[0], (unsigned char *)"INT8"))){
        if(argc != 1) error("Syntax");
        iret = *(unsigned char *)GetPeekAddr(p);
        targ = T_INT;
        return;
    }

    if((p = checkstring(argv[0], (unsigned char *)"VARADDR"))){
        if(argc != 1) error("Syntax");
        pp = findvar(p, V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
        iret = (unsigned int)pp;
        targ = T_INT;
        return;
        }

    if((p = checkstring(argv[0], (unsigned char *)"BP"))){
        if(argc != 1) error("Syntax");
        findvar(p, V_FIND  | V_NOFIND_ERR);
        if(!(g_vartbl[g_VarIndex].type & T_INT))error("Not integer variable");
        iret = *(unsigned char *)(uint32_t)g_vartbl[g_VarIndex].val.i;
        g_vartbl[g_VarIndex].val.i++;
        targ = T_INT;
        return;
    }
    if((p = checkstring(argv[0], (unsigned char *)"WP"))){
        if(argc != 1) error("Syntax");
        findvar(p, V_FIND  | V_NOFIND_ERR);
        if(!(g_vartbl[g_VarIndex].type & T_INT))error("Not integer variable");
        if(g_vartbl[g_VarIndex].val.i & 3)error("Not on word boundary");
        iret = *(unsigned int *)(uint32_t)g_vartbl[g_VarIndex].val.i;
        g_vartbl[g_VarIndex].val.i+=4;
        targ = T_INT;
        return;
    }
    if((p = checkstring(argv[0], (unsigned char *)"SP"))){
        if(argc != 1) error("Syntax");
        findvar(p, V_FIND  | V_NOFIND_ERR);
        if(!(g_vartbl[g_VarIndex].type & T_INT))error("Not integer variable");
        if(g_vartbl[g_VarIndex].val.i & 1)error("Not on short boundary");
        iret = *(unsigned short *)(uint32_t)g_vartbl[g_VarIndex].val.i;
        g_vartbl[g_VarIndex].val.i+=2;
        targ = T_INT;
        return;
    }
    if((p = checkstring(argv[0], (unsigned char *)"VAR"))){
        pp = findvar(p, V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
        iret = *((char *)pp + (int)getinteger(argv[2]));
        targ = T_INT;
        return;
    }
    
    if((p = checkstring(argv[0], (unsigned char *)"VARHEADER"))){
        if(argc != 1) error("Syntax");
        pp = findvar(p, V_FIND | V_EMPTY_OK | V_NOFIND_ERR);
        iret = (unsigned int)&g_vartbl[g_VarIndex].name[0];
        targ = T_INT;
        return;
        }
        
    if((p = checkstring(argv[0], (unsigned char *)"CFUNADDR"))){
    	int i,j;
        if(argc != 1) error("Syntax");
        i = FindSubFun(p, true);                                    // search for a function first
        if(i == -1) i = FindSubFun(p, false);                       // and if not found try for a subroutine
        if(i == -1){
            skipspace(p);
            getargs(&p,1,(unsigned char *)",");
            if(argc!=1)error("Syntax");
            unsigned char *q=getCstring(argv[0]);
            i = FindSubFun(q, true);                                    // search for a function first
            if(i == -1) i = FindSubFun(q, false);                       // and if not found try for a subroutine
        } if(i == -1 || !(commandtbl_decode(subfun[i]) == cmdCSUB)) error("Invalid argument");
        // search through program flash and the library looking for a match to the function being called
        j = GetCFunAddr((int *)CFunctionFlash, i,ProgMemory);
        if(!j) j = GetCFunAddr((int *)CFunctionLibrary, i,LibMemory);         //Check the library
        if(!j) error("Internal fault 6(sorry)");
        iret = (unsigned int)j;                                     // return the entry point
        targ = T_INT;
        return;
    }

    if((p = checkstring(argv[0], (unsigned char *)"WORD"))){
        if(argc != 1) error("Syntax");
        iret = *(unsigned int *)(GetPeekAddr(p) & 0b11111111111111111111111111111100);
        targ = T_INT;
        return;
        }
    if((p = checkstring(argv[0], (unsigned char *)"SHORT"))){
        if(argc != 1) error("Syntax");
        iret = (unsigned long long int) (*(unsigned short *)(GetPeekAddr(p) & 0b11111111111111111111111111111110));
        targ = T_INT;
        return;
        }
    if((p = checkstring(argv[0], (unsigned char *)"INTEGER"))){
        if(argc != 1) error("Syntax");
        iret = *(uint64_t *)(GetPeekAddr(p) & 0xFFFFFFF8);
        targ = T_INT;
        return;
        }

    if((p = checkstring(argv[0], (unsigned char *)"FLOAT"))){
        if(argc != 1) error("Syntax");
        fret = *(MMFLOAT *)(GetPeekAddr(p) & 0xFFFFFFF8);
        targ = T_NBR;
        return;
        }

    if(argc != 3) error("Syntax");

    if((checkstring(argv[0], (unsigned char *)"PROGMEM"))){
        iret = *((char *)ProgMemory + (int)getinteger(argv[2]));
        targ = T_INT;
        return;
    }

    if((checkstring(argv[0], (unsigned char *)"VARTBL"))){
        iret = *((char *)g_vartbl + (int)getinteger(argv[2]));
        targ = T_INT;
        return;
    }


    // default action is the old syntax of  b = PEEK(hiaddr, loaddr)
    iret = *(char *)(((int)getinteger(argv[0]) << 16) + (int)getinteger(argv[2]));
    targ = T_INT;
}



/***********************************************************************************************
interrupt check

The priority of interrupts (highest to low) is:
Touch (MM+ only)
CFunction Interrupt
ON KEY
I2C Slave Rx
I2C Slave Tx
COM1
COM2
COM3 (MM+ only)
COM4 (MM+ only)
GUI Int Down (MM+ only)
GUI Int Up (MM+ only)
WAV Finished (MM+ only)
IR Receive
I/O Pin Interrupts in order of definition
Tick Interrupts (1 to 4 in that order)

************************************************************************************************/

// check if an interrupt has occured and if so, set the next command to the interrupt routine
// will return true if interrupt detected or false if not
int checkdetailinterrupts(void) {
    int i, v;
    char *intaddr;
    static char rti[2];
    for(int i=1;i<=MAXPID;i++){
        if(PIDchannels[i].interrupt!=NULL && hal_time_us_64()>PIDchannels[i].timenext && PIDchannels[i].active){
            PIDchannels[i].timenext=hal_time_us_64()+(PIDchannels[i].PIDparams->T * 1000000);
            intaddr=(char *)PIDchannels[i].interrupt;
            goto GotAnInterrupt;
        }
    }

    // check for an  ON KEY loc  interrupt
    if(KeyInterrupt != NULL && Keycomplete) {
		Keycomplete=false;
		intaddr = KeyInterrupt;									    // set the next stmt to the interrupt location
		goto GotAnInterrupt;
	}

    if(OnKeyGOSUB && kbhitConsole()) {
        intaddr = (char *)OnKeyGOSUB;                                       // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    if(OnPS2GOSUB && PS2int) {
        /* PS2int is always false on USB-keyboard builds. */
        intaddr = (char *)OnPS2GOSUB;
        PS2int=false;
        goto GotAnInterrupt;
    }
    if(piointerrupt){  // have any PIO interrupts been set
        for(int pio=0 ;pio<PIOMAX;pio++){
            PIO pioinuse = port_pio_for_index(pio);
            for(int sm=0;sm<4;sm++){
                int TXlevel=((pioinuse->flevel)>>(sm*4)) & 0xf;
                int RXlevel=((pioinuse->flevel)>>(sm*4+4)) & 0xf;
                if(RXlevel && pioRXinterrupts[sm][pio]){ //is there a character in the buffer and has an interrupt been set?
                    intaddr=pioRXinterrupts[sm][pio];
                    goto GotAnInterrupt;
                }
                if(TXlevel && pioTXinterrupts[sm][pio]){
                    int full=(pioinuse->sm->shiftctrl & (1<<30))  ? 8 : 4;
                    if(TXlevel != full && pioTXlast[sm][pio]==full){ // was the buffer full last time and not now and is an interrupt set?
                        intaddr=pioTXinterrupts[sm][pio];
                        pioTXlast[sm][pio]=TXlevel;
                        goto GotAnInterrupt;
                    }
                }
                pioTXlast[sm][pio]=TXlevel;
            }
        }
    }
    if(DMAinterruptRX){
        if(!dma_channel_is_busy(dma_rx_chan)){
            PIO pio = (dma_rx_pio ? pio1: pio0);
            intaddr = (char *)DMAinterruptRX;
            DMAinterruptRX=NULL;
            pio_sm_set_enabled(pio, dma_rx_sm, false);
            goto GotAnInterrupt;
        }
    }
    if(DMAinterruptTX){
        if(!dma_channel_is_busy(dma_tx_chan)){
            PIO pio = (dma_tx_pio ? pio1: pio0);
            if((pio->flevel>>(dma_tx_sm*8) & 0xf)==0){
                intaddr = (char *)DMAinterruptTX;
                DMAinterruptTX=NULL;
                pio_sm_set_enabled(pio, dma_tx_sm, false);
                goto GotAnInterrupt;
            }
        }
    }

#ifdef GUICONTROLS
    if(Ctrl!=NULL){
        if(gui_int_down && GuiIntDownVector) {                          // interrupt on pen down
            intaddr = GuiIntDownVector;                                 // get a pointer to the interrupt routine
            gui_int_down = false;
            goto GotAnInterrupt;
        }

        if(gui_int_up && GuiIntUpVector) {
            intaddr = GuiIntUpVector;                                   // get a pointer to the interrupt routine
            gui_int_up = false;
            goto GotAnInterrupt;
        }
    }
#endif

    if (COLLISIONInterrupt != NULL && CollisionFound) {
        CollisionFound = false;
        intaddr = (char *)COLLISIONInterrupt;									    // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    /* WEB-only interrupt sources. Globals are zero on non-WEB builds
     * (see top-of-file unconditional defs + host_runtime.c stubs), so
     * the conditions never fire. */
    if(TCPreceived && TCPreceiveInterrupt){
        intaddr = (char *)TCPreceiveInterrupt;
        TCPreceived = 0;
        goto GotAnInterrupt;
    }
    if(MQTTComplete && MQTTInterrupt != NULL) {
        MQTTComplete = false;
        intaddr = (char *)MQTTInterrupt;
        goto GotAnInterrupt;
    }
    if(UDPreceive && UDPinterrupt != NULL) {
        UDPreceive = false;
        intaddr = (char *)UDPinterrupt;
        goto GotAnInterrupt;
    }
    for(int i=0;i<6;i++){
        if(nunInterruptc[i] !=NULL && nunfoundc[i]){
            nunfoundc[i]=false;
            intaddr=nunInterruptc[i];
            goto GotAnInterrupt;
        }
    }
    if(ADCInterrupt && dmarunning){
        if(!dma_channel_is_busy(ADC_dma_chan)){
            __compiler_memory_barrier();
            adc_run(false);
            adc_fifo_drain();
            int k=0;
            for(int i=0;i<ADCmax;i++){
                for(int j=0;j<ADCopen;j++){
                    if(j==0)*a1float++ = (MMFLOAT)ADCbuffer[k++]*ADCscale[0]+ADCbottom[0];
                    if(j==1)*a2float++ = (MMFLOAT)ADCbuffer[k++]*ADCscale[1]+ADCbottom[1];
                    if(j==2)*a3float++ = (MMFLOAT)ADCbuffer[k++]*ADCscale[2]+ADCbottom[2];
                    if(j==3)*a4float++ = (MMFLOAT)ADCbuffer[k++]*ADCscale[3]+ADCbottom[3];
                }
            }
        intaddr = ADCInterrupt;                                   // get a pointer to the interrupt routine
        dmarunning=false;
        hal_pin_adc_init();
        last_adc=99;
        FreeMemory((void *)ADCbuffer);
        goto GotAnInterrupt;
        }
    }
    if(ADCInterrupt && ADCDualBuffering){
        ADCDualBuffering=false;
        intaddr = ADCInterrupt;
        goto GotAnInterrupt;
    }



    if ((I2C_Status & I2C_Status_Slave_Receive_Rdy)) {
        I2C_Status &= ~I2C_Status_Slave_Receive_Rdy;                // clear completed flag
        intaddr = I2C_Slave_Receive_IntLine;                        // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    if ((I2C_Status & I2C_Status_Slave_Send_Rdy)) {
        I2C_Status &= ~I2C_Status_Slave_Send_Rdy;                   // clear completed flag
        intaddr = I2C_Slave_Send_IntLine;                           // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    if ((I2C2_Status & I2C_Status_Slave_Receive_Rdy)) {
        I2C2_Status &= ~I2C_Status_Slave_Receive_Rdy;                // clear completed flag
        intaddr = I2C2_Slave_Receive_IntLine;                        // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    if ((I2C2_Status & I2C_Status_Slave_Send_Rdy)) {
        I2C2_Status &= ~I2C_Status_Slave_Send_Rdy;                   // clear completed flag
        intaddr = I2C2_Slave_Send_IntLine;                           // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    if(WAVInterrupt != NULL && WAVcomplete) {
        WAVcomplete=false;
		intaddr = WAVInterrupt;									    // set the next stmt to the interrupt location
		goto GotAnInterrupt;
	}

    // interrupt routines for the serial ports
    if(com1_interrupt != NULL && SerialRxStatus(1) >= com1_ilevel) {// do we need to interrupt?
        intaddr = com1_interrupt;                                   // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    if(com2_interrupt != NULL && SerialRxStatus(2) >= com2_ilevel) {// do we need to interrupt?
        intaddr = com2_interrupt;                                   // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    if(IrGotMsg && IrInterrupt != NULL) {
        IrGotMsg = false;
        intaddr = (char *)IrInterrupt;                                      // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }

    if(KeypadInterrupt != NULL && KeypadCheck()) {
        intaddr = (char *)KeypadInterrupt;                                  // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }

    if(CSubInterrupt != NULL && CSubComplete) {
        intaddr = CSubInterrupt;                                  // set the next stmt to the interrupt location
        CSubComplete=0;
        goto GotAnInterrupt;
    }

    for(i = 0; i < NBRINTERRUPTS; i++) {                            // scan through the interrupt table
        if(inttbl[i].pin != 0) {                                    // if this entry has an interrupt pin set
            v = ExtInp(inttbl[i].pin);                              // get the current value of the pin
            // check if interrupt occured
            if((inttbl[i].lohi == T_HILO && v < inttbl[i].last) || (inttbl[i].lohi == T_LOHI && v > inttbl[i].last) || (inttbl[i].lohi == T_BOTH && v != inttbl[i].last)) {
                intaddr = inttbl[i].intp;                           // set the next stmt to the interrupt location
                inttbl[i].last = v;                                 // save the new pin value
                goto GotAnInterrupt;
            } else
                inttbl[i].last = v;                                 // no interrupt, just update the pin value
        }
    }

    // check if one of the tick interrupts is enabled and if it has occured
    for(i = 0; i < NBRSETTICKS; i++) {
        if(TickInt[i] != NULL && TickTimer[i] > TickPeriod[i]) {
            // reset for the next tick but skip any ticks completely missed
            while(TickTimer[i] > TickPeriod[i]) TickTimer[i] -= TickPeriod[i];
            intaddr = (char *)TickInt[i];
            if(intaddr)goto GotAnInterrupt;
        }
    }

    // if no interrupt was found then return having done nothing
    return 0;

    // an interrupt was found if we jumped to here
GotAnInterrupt:
    g_LocalIndex++;                                                   // IRETURN will decrement this
    if(OptionErrorSkip>0)SaveOptionErrorSkip=OptionErrorSkip;
    else SaveOptionErrorSkip = 0;
    OptionErrorSkip=0;
    strcpy(SaveErrorMessage , MMErrMsg);
    Saveerrno = MMerrno;
    *MMErrMsg = 0;
    MMerrno = 0;
    InterruptReturn = nextstmt;                                     // for when IRETURN is executed
    // if the interrupt is pointing to a SUB token we need to call a subroutine
    CommandToken tkn=commandtbl_decode((const unsigned char *)intaddr);
    if(tkn == cmdSUB) {
    	strncpy(CurrentInterruptName, intaddr + 2, MAXVARLEN);
        rti[0] = (cmdIRET & 0x7f ) + C_BASETOKEN;
        rti[1] = (cmdIRET >> 7) + C_BASETOKEN; //tokens can be 14-bit
        if(gosubindex >= MAXGOSUB) error("Too many SUBs for interrupt");
        errorstack[gosubindex] = CurrentLinePtr;
        gosubstack[gosubindex++] = (unsigned char *)rti;                             // return from the subroutine to the dummy IRETURN command
        g_LocalIndex++;                                               // return from the subroutine will decrement g_LocalIndex
        skipelement(intaddr);                                       // point to the body of the subroutine
    }
    nextstmt = (unsigned char *)intaddr;                                             // the next command will be in the interrupt routine
    return 1;
}
int __not_in_flash_func(check_interrupt)(void) {
#ifdef GUICONTROLS
    if(Ctrl!=NULL){
        if(!(DelayedDrawKeyboard || DelayedDrawFmtBox || calibrate))ProcessTouch();
        if(CheckGuiFlag) CheckGui();                                    // This implements a LED flash
    }
#endif
    hal_keyboard_service();

    if(!InterruptUsed) return 0;                                    // quick exit if there are no interrupts set
    if(InterruptReturn != NULL || CurrentLinePtr == NULL) return 0; // skip if we are in an interrupt or in immediate mode
    return checkdetailinterrupts();
}


// get the address for a MMBasic interrupt
// this will handle a line number, a label or a subroutine
// all areas of MMBasic that can generate an interrupt use this function
unsigned char *GetIntAddress(unsigned char *p) {
    int i;
    if(isnamestart((uint8_t)*p)) {                                           // if it starts with a valid name char
        i = FindSubFun(p, 0);                                       // try to find a matching subroutine
        if(i == -1)
            return findlabel(p);                                    // if a subroutine was NOT found it must be a label
        else
            return subfun[i];                                       // if a subroutine was found, return the address of the sub
    }

    return findline(getinteger(p), true);                           // otherwise try for a line number
}
