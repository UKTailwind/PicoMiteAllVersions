#include "pico_runtime_internal.h"

uint64_t __not_in_flash_func(uSecFunc)(uint64_t a){
    uint64_t b=time_us_64()+a;
    while(time_us_64()<b){}
    return b;
}
extern void MX470Display(int fn);
//Vector to CFunction routine called every command (ie, from the BASIC interrupt checker)
extern unsigned int CFuncInt1;
//Vector to CFunction routine called by the interrupt 2 handler
extern unsigned int CFuncInt2;
extern unsigned int CFuncmSec;
extern void CallCFuncInt1(void);
extern void CallCFuncInt2(void);
extern volatile bool CSubComplete;
static uint64_t __not_in_flash_func(uSecTimer)(void){ return time_us_64();}
static int64_t PinReadFunc(int a){return gpio_get(PinDef[a].GPno);}
extern void CallCFuncmSec(void);
#define CFUNCRAM_SIZE   256
int CFuncRam[CFUNCRAM_SIZE/sizeof(int)];
MMFLOAT IntToFloat(long long int a){ return a; }
MMFLOAT FMul(MMFLOAT a, MMFLOAT b){ return a * b; }
MMFLOAT FAdd(MMFLOAT a, MMFLOAT b){ return a + b; }
MMFLOAT FSub(MMFLOAT a, MMFLOAT b){ return a - b; }
MMFLOAT FDiv(MMFLOAT a, MMFLOAT b){ return a / b; }
uint32_t CFunc_delay_us;
/* QVGA scanout clock divider — used by the QVGA PIO state machine on
 * pure-VGA ports; HDMI ports use a different timing source. Defined
 * unconditionally; on HDMI ports the variable is unread. */
int QVGA_CLKDIV;
void PIOExecute(int pion, int sm, uint32_t ins){
    PIO pio = (pion ? pio1: pio0);
    pio_sm_exec(pio, sm, ins);
}

int IDiv(int a, int b){return a/b;}
int   FCmp(MMFLOAT a,MMFLOAT b){if(a>b) return 1;else if(a<b)return -1; else return 0;}
MMFLOAT LoadFloat(unsigned long long c){union ftype{ unsigned long long a; MMFLOAT b;}f;f.a=c;return f.b; }
const void * const CallTable[] __attribute__((section(".text")))  = {	(void *)uSecFunc,	//0x00
																		(void *)putConsole,	//0x04
																		(void *)getConsole,	//0x08
																		(void *)ExtCfg,	//0x0c
																		(void *)ExtSet,	//0x10
																		(void *)ExtInp,	//0x14
																		(void *)PinSetBit,	//0x18
																		(void *)PinReadFunc,	//0x1c
																		(void *)MMPrintString,	//0x20
																		(void *)IntToStr,	//0x24
																		(void *)CheckAbort,	//0x28
																		(void *)GetMemory,	//0x2c
																		(void *)GetTempMemory,	//0x30
																		(void *)FreeMemory, //0x34
																		(void *)&DrawRectangle,	//0x38
																		(void *)&DrawBitmap,	//0x3c
																		(void *)DrawLine,	//0x40
																		(void *)FontTable,	//0x44
																		(void *)&ExtCurrentConfig,	//0x48
																		(void *)&HRes,	//0x4C
																		(void *)&VRes,	//0x50
																		(void *)SoftReset, //0x54
																		(void *)error,	//0x58
																		(void *)&ProgMemory,	//0x5c
																		(void *)&g_vartbl, //0x60
																		(void *)&g_varcnt,  //0x64
																		(void *)&DrawBuffer,	//0x68
																		(void *)&ReadBuffer,	//0x6c
																		(void *)&FloatToStr,	//0x70
                                                                        (void *)CallExecuteProgram, //0x74
                                                                        (void *)&CFuncmSec, //0x78
                                                                        (void *)CFuncRam,	//0x7c
                                                                        (void *)&ScrollLCD,	//0x80
																		(void *)IntToFloat, //0x84
																		(void *)FloatToInt64,	//0x88
																		(void *)&Option,	//0x8c
																		(void *)sin,	//0x90
																		(void *)DrawCircle,	//0x94
																		(void *)DrawTriangle,	//0x98
																		(void *)uSecTimer,	//0x9c
                                                                        (void *)FMul,//0xa0
                                                                        (void *)FAdd,//0xa4
                                                                        (void *)FSub,//0xa8
                                                                        (void *)FDiv,//0xac
                                                                        (void *)FCmp,//0xb0
                                                                        (void *)&LoadFloat,//0xb4
                                                                        (void *)&CFuncInt1,	//0xb8
                                                                        (void *)&CFuncInt2,	//0xbc
																		(void *)&CSubComplete,	//0xc0
																		(void *)&AudioOutput,	//0xc4
                                                                        (void *)IDiv,//0x0xc8
                                                                        (void *)&AUDIO_WRAP,//0x0xcc
                                                                        (void *)&CFuncInt3,	//0xb8
                                                                        (void *)&CFuncInt4,	//0xbc
                                                                        (void *)PIOExecute,
	                                                                        };
