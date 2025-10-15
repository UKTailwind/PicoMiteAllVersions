/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic - I2C.h

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided with the distribution.
3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the
   original copyright message be displayed on the console at startup (additional copyright messages may be added).
4. All advertising materials mentioning features or use of this software must display the following acknowledgement:
   This product includes software developed by the <copyright holder>.
5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
************************************************************************************************************************/

#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
#ifndef I2C_HEADER
#define I2C_HEADER

/* ==============================================================================================================
 * I2C MASTER STATE MACHINE STATES
 * ============================================================================================================== */
#define I2C_State_Idle 0     // Bus Idle
#define I2C_State_Start 1    // Sending Start or Repeated Start
#define I2C_State_10Bit 2    // Sending a 10 bit address
#define I2C_State_10BitRcv 3 // 10 bit address receive
#define I2C_State_RcvAddr 4  // Receive address
#define I2C_State_Send 5     // Sending Data
#define I2C_State_Receive 6  // Receiving data
#define I2C_State_Ack 7      // Sending Acknowledgement
#define I2C_State_Stop 8     // Sending Stop

/* ==============================================================================================================
 * I2C STATUS FLAGS
 * ============================================================================================================== */
#define I2C_Status_Enabled 0x00000001
#define I2C_Status_MasterCmd 0x00000002
#define I2C_Status_NoAck 0x00000010
#define I2C_Status_Timeout 0x00000020
#define I2C_Status_InProgress 0x00000040
#define I2C_Status_Completed 0x00000080
#define I2C_Status_Interrupt 0x00000100
#define I2C_Status_BusHold 0x00000200
#define I2C_Status_10BitAddr 0x00000400
#define I2C_Status_BusOwned 0x00000800
#define I2C_Status_Send 0x00001000
#define I2C_Status_Receive 0x00002000
#define I2C_Status_Disable 0x00004000
#define I2C_Status_Master 0x00008000
#define I2C_Status_Slave 0x00010000
#define I2C_Status_Slave_Send 0x00020000
#define I2C_Status_Slave_Receive 0x00040000
#define I2C_Status_Slave_Send_Rdy 0x00080000
#define I2C_Status_Slave_Receive_Rdy 0x00100000
#define I2C_Status_Slave_Receive_Full 0x00200000

/* ==============================================================================================================
 * I2C CONFIGURATION
 * ============================================================================================================== */
#define SSD1306_I2C_Addr 0x3c
#define SystemI2CTimeout 5

/* ==============================================================================================================
 * ENUMERATIONS
 * ============================================================================================================== */

// OV7670 Camera Size Options
typedef enum
{
  OV7670_SIZE_DIV1 = 0, // 640 x 480
  OV7670_SIZE_DIV2,     // 320 x 240
  OV7670_SIZE_DIV4,     // 160 x 120
  OV7670_SIZE_DIV8,     // 80 x 60
  OV7670_SIZE_DIV16,    // 40 x 30
} OV7670_size;

// OV7670 Test Pattern Options
typedef enum
{
  OV7670_TEST_PATTERN_NONE = 0,       // Disable test pattern
  OV7670_TEST_PATTERN_SHIFTING_1,     // "Shifting 1" pattern
  OV7670_TEST_PATTERN_COLOR_BAR,      // 8 color bars
  OV7670_TEST_PATTERN_COLOR_BAR_FADE, // Color bars w/fade to white
} OV7670_pattern;

/* ==============================================================================================================
 * STRUCTURES
 * ============================================================================================================== */

// OV7670 Command Structure
typedef struct
{
  uint8_t reg;   // Register address
  uint8_t value; // Value to store
} OV7670_command;

// Nunchuk/Classic Controller Structure
typedef struct s_nunstruct
{
  char x;
  char y;
  int ax;            // classic left x
  int ay;            // classic left y
  int az;            // classic centre
  int Z;             // classic right x
  int C;             // classic right y
  int L;             // classic left analog
  int R;             // classic right analog
  unsigned short x0; // classic buttons
  unsigned short y0;
  unsigned short z0;
  unsigned short x1;
  unsigned short y1;
  unsigned short z1;
  uint64_t type;
  uint8_t calib[16];
  uint8_t classic[6];
  int16_t gyro[3];
  int16_t accs[3];
} a_nunstruct;

/* ==============================================================================================================
 * GLOBAL VARIABLES - I2C Core
 * ============================================================================================================== */
extern unsigned int I2C_Timer;            // master timeout counter
extern unsigned int I2C_Timeout;          // master timeout value
extern unsigned int I2C2_Timeout;         // master timeout value
extern volatile unsigned int I2C_Status;  // status flags
extern volatile unsigned int I2C2_Status; // status flags
extern bool I2C_enabled;                  // I2C enable marker
extern bool I2C2_enabled;                 // I2C2 enable marker
extern bool noRTC, noI2C;

/* ==============================================================================================================
 * GLOBAL VARIABLES - Interrupts
 * ============================================================================================================== */
extern char *I2C_IntLine;                // pointer to the master interrupt line number
extern char *I2C_Slave_Send_IntLine;     // pointer to the slave send interrupt line number
extern char *I2C_Slave_Receive_IntLine;  // pointer to the slave receive interrupt line number
extern char *I2C2_Slave_Send_IntLine;    // pointer to the slave send interrupt line number
extern char *I2C2_Slave_Receive_IntLine; // pointer to the slave receive interrupt line number

/* ==============================================================================================================
 * GLOBAL VARIABLES - Wii Controllers
 * ============================================================================================================== */
extern volatile uint8_t classic1, nunchuck1;
extern char *nunInterruptc[];
extern bool nunfoundc[];
extern uint8_t nunbuff[];
extern const unsigned char readcontroller[1];
extern volatile struct s_nunstruct nunstruct[6];
extern unsigned char classicread, nunchuckread;
extern int mmI2Cvalue;

/* ==============================================================================================================
 * FUNCTION PROTOTYPES - I2C Core
 * ============================================================================================================== */
extern void i2c_disable(void);
extern void i2c2_disable(void);
extern void I2C_Send_Command(char command);
extern uint8_t readRegister8(unsigned int addr, uint8_t reg);
extern void WriteRegister8(unsigned int addr, uint8_t reg, uint8_t data);
extern uint32_t readRegister32(unsigned int addr, uint8_t reg);
extern void Write8Register16(unsigned int addr, uint16_t reg, uint8_t data);
extern uint8_t read8Register16(unsigned int addr, uint16_t reg);

/* ==============================================================================================================
 * FUNCTION PROTOTYPES - Device Specific
 * ============================================================================================================== */
extern void RtcGetTime(int noerror);
extern void ConfigDisplayI2C(unsigned char *p);
extern void InitDisplayI2C(int InitOnly);
extern void CheckI2CKeyboard(int noerror, int read);
extern void cmd_camera(void);
extern void cmd_Classic(void);
extern void cameraclose(void);

/* ==============================================================================================================
 * FUNCTION PROTOTYPES - Wii Controllers
 * ============================================================================================================== */
extern void classicproc(void);
extern void nunproc(void);
extern void WiiReceive(int nbr, char *p);
extern void WiiSend(int nbr, char *p);

/* ==============================================================================================================
 * OV7670 CAMERA REGISTER DEFINITIONS
 * ============================================================================================================== */

// Core Registers
#define OV7670_REG_GAIN 0x00  // AGC gain bits 7:0 (9:8 in VREF)
#define OV7670_REG_BLUE 0x01  // AWB blue channel gain
#define OV7670_REG_RED 0x02   // AWB red channel gain
#define OV7670_REG_VREF 0x03  // Vert frame control bits
#define OV7670_REG_COM1 0x04  // Common control 1
#define OV7670_COM1_R656 0x40 // COM1 enable R656 format
#define OV7670_REG_BAVE 0x05  // U/B average level
#define OV7670_REG_GbAVE 0x06 // Y/Gb average level
#define OV7670_REG_AECHH 0x07 // Exposure value - AEC 15:10 bits
#define OV7670_REG_RAVE 0x08  // V/R average level

// Common Control Registers
#define OV7670_REG_COM2 0x09     // Common control 2
#define OV7670_COM2_SSLEEP 0x10  // COM2 soft sleep mode
#define OV7670_REG_PID 0x0A      // Product ID MSB (read-only)
#define OV7670_REG_VER 0x0B      // Product ID LSB (read-only)
#define OV7670_REG_COM3 0x0C     // Common control 3
#define OV7670_COM3_SWAP 0x40    // COM3 output data MSB/LSB swap
#define OV7670_COM3_SCALEEN 0x08 // COM3 scale enable
#define OV7670_COM3_DCWEN 0x04   // COM3 DCW enable
#define OV7670_REG_COM4 0x0D     // Common control 4
#define OV7670_REG_COM5 0x0E     // Common control 5
#define OV7670_REG_COM6 0x0F     // Common control 6

// Exposure and Clock Control
#define OV7670_REG_AECH 0x10  // Exposure value 9:2
#define OV7670_REG_CLKRC 0x11 // Internal clock
#define OV7670_CLK_EXT 0x40   // CLKRC Use ext clock directly
#define OV7670_CLK_SCALE 0x3F // CLKRC Int clock prescale mask

// COM7 - Output Format Control
#define OV7670_REG_COM7 0x12        // Common control 7
#define OV7670_COM7_RESET 0x80      // COM7 SCCB register reset
#define OV7670_COM7_SIZE_MASK 0x38  // COM7 output size mask
#define OV7670_COM7_PIXEL_MASK 0x05 // COM7 output pixel format mask
#define OV7670_COM7_SIZE_VGA 0x00   // COM7 output size VGA
#define OV7670_COM7_SIZE_CIF 0x20   // COM7 output size CIF
#define OV7670_COM7_SIZE_QVGA 0x10  // COM7 output size QVGA
#define OV7670_COM7_SIZE_QCIF 0x08  // COM7 output size QCIF
#define OV7670_COM7_RGB 0x04        // COM7 pixel format RGB
#define OV7670_COM7_YUV 0x00        // COM7 pixel format YUV
#define OV7670_COM7_BAYER 0x01      // COM7 pixel format Bayer RAW
#define OV7670_COM7_PBAYER 0x05     // COM7 pixel fmt proc Bayer RAW
#define OV7670_COM7_COLORBAR 0x02   // COM7 color bar enable

// COM8 - AGC/AEC/AWB Control
#define OV7670_REG_COM8 0x13     // Common control 8
#define OV7670_COM8_FASTAEC 0x80 // COM8 Enable fast AGC/AEC algo
#define OV7670_COM8_AECSTEP 0x40 // COM8 AEC step size unlimited
#define OV7670_COM8_BANDING 0x20 // COM8 Banding filter enable
#define OV7670_COM8_AGC 0x04     // COM8 AGC (auto gain) enable
#define OV7670_COM8_AWB 0x02     // COM8 AWB (auto white balance)
#define OV7670_COM8_AEC 0x01     // COM8 AEC (auto exposure) enable
#define OV7670_REG_COM9 0x14     // Common control 9 - max AGC value

// COM10 - Output Control
#define OV7670_REG_COM10 0x15      // Common control 10
#define OV7670_COM10_HSYNC 0x40    // COM10 HREF changes to HSYNC
#define OV7670_COM10_PCLK_HB 0x20  // COM10 Suppress PCLK on hblank
#define OV7670_COM10_HREF_REV 0x08 // COM10 HREF reverse
#define OV7670_COM10_VS_EDGE 0x04  // COM10 VSYNC chg on PCLK rising
#define OV7670_COM10_VS_NEG 0x02   // COM10 VSYNC negative
#define OV7670_COM10_HS_NEG 0x01   // COM10 HSYNC negative

// Frame Timing
#define OV7670_REG_HSTART 0x17 // Horiz frame start high bits
#define OV7670_REG_HSTOP 0x18  // Horiz frame end high bits
#define OV7670_REG_VSTART 0x19 // Vert frame start high bits
#define OV7670_REG_VSTOP 0x1A  // Vert frame end high bits
#define OV7670_REG_PSHFT 0x1B  // Pixel delay select

// Manufacturer ID
#define OV7670_REG_MIDH 0x1C // Manufacturer ID high byte
#define OV7670_REG_MIDL 0x1D // Manufacturer ID low byte

// Mirror/Flip Control
#define OV7670_REG_MVFP 0x1E    // Mirror / vert-flip enable
#define OV7670_MVFP_MIRROR 0x20 // MVFP Mirror image
#define OV7670_MVFP_VFLIP 0x10  // MVFP Vertical flip

// ADC Control
#define OV7670_REG_LAEC 0x1F    // Reserved
#define OV7670_REG_ADCCTR0 0x20 // ADC control
#define OV7670_REG_ADCCTR1 0x21 // Reserved
#define OV7670_REG_ADCCTR2 0x22 // Reserved
#define OV7670_REG_ADCCTR3 0x23 // Reserved

// AEC/AGC Control
#define OV7670_REG_AEW 0x24 // AGC/AEC upper limit
#define OV7670_REG_AEB 0x25 // AGC/AEC lower limit
#define OV7670_REG_VPT 0x26 // AGC/AEC fast mode op region

// Bias Control
#define OV7670_REG_BBIAS 0x27  // B channel signal output bias
#define OV7670_REG_GbBIAS 0x28 // Gb channel signal output bias
#define OV7670_REG_EXHCH 0x2A  // Dummy pixel insert MSB
#define OV7670_REG_EXHCL 0x2B  // Dummy pixel insert LSB
#define OV7670_REG_RBIAS 0x2C  // R channel signal output bias
#define OV7670_REG_ADVFL 0x2D  // Insert dummy lines MSB
#define OV7670_REG_ADVFH 0x2E  // Insert dummy lines LSB
#define OV7670_REG_YAVE 0x2F   // Y/G channel average value

// Timing Control
#define OV7670_REG_HSYST 0x30 // HSYNC rising edge delay
#define OV7670_REG_HSYEN 0x31 // HSYNC falling edge delay
#define OV7670_REG_HREF 0x32  // HREF control
#define OV7670_REG_CHLF 0x33  // Array current control
#define OV7670_REG_ARBLM 0x34 // Array ref control - reserved
#define OV7670_REG_ADC 0x37   // ADC control - reserved
#define OV7670_REG_ACOM 0x38  // ADC & analog common - reserved
#define OV7670_REG_OFON 0x39  // ADC offset control - reserved

// TSLB Control
#define OV7670_REG_TSLB 0x3A   // Line buffer test option
#define OV7670_TSLB_NEG 0x20   // TSLB Negative image enable
#define OV7670_TSLB_YLAST 0x04 // TSLB UYVY or VYUY, see COM13
#define OV7670_TSLB_AOW 0x01   // TSLB Auto output window

// COM11 - Night Mode
#define OV7670_REG_COM11 0x3B    // Common control 11
#define OV7670_COM11_NIGHT 0x80  // COM11 Night mode
#define OV7670_COM11_NMFR 0x60   // COM11 Night mode frame rate mask
#define OV7670_COM11_HZAUTO 0x10 // COM11 Auto detect 50/60 Hz
#define OV7670_COM11_BAND 0x08   // COM11 Banding filter val select
#define OV7670_COM11_EXP 0x02    // COM11 Exposure timing control

// COM12-14 Control
#define OV7670_REG_COM12 0x3C    // Common control 12
#define OV7670_COM12_HREF 0x80   // COM12 Always has HREF
#define OV7670_REG_COM13 0x3D    // Common control 13
#define OV7670_COM13_GAMMA 0x80  // COM13 Gamma enable
#define OV7670_COM13_UVSAT 0x40  // COM13 UV saturation auto adj
#define OV7670_COM13_UVSWAP 0x01 // COM13 UV swap, use w TSLB[3]
#define OV7670_REG_COM14 0x3E    // Common control 14
#define OV7670_COM14_DCWEN 0x10  // COM14 DCW & scaling PCLK enable
#define OV7670_REG_EDGE 0x3F     // Edge enhancement adjustment

// COM15 - Output Range Control
#define OV7670_REG_COM15 0x40     // Common control 15
#define OV7670_COM15_RMASK 0xC0   // COM15 Output range mask
#define OV7670_COM15_R10F0 0x00   // COM15 Output range 10 to F0
#define OV7670_COM15_R01FE 0x80   // COM15              01 to FE
#define OV7670_COM15_R00FF 0xC0   // COM15              00 to FF
#define OV7670_COM15_RGBMASK 0x30 // COM15 RGB 555/565 option mask
#define OV7670_COM15_RGB 0x00     // COM15 Normal RGB out
#define OV7670_COM15_RGB565 0x10  // COM15 RGB 565 output
#define OV7670_COM15_RGB555 0x30  // COM15 RGB 555 output

// COM16-17 Control
#define OV7670_REG_COM16 0x41     // Common control 16
#define OV7670_COM16_AWBGAIN 0x08 // COM16 AWB gain enable
#define OV7670_REG_COM17 0x42     // Common control 17
#define OV7670_COM17_AECWIN 0xC0  // COM17 AEC window must match COM4
#define OV7670_COM17_CBAR 0x08    // COM17 DSP Color bar enable

// AWB Control
#define OV7670_REG_AWBC1 0x43 // Reserved
#define OV7670_REG_AWBC2 0x44 // Reserved
#define OV7670_REG_AWBC3 0x45 // Reserved
#define OV7670_REG_AWBC4 0x46 // Reserved
#define OV7670_REG_AWBC5 0x47 // Reserved
#define OV7670_REG_AWBC6 0x48 // Reserved
#define OV7670_REG_REG4B 0x4B // UV average enable
#define OV7670_REG_DNSTH 0x4C // De-noise strength

// Color Matrix Coefficients
#define OV7670_REG_MTX1 0x4F // Matrix coefficient 1
#define OV7670_REG_MTX2 0x50 // Matrix coefficient 2
#define OV7670_REG_MTX3 0x51 // Matrix coefficient 3
#define OV7670_REG_MTX4 0x52 // Matrix coefficient 4
#define OV7670_REG_MTX5 0x53 // Matrix coefficient 5
#define OV7670_REG_MTX6 0x54 // Matrix coefficient 6

// Brightness and Contrast
#define OV7670_REG_BRIGHT 0x55         // Brightness control
#define OV7670_REG_CONTRAS 0x56        // Contrast control
#define OV7670_REG_CONTRAS_CENTER 0x57 // Contrast center
#define OV7670_REG_MTXS 0x58           // Matrix coefficient sign

// Lens Correction
#define OV7670_REG_LCC1 0x62 // Lens correction option 1
#define OV7670_REG_LCC2 0x63 // Lens correction option 2
#define OV7670_REG_LCC3 0x64 // Lens correction option 3
#define OV7670_REG_LCC4 0x65 // Lens correction option 4
#define OV7670_REG_LCC5 0x66 // Lens correction option 5

// Manual U/V and Gain
#define OV7670_REG_MANU 0x67  // Manual U value
#define OV7670_REG_MANV 0x68  // Manual V value
#define OV7670_REG_GFIX 0x69  // Fix gain control
#define OV7670_REG_GGAIN 0x6A // G channel AWB gain
#define OV7670_REG_DBLV 0x6B  // PLL & regulator control

// AWB Control Continued
#define OV7670_REG_AWBCTR3 0x6C // AWB control 3
#define OV7670_REG_AWBCTR2 0x6D // AWB control 2
#define OV7670_REG_AWBCTR1 0x6E // AWB control 1
#define OV7670_REG_AWBCTR0 0x6F // AWB control 0

// Scaling Control
#define OV7670_REG_SCALING_XSC 0x70      // Test pattern X scaling
#define OV7670_REG_SCALING_YSC 0x71      // Test pattern Y scaling
#define OV7670_REG_SCALING_DCWCTR 0x72   // DCW control
#define OV7670_REG_SCALING_PCLK_DIV 0x73 // DSP scale control clock divide
#define OV7670_REG_REG74 0x74            // Digital gain control
#define OV7670_REG_REG76 0x76            // Pixel correction
#define OV7670_REG_SLOP 0x7A             // Gamma curve highest seg slope

// Gamma Curve
#define OV7670_REG_GAM_BASE 0x7B // Gamma register base (1 of 15)
#define OV7670_GAM_LEN 15        // Number of gamma registers

// Pixel Correction Control
#define OV7670_R76_BLKPCOR 0x80 // REG76 black pixel corr enable
#define OV7670_R76_WHTPCOR 0x40 // REG76 white pixel corr enable

// RGB444 Control
#define OV7670_REG_RGB444 0x8C  // RGB 444 control
#define OV7670_R444_ENABLE 0x02 // RGB444 enable
#define OV7670_R444_RGBX 0x01   // RGB444 word format

// Dummy Line and Additional Lens Correction
#define OV7670_REG_DM_LNL 0x92 // Dummy line LSB
#define OV7670_REG_LCC6 0x94   // Lens correction option 6
#define OV7670_REG_LCC7 0x95   // Lens correction option 7

// Histogram-based AEC/AGC
#define OV7670_REG_HAECC1 0x9F             // Histogram-based AEC/AGC ctrl 1
#define OV7670_REG_HAECC2 0xA0             // Histogram-based AEC/AGC ctrl 2
#define OV7670_REG_SCALING_PCLK_DELAY 0xA2 // Scaling pixel clock delay
#define OV7670_REG_BD50MAX 0xA5            // 50 Hz banding step limit
#define OV7670_REG_HAECC3 0xA6             // Histogram-based AEC/AGC ctrl 3
#define OV7670_REG_HAECC4 0xA7             // Histogram-based AEC/AGC ctrl 4
#define OV7670_REG_HAECC5 0xA8             // Histogram-based AEC/AGC ctrl 5
#define OV7670_REG_HAECC6 0xA9             // Histogram-based AEC/AGC ctrl 6
#define OV7670_REG_HAECC7 0xAA             // Histogram-based AEC/AGC ctrl 7
#define OV7670_REG_BD60MAX 0xAB            // 60 Hz banding step limit

// ABLC and Saturation
#define OV7670_REG_ABLC1 0xB1  // ABLC enable
#define OV7670_REG_THL_ST 0xB3 // ABLC target
#define OV7670_REG_SATCTR 0xC9 // Saturation control

#define OV7670_REG_LAST OV7670_REG_SATCTR // Maximum register address

/* ==============================================================================================================
 * QQVGA SETTINGS
 * ============================================================================================================== */
#define HSTART_QQVGA 0x16
#define HSTOP_QQVGA 0x04
#define HREF_QQVGA 0xa4
#define VSTART_QQVGA 0x02
#define VSTOP_QQVGA 0x7a
#define VREF_QQVGA 0x0a
#define COM3_QQVGA 0x04
#define COM14_QQVGA 0x1A
#define SCALING_XSC_QQVGA 0x3a
#define SCALING_YSC_QQVGA 0x35
#define SCALING_DCWCTR_QQVGA 0x22
#define SCALING_PCLK_DIV_QQVGA 0xf2
#define SCALING_PCLK_DELAY_QQVGA 0x02

/* ==============================================================================================================
 * LEGACY REGISTER ALIASES (Maintained for backward compatibility)
 * ============================================================================================================== */
#define REG_GAIN 0x00     // Gain lower 8 bits (rest in vref)
#define REG_BLUE 0x01     // blue gain
#define REG_RED 0x02      // red gain
#define REG_VREF 0x03     // Pieces of GAIN, VSTART, VSTOP
#define REG_COM1 0x04     // Control 1
#define COM1_CCIR656 0x40 // CCIR656 enable
#define REG_BAVE 0x05     // U/B Average level
#define REG_GbAVE 0x06    // Y/Gb Average level
#define REG_AECHH 0x07    // AEC MS 5 bits
#define REG_RAVE 0x08     // V/R Average level
#define REG_COM2 0x09     // Control 2
#define COM2_SSLEEP 0x10  // Soft sleep mode
#define REG_PID 0x0a      // Product ID MSB
#define REG_VER 0x0b      // Product ID LSB
#define REG_COM3 0x0c     // Control 3
#define COM3_SWAP 0x40    // Byte swap
#define COM3_SCALEEN 0x08 // Enable scaling
#define COM3_DCWEN 0x04   // Enable downsamp/crop/window
#define REG_COM4 0x0d     // Control 4
#define REG_COM5 0x0e     // All "reserved"
#define REG_COM6 0x0f     // Control 6
#define REG_AECH 0x10     // More bits of AEC value
#define REG_CLKRC 0x11    // Clock control
#define CLK_EXT 0x40      // Use external clock directly
#define CLK_SCALE 0x3f    // Mask for internal clock scale
#define REG_COM7 0x12     // Control 7
#define COM7_RESET 0x80   // Register reset
#define COM7_FMT_MASK 0x38
#define COM7_FMT_VGA 0x00         // VGA format
#define COM7_FMT_CIF 0x20         // CIF format
#define COM7_FMT_QVGA 0x10        // QVGA format
#define COM7_FMT_QCIF 0x08        // QCIF format
#define COM7_RGB 0x04             // bits 0 and 2 - RGB format
#define COM7_YUV 0x00             // YUV
#define COM7_BAYER 0x01           // Bayer format
#define COM7_PBAYER 0x05          // "Processed bayer"
#define COM7_COLOR_BAR 0x02       // Enable Color Bar
#define REG_COM8 0x13             // Control 8
#define COM8_FASTAEC 0x80         // Enable fast AGC/AEC
#define COM8_AECSTEP 0x40         // Unlimited AEC step size
#define COM8_BFILT 0x20           // Band filter enable
#define COM8_AGC 0x04             // Auto gain enable
#define COM8_AWB 0x02             // White balance enable
#define COM8_AEC 0x01             // Auto exposure enable
#define REG_COM9 0x14             // Control 9 - gain ceiling
#define REG_COM10 0x15            // Control 10
#define COM10_HSYNC 0x40          // HSYNC instead of HREF
#define COM10_PCLK_HB 0x20        // Suppress PCLK on horiz blank
#define COM10_HREF_REV 0x08       // Reverse HREF
#define COM10_VS_LEAD 0x04        // VSYNC on clock leading edge
#define COM10_VS_NEG 0x02         // VSYNC negative
#define COM10_HS_NEG 0x01         // HSYNC negative
#define REG_HSTART 0x17           // Horiz start high bits
#define REG_HSTOP 0x18            // Horiz stop high bits
#define REG_VSTART 0x19           // Vert start high bits
#define REG_VSTOP 0x1a            // Vert stop high bits
#define REG_PSHFT 0x1b            // Pixel delay after HREF
#define REG_MIDH 0x1c             // Manuf. ID high
#define REG_MIDL 0x1d             // Manuf. ID low
#define REG_MVFP 0x1e             // Mirror / vflip
#define MVFP_MIRROR 0x20          // Mirror image
#define MVFP_FLIP 0x10            // Vertical flip
#define REG_AEW 0x24              // AGC upper limit
#define REG_AEB 0x25              // AGC lower limit
#define REG_VPT 0x26              // AGC/AEC fast mode op region
#define REG_HSYST 0x30            // HSYNC rising edge delay
#define REG_HSYEN 0x31            // HSYNC falling edge delay
#define REG_HREF 0x32             // HREF pieces
#define REG_TSLB 0x3a             // lots of stuff
#define TSLB_YLAST 0x04           // UYVY or VYUY - see com13
#define TSLB_UV 0x10              // enable special effects
#define TSLB_NEGATIVE 0x20        // enable special effects
#define REG_COM11 0x3b            // Control 11
#define COM11_NIGHT 0x80          // Night mode enable
#define COM11_NIGHT_FR2 0x20      // Night mode 1/2 of normal framerate
#define COM11_NIGHT_FR4 0x40      // Night mode 1/4 of normal framerate
#define COM11_NIGHT_FR8 0x60      // Night mode 1/8 of normal framerate
#define COM11_HZAUTO 0x10         // Auto detect 50/60 Hz
#define COM11_50HZ 0x08           // Manual 50Hz select
#define COM11_EXP 0x02            // Exposure timing can be less than limit
#define REG_COM12 0x3c            // Control 12
#define COM12_HREF 0x80           // HREF always
#define REG_COM13 0x3d            // Control 13
#define COM13_GAMMA 0x80          // Gamma enable
#define COM13_UVSAT 0x40          // UV saturation auto adjustment
#define COM13_UVSWAP 0x01         // V before U - w/TSLB
#define REG_COM14 0x3e            // Control 14
#define COM14_DCWEN 0x10          // DCW/PCLK-scale enable
#define COM14_MAN_SCAL 0x08       // Manual scaling enable
#define COM14_PCLK_DIV1 0x00      // PCLK divided by 1
#define COM14_PCLK_DIV2 0x01      // PCLK divided by 2
#define COM14_PCLK_DIV4 0x02      // PCLK divided by 4
#define COM14_PCLK_DIV8 0x03      // PCLK divided by 8
#define COM14_PCLK_DIV16 0x04     // PCLK divided by 16
#define REG_EDGE 0x3f             // Edge enhancement factor
#define REG_COM15 0x40            // Control 15
#define COM15_R10F0 0x00          // Data range 10 to F0
#define COM15_R01FE 0x80          //              01 to FE
#define COM15_R00FF 0xc0          //              00 to FF
#define COM15_RGB565 0x10         // RGB565 output
#define COM15_RGB555 0x30         // RGB555 output
#define COM15_RGB444 0x10         // RGB444 output
#define REG_COM16 0x41            // Control 16
#define COM16_AWBGAIN 0x08        // AWB gain enable
#define COM16_DENOISE 0x10        // Enable de-noise auto adjustment
#define COM16_EDGE 0x20           // Enable edge enhancement
#define REG_COM17 0x42            // Control 17
#define COM17_AECWIN 0xc0         // AEC window - must match COM4
#define COM17_CBAR 0x08           // DSP Color bar
#define REG_DENOISE_STRENGTH 0x4c // De-noise strength
#define REG_SCALING_XSC 0x70
#define REG_SCALING_YSC 0x71
#define REG_SCALING_DCWCTR 0x72
#define REG_SCALING_PCLK_DIV 0x73
#define REG_SCALING_PCLK_DELAY 0xa2
#define REG_CMATRIX_BASE 0x4f
#define REG_CMATRIX_1 0x4f
#define REG_CMATRIX_2 0x50
#define REG_CMATRIX_3 0x51
#define REG_CMATRIX_4 0x52
#define REG_CMATRIX_5 0x53
#define REG_CMATRIX_6 0x54
#define CMATRIX_LEN 6
#define REG_CMATRIX_SIGN 0x58
#define REG_BRIGHT 0x55   // Brightness
#define REG_CONTRAST 0x56 // Contrast control
#define REG_GFIX 0x69     // Fix gain control
#define REG_GGAIN 0x6a    // G channel AWB gain
#define REG_DBLV 0x6b     // PLL control
#define REG_REG76 0x76    // OV's name
#define R76_BLKPCOR 0x80  // Black pixel correction enable
#define R76_WHTPCOR 0x40  // White pixel correction enable
#define REG_RGB444 0x8c   // RGB 444 control
#define R444_ENABLE 0x02  // Turn on RGB444, overrides 5x5
#define R444_RGBX 0x01    // Empty nibble at end
#define R444_XBGR 0x00
#define REG_HAECC1 0x9f  // Hist AEC/AGC control 1
#define REG_HAECC2 0xa0  // Hist AEC/AGC control 2
#define REG_BD50MAX 0xa5 // 50hz banding step limit
#define REG_HAECC3 0xa6  // Hist AEC/AGC control 3
#define REG_HAECC4 0xa7  // Hist AEC/AGC control 4
#define REG_HAECC5 0xa8  // Hist AEC/AGC control 5
#define REG_HAECC6 0xa9  // Hist AEC/AGC control 6
#define REG_HAECC7 0xaa  // Hist AEC/AGC control 7
#define REG_BD60MAX 0xab // 60hz banding step limit

#endif // I2C_HEADER
#endif // !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
       /* @endcond */