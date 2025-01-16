/*******************************************************************************************
*
*  Definitions used when calling MMBasic Interpreter API Functions from CFunctions
*  For PicoMite MMBasic V6.00.01
*
*  This file is public domain and may be used without license.
*
*  Use with AMRCFGENV144.bas
*
*  V1.6.2
*  NB: Base address has changed from previous versions to match V5.07.05
*  V1.6.3  struct option_s updated to match 5.07.05 as defined in fileIO.h
*  V1.6.4  Additional links
*  v1.6.5  Latest Beta29 and PICOMITEVGA PICOMITEWEB Compiled with GCC 11.2.1
*  v1.6.6  Updated to match 5.07.07 and 5.07.08b5 updates for fixed IP address
*  v1.6.7  Updated to match 5.07.08 release matches option_s and CSubComplete now char
*  v2.0.0  Updated for MMBasic 6.00.00 and also for RP2350 chip.
*          BaseAddress is different for PICO and PICO2 Set the correct #define
*          Note: Use ? HEX$(MM.INFO(CALLTABLE)) to verify the location of the calltable.
*          struct option_s updated to match v6.00.00RC15
*  v2.0.1  struct option_s updated to match v6.00.01 Release
*
******************************************************************************************/
/*** Uncomment one of these three  ***/
#define PICOMITE
//#define PICOMITEVGA
//#define PICOMITEWEB

/***  Uncomment this define if using PICO2 Chip  ***/
#define PICORP2350

/***  Uncomment this define if HDMI pins required  ***/
#define GUICONTROLS

/*****************************************************************************************/
#define MAXVARLEN           32                      // maximum length of a variable name
#define MAXDIM              5                       // maximum nbr of dimensions to an array
#define MMFLOAT double
#define MAXKEYLEN 64

//Addresses in the API Table for the pointers to each function
#ifdef PICORP2350
 #define BaseAddress   0x1000023C
#else
 #define BaseAddress   0x100002D4
#endif

#define Vector_uSec               (*(unsigned int *)(BaseAddress+0x00))       // void uSec(unsigned int us)
#define Vector_putConsole         (*(unsigned int *)(BaseAddress+0x04))       // void putConsole(int C))
#define Vector_getConsole         (*(unsigned int *)(BaseAddress+0x08))       // int getConsole(void)
#define Vector_ExtCfg             (*(unsigned int *)(BaseAddress+0x0C))       // void ExtCfg(int pin, int cfg, int option)
#define Vector_ExtSet             (*(unsigned int *)(BaseAddress+0x10))       // void ExtSet(int pin, int val)
#define Vector_ExtInp             (*(unsigned int *)(BaseAddress+0x14))       // int ExtInp(int pin)
#define Vector_PinSetBit          (*(unsigned int *)(BaseAddress+0x18))       // void PinSetBit(int pin, unsigned int offset)
#define Vector_PinRead            (*(unsigned int *)(BaseAddress+0x1C))       // int PinRead(int pin)
#define Vector_MMPrintString      (*(unsigned int *)(BaseAddress+0x20))       // void MMPrintString(char* s)
#define Vector_IntToStr           (*(unsigned int *)(BaseAddress+0x24))       // void IntToStr(char *strr, long long int nbr, unsigned int base)
#define Vector_CheckAbort         (*(unsigned int *)(BaseAddress+0x28))       // void CheckAbort(void)
#define Vector_GetMemory          (*(unsigned int *)(BaseAddress+0x2C))       // void *GetMemory(size_t msize);
#define Vector_GetTempMemory      (*(unsigned int *)(BaseAddress+0x30))       // void *GetTempMemory(int NbrBytes)
#define Vector_FreeMemory         (*(unsigned int *)(BaseAddress+0x34))       // void FreeMemory(void *addr)
#define Vector_DrawRectangle      *(unsigned int *)(BaseAddress+0x38 )        // void DrawRectangle(int x1, int y1, int x2, int y2, int C))
#define Vector_DrawBitmap         *(unsigned int *)(BaseAddress+0x3c )        // void DrawBitmap(int x1, int y1, int width, int height, int scale, int fg, int bg, unsigned char *bitmap )
#define Vector_DrawLine           (*(unsigned int *)(BaseAddress+0x40))       // void DrawLine(int x1, int y1, int x2, int y2, int w, int C))
#define Vector_FontTable          (*(unsigned int *)(BaseAddress+0x44))       // const unsigned char *FontTable[FONT_NBR]
#define Vector_ExtCurrentConfig   (*(unsigned int *)(BaseAddress+0x48))       // int ExtCurrentConfig[NBRPINS + 1];
#define Vector_HRes               (*(unsigned int *)(BaseAddress+0x4C))       // HRes
#define Vector_VRes               (*(unsigned int *)(BaseAddress+0x50))       // VRes
#define Vector_SoftReset          (*(unsigned int *)(BaseAddress+0x54))       // void SoftReset(void)
#define Vector_error              (*(unsigned int *)(BaseAddress+0x58))       // void error(char *msg)
#define Vector_ProgFlash          (*(unsigned int *)(BaseAddress+0x5C))       // ProgFlash
#define Vector_vartbl             (*(unsigned int *)(BaseAddress+0x60))       // g_vartbl
#define Vector_varcnt             (*(unsigned int *)(BaseAddress+0x64))       // g_varcnt
#define Vector_DrawBuffer         *(unsigned int *)(BaseAddress+0x68 )        // void DrawRectangle(int x1, int y1, int x2, int y2, int C))
#define Vector_ReadBuffer         *(unsigned int *)(BaseAddress+0x6c )        // void DrawRectangle(int x1, int y1, int x2, int y2, int C))
#define Vector_FloatToStr         (*(unsigned int *)(BaseAddress+0x70))       // convert a float to a string including scientific notation if necessary
#define Vector_ExecuteProgram     (*(unsigned int *)(BaseAddress+0x74))       // void ExecuteProgram(char *fname)
#define Vector_CFuncmSec          (*(unsigned int *)(BaseAddress+0x78))       // CFuncmSec
#define Vector_CFuncRam           (*(unsigned int *)(BaseAddress+0x7C))       // StartOfCFuncRam
#define Vector_ScrollLCD          *(unsigned int *)(BaseAddress+0x80 )        // void scrollLCD(int lines, int blank)
#define Vector_IntToFloat         (*(unsigned int *)(BaseAddress+0x84))       // MMFLOAT IntToFloat(long long int a)
#define Vector_FloatToInt         (*(unsigned int *)(BaseAddress+0x88))       // long long int FloatToInt64(MMFLOAT x)
#define Vector_Option             (*(unsigned int *)(BaseAddress+0x8C))       // Option
#define Vector_Sine               (*(unsigned int *)(BaseAddress+0x90))       // MMFLOAT sin(MMFLOAT)
#define Vector_DrawCircle         (*(unsigned int *)(BaseAddress+0x94))       // DrawCircle(int x, int y, int radius, int w, int c, int fill, MMFLOAT aspect)
#define Vector_DrawTriangle       (*(unsigned int *)(BaseAddress+0x98))       // DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int c, int fill)
#define Vector_Timer      (*(unsigned int *)(BaseAddress+0x9C))       // uint64_t timer(void)
#define Vector_FMul      (*(unsigned int *)(BaseAddress+0xA0))       // MMFLOAT FMul(MMFLOAT a, MMFLOAT b){ return a * b; }
#define Vector_FAdd      (*(unsigned int *)(BaseAddress+0xA4))       // MMFLOAT FAdd(MMFLOAT a, MMFLOAT b){ return a + b; }
#define Vector_FSub      (*(unsigned int *)(BaseAddress+0xA8))       // MMFLOAT FSub(MMFLOAT a, MMFLOAT b){ return a - b; }
#define Vector_FDiv        (*(unsigned int *)(BaseAddress+0xAC))       // MMFLOAT FDiv(MMFLOAT a, MMFLOAT b){ return a / b; }
#define Vector_FCmp      (*(unsigned int *)(BaseAddress+0xB0))       // int   FCmp(MMFLOAT a,MMFLOAT b){if(a>b) return 1;else if(a<b)return -1; else return 0;}
#define Vector_LoadFloat        (*(unsigned int *)(BaseAddress+0xB4))       /* MMFLOAT LoadFloat(unsigned long long C)){union ftype{ unsigned long long a; MMFLOAT b;}f;f.a=c;return f.b; }*/
#define Vector_CFuncInt1          *(unsigned int *)(BaseAddress+0xB8 )        // CFuncInt1
#define Vector_CFuncInt2          *(unsigned int *)(BaseAddress+0xBC)         // CFuncInt2
#define Vector_CSubComplete    (*(unsigned int *)(BaseAddress+0xC0))       // CSubComplete
#define Vector_AudioOutput        *(unsigned int *)(BaseAddress+0xC4)         // AudioOutput(int left, int right)
#define Vector_IDiv      (*(unsigned int *)(BaseAddress+0xC8))       // int IDiv(int a, int b){ return a / b; }
#define Vector_AUDIO_WRAP         (*(volatile unsigned int *)(BaseAddress+0xCC))// AUDIO_WRAP
#define Vector_CFuncInt3          *(unsigned int *)(BaseAddress+0xD0 )        // CFuncInt3
#define Vector_CFuncInt4          *(unsigned int *)(BaseAddress+0xD4)         // CFuncInt4
#define Vector_PIOExecute         (*(unsigned int *)(BaseAddress+0xD8))       // void PioExecute(int pio, int sm, uint32_t instruction)



//Macros to call each function.
#define uSec(a)                         ((void  (*)(unsigned long long )) Vector_uSec) (a)
#define putConsole(a,b)                 ((void(*)(int, int)) Vector_putConsole) (a,b)
#define getConsole()                    ((int (*)(void)) Vector_getConsole) ()
#define ExtCfg(a,b,c)                   ((void (*)(int, int, int)) Vector_ExtCfg) (a,b,c)
#define ExtSet(a,b)                     ((void(*)(int, int)) Vector_ExtSet) (a,b)
#define ExtInp(a)                       ((int(*)(int)) Vector_ExtInp) (a)
#define PinSetBit(a,b)                  ((void(*)(int, int)) Vector_PinSetBit) (a,b)
#define PinRead(a)                      ((int(*)(int)) Vector_PinRead) (a)
#define MMPrintString(a)                ((void (*)(char*)) Vector_MMPrintString) (a)
#define IntToStr(a,b,c)                 ((void (*)(char *, long long int, unsigned int)) Vector_IntToStr) (a,b,c)
#define CheckAbort()                    ((void (*)(void)) Vector_CheckAbort) ()
#define GetMemory(a)                    ((void* (*)(int)) Vector_GetMemory) (a)
#define GetTempMemory(a)                ((void* (*)(int)) Vector_GetTempMemory) (a)
#define FreeMemory(a)                   ((void (*)(void *)) Vector_FreeMemory) (a)
#define DrawRectangle(a,b,c,d,e)        ((void (*)(int,int,int,int,int)) (*(unsigned int *)Vector_DrawRectangle)) (a,b,c,d,e)
#define DrawRectangleVector             (*(unsigned int *)Vector_DrawRectangle)
#define DrawBitmap(a,b,c,d,e,f,g,h)     ((void (*)(int,int,int,int,int,int,int, char*)) (*(unsigned int *)Vector_DrawBitmap)) (a,b,c,d,e,f,g,h)
#define DrawBitmapVector                (*(unsigned int *)Vector_DrawBitmap)
#define DrawLine(a,b,c,d,e,f)           ((void (*)(int,int,int,int,int,int)) Vector_DrawLine) (a,b,c,d,e,f)
#define FontTable                       (void*)((int*)(Vector_FontTable))
#define ExtCurrentConfig                ((int *) Vector_ExtCurrentConfig)
#define HRes                            (*(unsigned int *) Vector_HRes)
#define VRes                            (*(unsigned int *) Vector_VRes)
#define SoftReset()                     ((void (*)(void)) Vector_SoftReset) ()
#define error(a)                        ((void (*)(char *)) Vector_error) (a)
#define ProgFlash                       ((int *) Vector_ProgFlash)
#define g_vartbl                          (*(struct s_vartbl *) Vector_vartbl)
#define g_varcnt                          (*(unsigned int *) Vector_varcnt)
#define DrawBuffer(a,b,c,d,e)           ((void (*)(int,int,int,int,char *)) (*(unsigned int *)Vector_DrawBuffer)) (a,b,c,d,e)
#define DrawBufferVector                (*(unsigned int *)Vector_DrawBuffer)
#define ReadBuffer(a,b,c,d,e)           ((void (*)(int,int,int,int,char *)) (*(unsigned int *)Vector_ReadBuffer)) (a,b,c,d,e)
#define ReadBufferVector                (*(unsigned int *)Vector_ReadBuffer)
#define FloatToStr(a,b,c,d,e)           ((void (*)(char *, MMFLOAT, int, int, char)) Vector_FloatToStr) (a,b,c,d,e)
// NOTE:  The argument to RunBasicSub is a string specifying the name of the BASIC subroutine to be executed.
//        It MUST be terminated with TWO null chars.
#define RunBasicSub(a)                  ((void (*)(char *)) Vector_ExecuteProgram) (a)
#define CFuncmSec                       (*(unsigned int *) Vector_CFuncmSec)
#define CFuncRam                        ((int *) Vector_CFuncRam)
#define ScrollLCD(a,b)                  ((void (*)(int, int)) (*(unsigned int *)Vector_ScrollLCD)) (a, b)
#define ScrollLCDVector                 (*(unsigned int *)Vector_ScrollLCD)
#define ScrollBufferV(a,b)              ((void (*)(int, int)) (*(unsigned int *)Vector_ScrollBufferV)) (a, b)
#define ScrollBufferVVector             (*(unsigned int *)Vector_ScrollBufferV)
#define ScrollBufferH(a)                ((void (*)(int)) (*(unsigned int *)Vector_ScrollBufferH)) (a)
#define ScrollBufferHVector             (*(unsigned int *)Vector_ScrollBufferH)
#define DrawBufferFast(a,b,c,d,e)       ((void (*)(int,int,int,int, char*)) (*(unsigned int *)Vector_DrawBufferFast)) (a,b,c,d,e)
#define DrawBufferFastVector            (*(unsigned int *)Vector_DrawBufferFast)
#define ReadBufferFast(a,b,c,d,e)       ((void (*)(int,int,int,int, char*)) (*(unsigned int *)Vector_ReadBufferFast)) (a,b,c,d,e)
#define ReadBufferFastVector            (*(unsigned int *)Vector_ReadBufferFast)
#define MoveBufferFast(a,b,c,d,e,f,g)   ((void (*)(int,int,int,int, int,int,int)) (*(unsigned int *)Vector_MoveBuffer)) (a,b,c,d,e,f,g)
#define MoveBufferFastVector            (*(unsigned int *)Vector_MoveBuffer)
#define DrawPixel(a,b,c)                ((void(*)(int, int, int)) Vector_DrawPixel) (a,b,c)
#define RoutineChecks()                 ((void (*)(void)) Vector_RoutineChecks) ()
#define GetPageAddress(a)               ((int(*)(int)) Vector_GetPageAddress) (a)
//#define memcpy(a,b,c)                   ((void (*)(void *, void *, int)) Vector_mycopysafe) (a,b,c)
#define IntToFloat(a)                   ((MMFLOAT (*)(long long)) Vector_IntToFloat) (a)
#define FloatToInt(a)                   ((long long (*)(MMFLOAT)) Vector_FloatToInt) (a)
#define Option (*(struct option_s *)(unsigned int)Vector_Option)
#define ReadPageAddress                 (*(unsigned int *) Vector_ReadPageAddress)
#define WritePageAddress                (*(unsigned int *) Vector_WritePageAddress)
#define uSecTimer                       ((unsigned long long (*)(void)) Vector_Timer)
#define FastTimer                       ((unsigned long long  (*)(void)) Vector_FastTimer)
#define TicksPerUsec                    (*(unsigned int *) Vector_TicksPerUsec)
#define map(a) ((int(*)(int)) Vector_Map) (a)
#define Sine(a)                         ((MMFLOAT (*)(MMFLOAT)) Vector_Sine) (a)
#define VideoColour                     (*(int *) Vector_VideoColour)
#define DrawCircle(a,b,c,d,e,f,g)       ((void (*)(int,int,int,int,int,int,MMFLOAT)) Vector_DrawCircle) (a,b,c,d,e,f,g)
#define DrawTriangle(a,b,c,d,e,f,g,h)   ((void (*)(int,int,int,int,int,int,int,int)) Vector_DrawTriangle) (a,b,c,d,e,f,g,h)
#define LoadFloat(a)                    ((MMFLOAT (*)(unsigned int)) Vector_LoadFloat) (a)
#define FMul(a,b)                       ((MMFLOAT (*)(MMFLOAT, MMFLOAT)) Vector_FMul) (a,b)
#define FAdd(a,b)                       ((MMFLOAT (*)(MMFLOAT, MMFLOAT)) Vector_FAdd) (a,b)
#define FSub(a,b)                       ((MMFLOAT (*)(MMFLOAT, MMFLOAT)) Vector_FSub) (a,b)
#define FDiv(a,b)                       ((MMFLOAT (*)(MMFLOAT, MMFLOAT)) Vector_FDiv) (a,b)
#define FCmp(a,b)                       ((int (*)(MMFLOAT, MMFLOAT)) Vector_FCmp) (a,b)
#define CFuncInt1                       (*(unsigned int *) Vector_CFuncInt1)
#define CFuncInt2                       (*(unsigned int *) Vector_CFuncInt2)
//#define Interrupt                       (*(unsigned int *) Vector_CSubComplete)
#define Interrupt                       (*(char *) Vector_CSubComplete)   //CSubComplete now char in 5.08.00
#define AudioOutputVector               (*(unsigned int *) Vector_AudioOutput)
#define AudioOutput(a,b)                ((void (*)(uint16_t, uint16_t)) (*(unsigned int *)Vector_AudioOutput)) (a, b)
#define IDiv(a,b)                       ((int (*)(int, int)) Vector_IDiv) (a,b)
#define AUDIO_WRAP                      (*(uint16_t *) Vector_AUDIO_WRAP)
#define CFuncInt3                       (*(unsigned int *) Vector_CFuncInt3)
#define CFuncInt4                       (*(unsigned int *) Vector_CFuncInt4)
#define PIOExecute(a,b,c)               ((void (*)(int, int, unsigned int)) Vector_PIOExecute) (a,b,c)



// the structure of the variable table, passed to the CFunction as a pointer Vector_vartbl which is #defined as g_vartbl
struct s_vartbl {                               // structure of the variable table
  char name[MAXVARLEN];                       // variable's name
  char type;                                  // its type (T_NUM, T_INT or T_STR)
  char level;                                 // its subroutine or function level (used to track local variables)
  unsigned char size;                         // the number of chars to allocate for each element in a string array
  char dummy;
  int __attribute__ ((aligned (4))) dims[MAXDIM];                     // the dimensions. it is an array if the first dimension is NOT zero
  union u_val{
      MMFLOAT f;                              // the value if it is a float
      long long int i;                        // the value if it is an integer
      MMFLOAT *fa;                            // pointer to the allocated memory if it is an array of floats
      long long int *ia;                      // pointer to the allocated memory if it is an array of integers
      char *s;                                // pointer to the allocated memory if it is a string
  }  __attribute__ ((aligned (8))) val;
} __attribute__ ((aligned (8))) val;

//  Useful macros


// Types used to define a variable in the variable table (g_vartbl).   Often they are ORed together.
// Also used in tokens and arguments to functions
#define T_NOTYPE       0                            // type not set or discovered
#define T_NBR       0x01                            // number (or float) type
#define T_STR       0x02                            // string type
#define T_INT       0x04                            // 64 bit integer type
#define T_PTR       0x08                            // the variable points to another variable's data
#define T_IMPLIED   0x10                            // the variables type does not have to be specified with a suffix
#define T_CONST     0x20                            // the contents of this variable cannot be changed
#define T_BLOCKED   0x40                            // Hash table entry blocked after ERASE


//***************************************************************************************************
// Constants and definitions copied from the Micromite MkII and Micromite Plus source
//***************************************************************************************************

//The Option structure

struct option_s {
   int  Magic;
   char Autorun;
   char Tab;
   char Invert;
   char Listcase; //8
 //
   unsigned int PROG_FLASH_SIZE;
   unsigned int HEAP_SIZE;
   char Height;
   char Width;
   unsigned char DISPLAY_TYPE;
   char DISPLAY_ORIENTATION; //12=20
//
   int  PIN;
   int  Baudrate;
   char  ColourCode;
   unsigned char MOUSE_CLOCK;
   unsigned char MOUSE_DATA;
   char spare;
   int CPU_Speed;
   unsigned int Telnet;           // 40 used to store status on console OFF/ON/BOTH
   int DefaultFC, DefaultBC;      // 44 the default colours
   short DefaultBrightness;         // 48 default backlight brightness //40
   unsigned char KEYBOARD_CLOCK;
   unsigned char KEYBOARD_DATA;
   unsigned short VGAFC, VGABC;  // the default colours 36=56  //50?
//
   // display related
   unsigned char DefaultFont;
   unsigned char KeyboardConfig;
   unsigned char RTC_Clock;
   unsigned char RTC_Data; //4=60
   //
   #ifdef PICOMITE
       int dummy;                // maximum number of controls allowed //48
   #endif

   #ifdef PICOMITEWEB
       uint16_t TCP_PORT;             // maximum number of controls allowed //48
       uint16_t ServerResponceTime;
   #endif
   #ifdef PICOMITEVGA
       int16_t X_TILE;                // maximum number of controls allowed //48
       int16_t Y_TILE;                // maximum number of controls allowed //48
   #endif
   // for the SPI LCDs 4=64
   unsigned char LCD_CD;
   unsigned char LCD_CS;
   unsigned char LCD_Reset;
   // touch related
   unsigned char TOUCH_CS;
   unsigned char TOUCH_IRQ;
   char TOUCH_SWAPXY;
   unsigned char repeat;
   char disabletftp;//56   8=72
   int  TOUCH_XZERO;
   int  TOUCH_YZERO;
   float TOUCH_XSCALE;
   float TOUCH_YSCALE; //72 16=88
#ifdef GUICONTROLS
   int MaxCtrls;
#else
   uint8_t HDMIclock;
   uint8_t HDMId0;
   uint8_t HDMId1;
   uint8_t HDMId2;
#endif
   unsigned int FlashSize; //8=96
   unsigned char SD_CS;
   unsigned char SYSTEM_MOSI;
   unsigned char SYSTEM_MISO;
   unsigned char SYSTEM_CLK;
   unsigned char DISPLAY_BL;
   unsigned char DISPLAY_CONSOLE;
   unsigned char TOUCH_Click;
   char LCD_RD;                   // used for the RD pin for SSD1963  //8=104
   unsigned char AUDIO_L;
   unsigned char AUDIO_R;
   unsigned char AUDIO_SLICE;
   unsigned char SDspeed;
   unsigned char pins[8];  //8=116                // general use storage for CFunctions written by PeterM //86
   char LCDVOP;
   char I2Coffset;
   unsigned char NoHeartbeat;
   char Refresh;
   unsigned char SYSTEM_I2C_SDA;
   unsigned char SYSTEM_I2C_SCL;
   unsigned char RTC;
   char PWM;  //8=124
   unsigned char INT1pin;
   unsigned char INT2pin;
   unsigned char INT3pin;
   unsigned char INT4pin;
   unsigned char SD_CLK_PIN;
   unsigned char SD_MOSI_PIN;
   unsigned char SD_MISO_PIN;
   unsigned char SerialConsole; //8=132
   unsigned char SerialTX;
   unsigned char SerialRX;
   unsigned char numlock;
   unsigned char capslock; //4=136
   unsigned int LIBRARY_FLASH_SIZE; // 4=140
   unsigned char AUDIO_CLK_PIN;
   unsigned char AUDIO_MOSI_PIN;
   unsigned char SYSTEM_I2C_SLOW;
   unsigned char AUDIO_CS_PIN;     //+4=144
   #ifdef PICOMITEWEB
       uint16_t UDP_PORT;
       uint16_t UDPServerResponceTime;
       char hostname[32];
       char ipaddress[16];
       char mask[16];
       char gateway[16];
       unsigned char x[1];  //112=256
   #else
       unsigned char x[85]; //112=256
   #endif
   unsigned char PSRAM_CS_PIN;
   unsigned char BGR;
   unsigned char NoScroll;
   unsigned char CombinedCS;
   unsigned char USBKeyboard;
   unsigned char VGA_HSYNC;
   unsigned char VGA_BLUE;
   unsigned char AUDIO_MISO_PIN;
   unsigned char AUDIO_DCS_PIN;
   unsigned char AUDIO_DREQ_PIN;
   unsigned char AUDIO_RESET_PIN;
   unsigned char SSD_DC;
   unsigned char SSD_WR;
   unsigned char SSD_RD;
   unsigned char SSD_RESET;
   unsigned char BackLightLevel;
   unsigned char NoReset;
   unsigned char AllPins;
   unsigned char modbuff;
short RepeatStart;
short RepeatRate;
   int modbuffsize;                // +18=
   unsigned char F1key[MAXKEYLEN];    //64=320
   unsigned char F5key[MAXKEYLEN];    //64=384
   unsigned char F6key[MAXKEYLEN];    //64=448
   unsigned char F7key[MAXKEYLEN];    //64=512
   unsigned char F8key[MAXKEYLEN];    //64=576
   unsigned char F9key[MAXKEYLEN];    //64=640
   unsigned char SSID[MAXKEYLEN];     //64=704
   unsigned char PASSWORD[MAXKEYLEN]; //64=768
   unsigned char platform[32];
   unsigned char extensions[96]; //128=896 == 7 XMODEM blocks
   // To enable older CFunctions to run any new options *MUST* be added at the end of the list
}  __attribute__((packed));


// Define the offsets from the PORT address
// these are used by GetPortAddr(a,b)
#define ANSEL               -8
#define ANSELCLR            -7
#define ANSELSET            -6
#define ANSELINV            -5
#define TRIS                -4
#define TRISCLR             -3
#define TRISSET             -2
#define TRISINV             -1
#define PORT                0
#define PORTCLR             1
#define PORTSET             2
#define PORTINV             3
#define LAT                 4
#define LATCLR              5
#define LATSET              6
#define LATINV              7
#define ODC                 8
#define ODCCLR              9
#define ODCSET              10
#define ODCINV              11
#define CNPU                12
#define CNPUCLR             13
#define CNPUSET             14
#define CNPUINV             15
#define CNPD                16
#define CNPDCLR             17
#define CNPDSET             18
#define CNPDINV             19
#define CNCON               20
#define CNCONCLR            21
#define CNCONSET            22
#define CNCONINV            23
#define CNEN                24
#define CNENCLR             25
#define CNENSET             26
#define CNENINV             27
#define CNSTAT              28
#define CNSTATCLR           29
#define CNSTATSET           30
#define CNSTATINV           31

// configurations for an I/O pin
// these are used by ExtCfg(a,b,c)
#define EXT_NOT_CONFIG          0
#define EXT_ANA_IN 1
#define EXT_DIG_IN 2
#define EXT_FREQ_IN 3
#define EXT_PER_IN 4
#define EXT_CNT_IN 5
#define EXT_INT_HI 6
#define EXT_INT_LO 7
#define EXT_DIG_OUT 8
#define EXT_HEARTBEAT 9
#define EXT_INT_BOTH  10
#define EXT_UART0TX 11
#define EXT_UART0RX 12
#define EXT_UART1TX 13
#define EXT_UART1RX 14
#define EXT_I2C0SDA 15
#define EXT_I2C0SCL 16
#define EXT_I2C1SDA 17
#define EXT_I2C1SCL 18
#define EXT_SPI0RX 19
#define EXT_SPI0TX 20
#define EXT_SPI0SCK 21
#define EXT_SPI1RX 22
#define EXT_SPI1TX 23
#define EXT_SPI1SCK 24
#define EXT_IR          25
#define EXT_INT1        26
#define EXT_INT2        27
#define EXT_INT3        28
#define EXT_INT4        29
#define EXT_PWM0A        30
#define EXT_PWM0B        31
#define EXT_PWM1A        32
#define EXT_PWM1B        33
#define EXT_PWM2A        34
#define EXT_PWM2B        35
#define EXT_PWM3A        36
#define EXT_PWM3B        37
#define EXT_PWM4A        38
#define EXT_PWM4B        39
#define EXT_PWM5A        40
#define EXT_PWM5B        41
#define EXT_PWM6A        42
#define EXT_PWM6B        43
#define EXT_PWM7A        44
#define EXT_PWM7B        45
#define EXT_PIO0_OUT      46
#define EXT_PIO1_OUT      47
#define EXT_DS18B20_RESERVED    0x100                 // this pin is reserved for DS18B20 and cannot be used
#define EXT_COM_RESERVED        0x200                 // this pin is reserved and SETPIN and PIN cannot be used
#define EXT_BOOT_RESERVED       0x400                 // this pin is reserved at bootup and cannot be used
#define NOP()  __asm volatile ("nop")
#define USERLCDPANEL            25