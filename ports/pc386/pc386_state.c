/*
 * ports/pc386/pc386_state.c — global state container.
 *
 * MMBasic core declares a long list of globals as `extern` and expects
 * the host port to provide storage. The Pico SDK builds get them from
 * PicoMite.c; host_native gets them from host_runtime.c; this file
 * provides them for pc386. Almost-identical to host_runtime.c's BSS
 * block — the divergence is that pc386 has no SDL framebuffer, no
 * sim-server, and no POSIX terminal, so those host-specific bits
 * (host_runtime_*, host_screenshot_*, host_key_*) live in
 * pc386_runtime.c instead.
 *
 * Keep this file purely declarative. Behavioural code goes in
 * pc386_runtime.c.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* Hardware / system state */
uint32_t _excep_code = 0;
uint64_t _persistent = 0;
uint32_t ADC_dma_chan = 0;
uint32_t ADC_dma_chan2 = 0;
bool ADCDualBuffering = 0;
volatile unsigned int AHRSTimer = 0;
volatile int ConsoleTxBufHead = 0;
volatile int ConsoleTxBufTail = 0;
uint32_t core1stack[256] = {[0] = 0x12345678};
volatile int DISPLAY_TYPE = 0;
uint32_t dma_rx_chan = 0;
uint32_t dma_rx_chan2 = 0;
uint32_t dma_tx_chan = 0;
uint32_t dma_tx_chan2 = 0;
bool dmarunning = 0;
long long int *ds18b20Timers = NULL;
/* ExtCurrentConfig is provided by core/state/pin_state.c. */
/* FileTable lives in FileIO.c. */
const uint8_t *flash_progmemory = NULL;
/* FSerror lives in FileIO.c. */
int GPSchannel = 0;
/* gui_bcolour / gui_fcolour / gui_font live in Draw.c. */
uint8_t I2C0locked = 0;
uint8_t I2C1locked = 0;
unsigned char IgnorePIN = 0;
unsigned char *InterruptReturn = NULL;
int InterruptUsed = 0;
int last_adc = 0;
lfs_t lfs;
int MMCharPos = 0;
int mmI2Cvalue = 0;
int mmOWvalue = 0;
bool mouse0 = 0;
unsigned char *OnKeyGOSUB = NULL;
unsigned char *OnPS2GOSUB = NULL;
MMFLOAT optionangle = 0;
bool optionfastaudio = 0;
bool optionfulltime = 0;
bool optionlogging = 0;
int PromptFont = 1;
int PromptFC = 0xFFFFFF;
int PromptBC = 0;
volatile int  PS2code = 0;
volatile bool PS2int  = false;
volatile bool TCPreceived = false;
char         *TCPreceiveInterrupt = NULL;
/* realflashpointer lives in FileIO.c. */

/* Erased-flash buffers so Memory.c's scan loops terminate immediately. */
static unsigned char pc386_saved_vars_buf[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
unsigned char *SavedVarsFlash = pc386_saved_vars_buf;

unsigned char pc386_cfunction_buf[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

volatile unsigned int ScrewUpTimer = 0;
unsigned char *TickInt[NBRSETTICKS] = {NULL};
volatile int TickTimer[NBRSETTICKS] = {0};
int TickPeriod[NBRSETTICKS] = {0};
volatile unsigned char TickActive[NBRSETTICKS] = {0};
MMFLOAT VCC = 3.3;
bool useoptionangle = 0;
unsigned char WatchdogSet = 0;
unsigned char BreakKey = 3;
volatile int MMAbort = 0;
volatile unsigned int WDTimer = 0;

const struct s_PinDef PinDef[NBRPINS + 1] = {{0}};

/* Timer/system variables */
volatile long long int mSecTimer = 0;
volatile unsigned int PauseTimer = 0;
volatile unsigned int IntPauseTimer = 0;
volatile unsigned int Timer1 = 0, Timer2 = 0, Timer3 = 0, Timer4 = 0, Timer5 = 0;
volatile unsigned int diskchecktimer = 0;
volatile unsigned int clocktimer = 0;
volatile int ds18b20Timer = 0;
volatile unsigned int I2CTimer = 0;
volatile unsigned int MouseTimer = 0;
volatile unsigned int SecondsTimer = 0;
volatile int day_of_week = 0;
unsigned char PulsePin[NBR_PULSE_SLOTS] = {0};
unsigned char PulseDirection[NBR_PULSE_SLOTS] = {0};
int PulseCnt[NBR_PULSE_SLOTS] = {0};
int PulseActive = 0;
volatile int ClickTimer = 0;
int calibrate = 0;
volatile unsigned int InkeyTimer = 0;
volatile char ConsoleRxBuf[CONSOLE_RX_BUF_SIZE] = {0};
volatile int ConsoleRxBufHead = 0;
volatile int ConsoleRxBufTail = 0;
volatile char ConsoleTxBuf[CONSOLE_TX_BUF_SIZE] = {0};
unsigned char SPIatRisk = 0;
int ExitMMBasicFlag = 0;
unsigned int _excep_peek = 0;
int OptionErrorCheck = 0;
unsigned int CurrentCpuSpeed = 0;
unsigned int PeripheralBusSpeed = 0;
unsigned char EchoOption = 0;
volatile unsigned int GPSTimer = 0;
uint16_t AUDIO_L_PIN = 0, AUDIO_R_PIN = 0, AUDIO_SLICE = 0;
uint16_t AUDIO_WRAP = 0;
int ticks_per_second = 1000;
lfs_dir_t lfs_dir;
struct lfs_info lfs_info;
/* DisplayHRes / DisplayVRes live in Draw.c. */
int ScreenSize = 0;
unsigned char *DisplayBuf = NULL;
unsigned char *SecondLayer = NULL;
unsigned char *SecondFrame = NULL;
char LCDAttrib = 0;
/* camera[] is provided by core/state/display_state.c. */
/* RGB121map[] lives in Draw.c. */

/* Interrupt-related */
volatile int INT0Value = 0, INT0InitTimer = 0, INT0Timer = 0;
volatile int INT1Value = 0, INT1InitTimer = 0, INT1Timer = 0;
volatile int INT2Value = 0, INT2InitTimer = 0, INT2Timer = 0;
volatile int INT3Value = 0, INT3InitTimer = 0, INT3Timer = 0;
volatile int INT4Value = 0, INT4InitTimer = 0, INT4Timer = 0;
volatile int64_t INT1Count = 0, INT2Count = 0, INT3Count = 0, INT4Count = 0;
volatile uint64_t INT5Count = 0, INT5Value = 0, INT5InitTimer = 0, INT5Timer = 0;
struct s_inttbl inttbl[NBRINTERRUPTS] = {{0}};

/* PWM / UART / SPI / I2C pin assignments */
uint8_t PWM0Apin = 0, PWM0Bpin = 0;
uint8_t PWM1Apin = 0, PWM1Bpin = 0;
uint8_t PWM2Apin = 0, PWM2Bpin = 0;
uint8_t PWM3Apin = 0, PWM3Bpin = 0;
uint8_t PWM4Apin = 0, PWM4Bpin = 0;
uint8_t PWM5Apin = 0, PWM5Bpin = 0;
uint8_t PWM6Apin = 0, PWM6Bpin = 0;
uint8_t PWM7Apin = 0, PWM7Bpin = 0;

uint8_t UART1RXpin = 0, UART1TXpin = 0;
uint8_t UART0TXpin = 0, UART0RXpin = 0;
uint8_t SPI1TXpin = 0, SPI1RXpin = 0, SPI1SCKpin = 0;
uint8_t SPI0TXpin = 0, SPI0RXpin = 0, SPI0SCKpin = 0;
uint8_t I2C1SDApin = 0, I2C1SCLpin = 0;
uint8_t I2C0SDApin = 0, I2C0SCLpin = 0;
uint8_t slice0 = 0, slice1 = 0, slice2 = 0, slice3 = 0;
uint8_t slice4 = 0, slice5 = 0, slice6 = 0, slice7 = 0;
uint8_t SPI0locked = 0, SPI1locked = 0;
volatile int CallBackEnabled = 0;
int ADCopen = 0;
volatile MMFLOAT *volatile a1float = NULL, *volatile a2float = NULL;
volatile MMFLOAT *volatile a3float = NULL, *volatile a4float = NULL;
uint32_t ADCmax = 0;
char *ADCInterrupt = NULL;
short *ADCbuffer = NULL;
volatile uint8_t *adcint = NULL;
uint8_t *adcint1 = NULL, *adcint2 = NULL;
unsigned char *KeypadInterrupt = NULL;
MMFLOAT ADCscale[4] = {0}, ADCbottom[4] = {0};

/* IR */
void *IrDev = NULL, *IrCmd = NULL;
volatile char IrVarType = 0, IrState = 0, IrGotMsg = 0;
int IrBits = 0, IrCount = 0;
unsigned char *IrInterrupt = NULL;
unsigned int CFuncInt1 = 0, CFuncInt2 = 0, CFuncInt3 = 0, CFuncInt4 = 0;

int p100interrupts[NBRPINS + 1] = {0};

int BacklightSlice = 0, BacklightChannel = 0;

/* QVGA / display */
int QVGA_CLKDIV = 0;
volatile int X_TILE = 0, Y_TILE = 0;
int CameraSlice = 0, CameraChannel = 0;
char id_out[256] = {0};
uint8_t *buff320 = NULL;
uint16_t SD_CLK_PIN = 0, SD_MOSI_PIN = 0, SD_MISO_PIN = 0, SD_CS_PIN = 0;
bool screen320 = 0;

/* PINMAP */
const uint8_t PINMAP[30] = {0};

/* PinFunction */
const char *PinFunction[64] = {NULL};

/* Tile color arrays */
uint8_t map16[16] = {0};
uint16_t tilefcols[80*40] = {0};
uint16_t tilebcols[80*40] = {0};

/* PS/2 mouse pins */
int MOUSE_CLOCK = 0, MOUSE_DATA = 0;

volatile uint64_t IRoffset = 0;

/* dma_hw / watchdog_hw stubs — Commands.c references their fields. */
#include "hardware/dma.h"
#include "hardware/structs/watchdog.h"
static dma_hw_t      pc386_dma_store  = {0};
static watchdog_hw_t pc386_wdog_store = {0};
dma_hw_t      *dma_hw      = &pc386_dma_store;
watchdog_hw_t *watchdog_hw = &pc386_wdog_store;
