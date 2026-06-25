/***********************************************************************************************************************
PicoMite MMBasic

I2C.c

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
/**
 * @file I2C.c
 * @author Geoff Graham, Peter Mather, Ernst Bokkelkamp
 * @brief Source for I2C MMBasic commands
 */
/**
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"

#include "Hardware_Includes.h"

#include "hardware/i2c.h"

#include "hardware/irq.h"

#define PinRead(a) gpio_get(PinDef[a].GPno)
extern void DrawBufferMEM(int x1, int y1, int x2, int y2, unsigned char *p);
extern void ReadBufferMEM(int x1, int y1, int x2, int y2, unsigned char *buff);
// Declare functions
void i2cEnable(unsigned char *p);
void i2cDisable(unsigned char *p);
void i2cSend(unsigned char *p);
void i2cSendSlave(unsigned char *p, int channel);
void i2cReceive(unsigned char *p);
void i2c_disable(void);
void i2c_enable(int bps);
int i2c_masterCommand(int timer, unsigned char *buff, int update_global);
void i2cCheck(unsigned char *p);
void i2c2Enable(unsigned char *p);
void i2c2Disable(unsigned char *p);
void i2c2Send(unsigned char *p);
void i2c2Receive(unsigned char *p);
void i2c2_disable(void);
void i2c2_enable(int bps);
int i2c2_masterCommand(int timer, unsigned char *buff, int update_global);
void i2c2Check(unsigned char *p);
static MMFLOAT *I2C_Rcvbuf_Float;          // pointer to the master receive buffer for a MMFLOAT
static long long int *I2C_Rcvbuf_Int;      // pointer to the master receive buffer for an integer
static char *I2C_Rcvbuf_String;            // pointer to the master receive buffer for a string
static unsigned int I2C_Addr;              // I2C device address
static volatile unsigned int I2C_Sendlen;  // length of the master send buffer
static volatile unsigned int I2C_Rcvlen;   // length of the master receive buffer
static unsigned char I2C_Send_Buffer[256]; // I2C send buffer
bool I2C_enabled = false;                  // I2C enable marker
unsigned int I2C_Timeout;                  // master timeout value
volatile unsigned int I2C_Status = 0;      // status flags
int mmI2Cvalue;
// value of MM.I2C
static MMFLOAT *I2C2_Rcvbuf_Float;         // pointer to the master receive buffer for a MMFLOAT
static long long int *I2C2_Rcvbuf_Int;     // pointer to the master receive buffer for an integer
static char *I2C2_Rcvbuf_String;           // pointer to the master receive buffer for a string
static unsigned int I2C2_Addr;             // I2C device address
static volatile unsigned int I2C2_Sendlen; // length of the master send buffer
static volatile unsigned int I2C2_Rcvlen;  // length of the master receive buffer
// static unsigned char I2C_Send_Buffer[256];                                   // I2C send buffer
bool I2C2_enabled = false;             // I2C enable marker
unsigned int I2C2_Timeout;             // master timeout value
volatile unsigned int I2C2_Status = 0; // status flags
// static char I2C_Rcv_Buffer[256];                                // I2C receive buffer
static unsigned int I2C_Slave_Addr; // slave address
char *I2C_Slave_Send_IntLine;       // pointer to the slave send interrupt line number
char *I2C_Slave_Receive_IntLine;    // pointer to the slave receive interrupt line number
// static char I2C2_Rcv_Buffer[256];                                // I2C receive buffer
char *I2C2_Slave_Send_IntLine;       // pointer to the slave send interrupt line number
char *I2C2_Slave_Receive_IntLine;    // pointer to the slave receive interrupt line number
static unsigned int I2C2_Slave_Addr; // slave address
bool noRTC = false, noI2C = false;
extern void SaveToBuffer(void);
extern void CompareToBuffer(void);
extern void DrawPixelMEM(int x1, int y1, int c);
extern void DrawRectangleMEM(int x1, int y1, int x2, int y2, int c);
extern void DrawBitmapMEM(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
void i2cSlave(unsigned char *p);
void i2c2Slave(unsigned char *p);
void i2cReceiveSlave(unsigned char *p, int channel);
int CameraSlice = -1;
int CameraChannel = -1;
extern void PWMoff(int slice);
const unsigned char nuninit[2] = {
    0xF0,
    0x55};
const unsigned char nuninit2[2] = {
    0xFB,
    0x0};
const unsigned char readcontroller[1] = {
    0};
const unsigned char nunid[1] = {
    0xFC};
const unsigned char nuncalib[1] = {
    0x20};
volatile uint8_t classic1 = false, nunchuck1 = false;
uint8_t nunbuff[10];
uint32_t swap32(uint32_t in)
{
  in = __builtin_bswap32(in);
  return in;
}
volatile struct s_nunstruct nunstruct[6];
char *nunInterruptc[6] = {
    NULL};
bool nunfoundc[6] = {
    false};
unsigned char classicread = 0, nunchuckread = 0;
/*******************************************************************************************
                I2C related commands in MMBasic
                              ===============================
These are the functions responsible for executing the I2C related commands in MMBasic
They are supported by utility functions that are grouped at the end of this file

********************************************************************************************/
int I2C_Send_Command(char command, int update_global)
{
  int i2cret;
  int status = 0;
  int i2caddr = SSD1306_I2C_Addr;
  I2C_Send_Buffer[0] = 0;
  I2C_Send_Buffer[1] = command;
  I2C_Sendlen = 2;
  I2C_Timeout = 1000;
  if (I2C1locked)
    i2cret = i2c_write_timeout_us(i2c1, (uint8_t)i2caddr, (uint8_t *)I2C_Send_Buffer, I2C_Sendlen, false, I2C_Timeout * 1000);
  else
    i2cret = i2c_write_timeout_us(i2c0, (uint8_t)i2caddr, (uint8_t *)I2C_Send_Buffer, I2C_Sendlen, false, I2C_Timeout * 1000);
  if (i2cret == PICO_ERROR_GENERIC)
    status = 1;
  if (i2cret == PICO_ERROR_TIMEOUT)
    status = 2;
  if (update_global)
    mmI2Cvalue = status;
  return status;
}
int I2C_Send_Data(unsigned char *data, int n, int update_global)
{
  int i2cret;
  int status = 0;
  int i, i2caddr = SSD1306_I2C_Addr;
  I2C_Sendlen = n + 1;
  I2C_Send_Buffer[0] = 0x40;
  I2C_Timeout = 1000;
  for (i = 1; i <= n; i++)
  {
    I2C_Send_Buffer[i] = data[i - 1];
  }
  if (I2C1locked)
    i2cret = i2c_write_timeout_us(i2c1, (uint8_t)i2caddr, (uint8_t *)I2C_Send_Buffer, I2C_Sendlen, false, I2C_Timeout * 1000);
  else
    i2cret = i2c_write_timeout_us(i2c0, (uint8_t)i2caddr, (uint8_t *)I2C_Send_Buffer, I2C_Sendlen, false, I2C_Timeout * 1000);
  if (i2cret == PICO_ERROR_GENERIC)
    status = 1;
  if (i2cret == PICO_ERROR_TIMEOUT)
    status = 2;
  if (update_global)
    mmI2Cvalue = status;
  return status;
}
#ifndef PICOMITEVGA
void ConfigDisplayI2C(unsigned char *p)
{
  unsigned char DISPLAY_TYPE = 0;
  getcsargs(&p, 5);
  if (!(argc == 3 || argc == 5))
    StandardError(2);
  if (checkstring(argv[0], (unsigned char *)"SSD1306I2C"))
  {
    DISPLAY_TYPE = SSD1306I2C;
  }
  else if (checkstring(argv[0], (unsigned char *)"SSD1306I2C32"))
  {
    DISPLAY_TYPE = SSD1306I2C32;
  }
  else
    error("Invalid display type");

  if (checkstring(argv[2], (unsigned char *)"L") || checkstring(argv[2], (unsigned char *)"LANDSCAPE"))
    Option.DISPLAY_ORIENTATION = LANDSCAPE;
  else if (checkstring(argv[2], (unsigned char *)"P") || checkstring(argv[2], (unsigned char *)"PORTRAIT"))
    Option.DISPLAY_ORIENTATION = PORTRAIT;
  else if (checkstring(argv[2], (unsigned char *)"RL") || checkstring(argv[2], (unsigned char *)"RLANDSCAPE"))
    Option.DISPLAY_ORIENTATION = RLANDSCAPE;
  else if (checkstring(argv[2], (unsigned char *)"RP") || checkstring(argv[2], (unsigned char *)"RPORTRAIT"))
    Option.DISPLAY_ORIENTATION = RPORTRAIT;
  else
    error("Orientation");
  Option.I2Coffset = 0;
  if (argc == 5)
    Option.I2Coffset = getint(argv[4], 0, 10);
  if (!(I2C0locked || I2C1locked))
    StandardError(44);
  Option.Refresh = 1;
  Option.DISPLAY_TYPE = DISPLAY_TYPE;
}

void InitDisplayI2C(int InitOnly)
{
  if (Option.DISPLAY_TYPE == 0 || Option.DISPLAY_TYPE > I2C_PANEL)
    return;
  //	I2Con();
  //	i2c_enable(display_details[Option.DISPLAY_TYPE].speed);
  DrawRectangle = DrawRectangleMEM;
  DrawBitmap = DrawBitmapMEM;
  DrawBuffer = DrawBufferMEM;
  ReadBuffer = ReadBufferMEM;
  DrawPixel = DrawPixelMEM;
  DrawBLITBuffer = DrawBufferMEM;
  ReadBLITBuffer = ReadBufferMEM;
  DisplayHRes = display_details[Option.DISPLAY_TYPE].horizontal;
  DisplayVRes = display_details[Option.DISPLAY_TYPE].vertical;
  I2C_Send_Command(0xAE, 1); // DISPLAYOFF

  I2C_Send_Command(0xD5, 1); // DISPLAYCLOCKDIV
  I2C_Send_Command(0xF0, 1); // the suggested ratio &H80

  I2C_Send_Command(0xA8, 1); // MULTIPLEX
  if (Option.DISPLAY_TYPE == SSD1306I2C)
    I2C_Send_Command(0x3F, 1);
  else if (Option.DISPLAY_TYPE == SSD1306I2C32)
    I2C_Send_Command(0x1F, 1);

  I2C_Send_Command(0xD3, 1); // DISPLAYOFFSET
  I2C_Send_Command(0x0, 1);  // no offset

  I2C_Send_Command(0x40, 1); // STARTLINE

  I2C_Send_Command(0x8D, 1); // CHARGEPUMP
  I2C_Send_Command(0x14, 1);

  I2C_Send_Command(0x20, 1); // MEMORYMODE
  I2C_Send_Command(0x00, 1); //&H0 act like ks0108

  I2C_Send_Command(0xA1, 1); // SEGREMAP OR 1
  I2C_Send_Command(0xC8, 1); // COMSCANDEC

  I2C_Send_Command(0xDA, 1); // COMPINS
  if (Option.DISPLAY_TYPE == SSD1306I2C)
    I2C_Send_Command(0x12, 1);
  else if (Option.DISPLAY_TYPE == SSD1306I2C32)
    I2C_Send_Command(0x02, 1);

  I2C_Send_Command(0x81, 1); // SETCONTRAST
  I2C_Send_Command(0xCF, 1);

  I2C_Send_Command(0xd9, 1); // SETPRECHARGE
  I2C_Send_Command(0x22, 1);

  I2C_Send_Command(0xDB, 1); // VCOMDETECT
  I2C_Send_Command(0x20, 1);

  I2C_Send_Command(0xA4, 1); // DISPLAYALLON_RESUME
  I2C_Send_Command(0xA6, 1); // NORMALDISPLAY
  I2C_Send_Command(0xAF, 1); // DISPLAYON
  if (Option.DISPLAY_ORIENTATION & 1)
  {
    VRes = DisplayVRes;
    HRes = DisplayHRes;
  }
  else
  {
    VRes = DisplayHRes;
    HRes = DisplayVRes;
  }
  if (!InitOnly)
  {
    ResetDisplay();
    ClearScreen(0);
    Display_Refresh();
  }
}
#endif
/*  @endcond */

void cmd_i2c(void)
{
  unsigned char *p; //, *pp;
  if (I2C0SDApin == 99 || I2C0SCLpin == 99)
    error("Pin not set for I2C");

  if ((p = checkstring(cmdline, (unsigned char *)"OPEN")) != NULL)
    i2cEnable(p);
  else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE")) != NULL)
    i2cDisable(p);
  else if ((p = checkstring(cmdline, (unsigned char *)"WRITE")) != NULL)
  {
    if (I2C0SDApin == Option.SYSTEM_I2C_SDA)
      I2C_Timeout = 1000;
    i2cSend(p);
    if (I2C0SDApin == Option.SYSTEM_I2C_SDA)
      I2C_Timeout = SystemI2CTimeout;
  }
  else if ((p = checkstring(cmdline, (unsigned char *)"READ")) != NULL)
  {
    if (I2C0SDApin == Option.SYSTEM_I2C_SDA)
      I2C_Timeout = 1000;
    i2cReceive(p);
    if (I2C0SDApin == Option.SYSTEM_I2C_SDA)
      I2C_Timeout = SystemI2CTimeout;
  }
  else if ((p = checkstring(cmdline, (unsigned char *)"CHECK")) != NULL)
    i2cCheck(p);
  else if ((p = checkstring(cmdline, (unsigned char *)"SLAVE OPEN")) != NULL)
    i2cSlave(p);
  else if ((p = checkstring(cmdline, (unsigned char *)"SLAVE READ")) != NULL)
    i2cReceiveSlave(p, 0);
  else if ((p = checkstring(cmdline, (unsigned char *)"SLAVE WRITE")) != NULL)
    i2cSendSlave(p, 0);
  else if ((p = checkstring(cmdline, (unsigned char *)"SLAVE CLOSE")) != NULL)
    i2cDisable(p);
  else
    StandardError(36);
}
void cmd_i2c2(void)
{
  unsigned char *p; //, *pp;
  if (I2C1SDApin == 99 || I2C1SCLpin == 99)
    error("Pin not set for I2C2");

  if ((p = checkstring(cmdline, (unsigned char *)"OPEN")) != NULL)
    i2c2Enable(p);
  else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE")) != NULL)
    i2c2Disable(p);
  else if ((p = checkstring(cmdline, (unsigned char *)"WRITE")) != NULL)
  {
    if (I2C1SDApin == Option.SYSTEM_I2C_SDA)
      I2C2_Timeout = 1000;
    i2c2Send(p);
    if (I2C1SDApin == Option.SYSTEM_I2C_SDA)
      I2C2_Timeout = SystemI2CTimeout;
  }
  else if ((p = checkstring(cmdline, (unsigned char *)"READ")) != NULL)
  {
    if (I2C1SDApin == Option.SYSTEM_I2C_SDA)
      I2C2_Timeout = 1000;
    i2c2Receive(p);
    if (I2C1SDApin == Option.SYSTEM_I2C_SDA)
      I2C2_Timeout = SystemI2CTimeout;
  }
  else if ((p = checkstring(cmdline, (unsigned char *)"CHECK")) != NULL)
    i2c2Check(p);
  else if ((p = checkstring(cmdline, (unsigned char *)"SLAVE OPEN")) != NULL)
    i2c2Slave(p);
  else if ((p = checkstring(cmdline, (unsigned char *)"SLAVE READ")) != NULL)
    i2cReceiveSlave(p, 1);
  else if ((p = checkstring(cmdline, (unsigned char *)"SLAVE WRITE")) != NULL)
    i2cSendSlave(p, 1);
  else if ((p = checkstring(cmdline, (unsigned char *)"SLAVE CLOSE")) != NULL)
    i2c2Disable(p);
  else
    StandardError(36);
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

void __not_in_flash_func(i2c0_irq_handler)(void)
{
  // Get interrupt status
  uint32_t status = i2c0->hw->intr_stat;
  // is a write request? Or a read request ? event
  if (status & I2C_IC_INTR_STAT_R_RX_FULL_BITS)
  {
    i2c0->hw->intr_mask = I2C_IC_INTR_MASK_M_RD_REQ_BITS;
    I2C_Status |= I2C_Status_Slave_Receive_Rdy;
  }
  else if (status & I2C_IC_INTR_STAT_R_RD_REQ_BITS)
  {
    i2c0->hw->clr_rd_req;
    I2C_Status |= I2C_Status_Slave_Send_Rdy;
  }
}
void __not_in_flash_func(i2c1_irq_handler)(void)
{
  // Get interrupt status
  uint32_t status = i2c1->hw->intr_stat;
  // is a write request? Or a read request ? event
  if (status & I2C_IC_INTR_STAT_R_RX_FULL_BITS)
  {
    i2c1->hw->intr_mask = I2C_IC_INTR_MASK_M_RD_REQ_BITS;
    I2C2_Status |= I2C_Status_Slave_Receive_Rdy;
  }
  else if (status & I2C_IC_INTR_STAT_R_RD_REQ_BITS)
  {
    i2c1->hw->clr_rd_req;
    I2C2_Status |= I2C_Status_Slave_Send_Rdy;
  }
}
void i2cSlave(unsigned char *p)
{
  int addr;
  getcsargs(&p, 5);
  if (argc != 5)
    StandardError(2);
  if (I2C_Status & I2C_Status_Slave)
    error("Slave already open");
  addr = getinteger(argv[0]);
  ExtCfg(I2C0SDApin, EXT_COM_RESERVED, 0);
  ExtCfg(I2C0SCLpin, EXT_COM_RESERVED, 0);
  gpio_pull_up(PinDef[I2C0SDApin].GPno);
  gpio_pull_up(PinDef[I2C0SCLpin].GPno);
  i2c_init(i2c0, 400000);
  I2C_Slave_Addr = addr;
  I2C_Slave_Send_IntLine = (char *)GetIntAddress(argv[2]);    // get the interrupt routine's location
  I2C_Slave_Receive_IntLine = (char *)GetIntAddress(argv[4]); // get the interrupt routine's location
  InterruptUsed = true;
  i2c_set_slave_mode(i2c0, true, I2C_Slave_Addr);
  // Enable the I2C interrupts we want to process
  i2c0->hw->intr_mask = I2C_IC_INTR_STAT_R_RX_FULL_BITS | I2C_IC_INTR_MASK_M_RD_REQ_BITS;

  // Set up the interrupt handler to service I2C interrupts
  irq_set_exclusive_handler(I2C0_IRQ, i2c0_irq_handler);
  irq_set_enabled(I2C0_IRQ, true);
  I2C_Status = I2C_Status_Slave;
}
void i2c2Slave(unsigned char *p)
{
  int addr;
  getcsargs(&p, 5);
  if (argc != 5)
    StandardError(2);
  if (I2C2_Status & I2C_Status_Slave)
    error("Slave already open");
  addr = getinteger(argv[0]);
  ExtCfg(I2C1SDApin, EXT_COM_RESERVED, 0);
  ExtCfg(I2C1SCLpin, EXT_COM_RESERVED, 0);
  gpio_pull_up(PinDef[I2C1SDApin].GPno);
  gpio_pull_up(PinDef[I2C1SCLpin].GPno);
  i2c_init(i2c1, 400000);
  I2C2_Slave_Addr = addr;
  I2C2_Slave_Send_IntLine = (char *)GetIntAddress(argv[2]);    // get the interrupt routine's location
  I2C2_Slave_Receive_IntLine = (char *)GetIntAddress(argv[4]); // get the interrupt routine's location
  InterruptUsed = true;
  i2c_set_slave_mode(i2c1, true, I2C2_Slave_Addr);
  // Enable the I2C interrupts we want to process
  i2c1->hw->intr_mask = I2C_IC_INTR_STAT_R_RX_FULL_BITS | I2C_IC_INTR_MASK_M_RD_REQ_BITS;

  // Set up the interrupt handler to service I2C interrupts
  irq_set_exclusive_handler(I2C1_IRQ, i2c1_irq_handler);
  irq_set_enabled(I2C1_IRQ, true);
  I2C2_Status = I2C_Status_Slave;
}
int DoRtcI2C(int addr, unsigned char *buff)
{
  int status;
  if (I2C0locked)
  {
    I2C_Addr = addr;                        // address of the device
    status = i2c_masterCommand(1, buff, 0); // Background operation - don't update mmI2Cvalue
  }
  else
  {
    I2C2_Addr = addr;                        // address of the device
    status = i2c2_masterCommand(1, buff, 0); // Background operation - don't update mmI2Cvalue
  }
  return !status;
}

/**
 * @brief Attempt to recover a stuck I2C bus
 *
 * This function attempts to recover an I2C bus that is stuck due to a slave
 * holding SDA or SCL low (e.g., after a timeout during clock stretching).
 *
 * Recovery procedure:
 * 1. Disable I2C peripheral
 * 2. Configure SCL as GPIO output, SDA as GPIO input
 * 3. Clock out up to 9 SCL pulses until SDA goes high
 * 4. Generate a STOP condition manually
 * 5. Re-initialize I2C peripheral
 *
 * @param i2c_num 0 for I2C0, 1 for I2C1
 * @param baudrate Baudrate to restore (100000, 400000, or 1000000)
 * @return 1 if recovery successful (SDA released), 0 if bus still stuck
 */
int i2c_bus_recovery(int i2c_num, int baudrate)
{
  uint8_t sda_pin, scl_pin;
  uint8_t sda_gpio, scl_gpio;
  i2c_inst_t *i2c;

  // Get the correct pins and I2C instance
  if (i2c_num == 0)
  {
    sda_pin = I2C0SDApin;
    scl_pin = I2C0SCLpin;
    i2c = i2c0;
  }
  else
  {
    sda_pin = I2C1SDApin;
    scl_pin = I2C1SCLpin;
    i2c = i2c1;
  }

  // Get actual GPIO numbers
  sda_gpio = PinDef[sda_pin].GPno;
  scl_gpio = PinDef[scl_pin].GPno;

  // Step 1: Disable I2C peripheral
  i2c->hw->enable = 0;

  // Step 2: Configure pins as GPIO
  gpio_init(scl_gpio);
  gpio_init(sda_gpio);

  // SCL as output (high), SDA as input with pull-up
  gpio_set_dir(scl_gpio, GPIO_OUT);
  gpio_put(scl_gpio, 1);

  gpio_set_dir(sda_gpio, GPIO_IN);
  gpio_pull_up(sda_gpio);

  // Brief delay to let pull-up take effect
  uSec(10);

  // Check if SCL is being held low (clock stretching)
  // If so, we can't do much - the slave is in control
  if (!gpio_get(scl_gpio))
  {
    // SCL is stuck low - can't recover via clocking
    // Just restore I2C and report failure
    gpio_set_function(sda_gpio, GPIO_FUNC_I2C);
    gpio_set_function(scl_gpio, GPIO_FUNC_I2C);
    gpio_pull_up(sda_gpio);
    gpio_pull_up(scl_gpio);
    i2c_init(i2c, baudrate);
    return 0;
  }

  // Step 3: Clock out up to 9 SCL pulses until SDA goes high
  // A stuck slave is mid-byte; clocking SCL lets it finish
  int recovered = 0;
  for (int i = 0; i < 9; i++)
  {
    if (gpio_get(sda_gpio))
    {
      // SDA is high - slave has released the bus
      recovered = 1;
      break;
    }
    // Clock pulse: high -> low -> high
    gpio_put(scl_gpio, 0);
    uSec(5); // Half period at ~100KHz
    gpio_put(scl_gpio, 1);
    uSec(5);
  }

  // Step 4: Generate STOP condition (SDA low->high while SCL high)
  // First ensure SCL is high
  gpio_put(scl_gpio, 1);
  uSec(5);

  // Configure SDA as output
  gpio_set_dir(sda_gpio, GPIO_OUT);

  // SDA low
  gpio_put(sda_gpio, 0);
  uSec(5);

  // SCL high (already high, but ensure it)
  gpio_put(scl_gpio, 1);
  uSec(5);

  // SDA high while SCL high = STOP condition
  gpio_put(sda_gpio, 1);
  uSec(5);

  // Step 5: Re-initialize I2C peripheral
  gpio_set_function(sda_gpio, GPIO_FUNC_I2C);
  gpio_set_function(scl_gpio, GPIO_FUNC_I2C);
  gpio_pull_up(sda_gpio);
  gpio_pull_up(scl_gpio);
  i2c_init(i2c, baudrate);

  return recovered;
}

#ifndef USBKEYBOARD
void CheckI2CKeyboard(int noerror, int read)
{
  uint16_t buff;
  //	int readover=0;
  static int ctrlheld = 0;

  //	while(readover==0){
  if (I2C0locked)
  {
    if (read == 0)
    {
      I2C_Sendlen = 1; // send one byte
      I2C_Rcvlen = 0;
      I2C_Status = 0;
      I2C_Send_Buffer[0] = 9; // the first register to read
      if (!(DoRtcI2C(0x1F, NULL)))
        goto i2c_error_exit;
    }
    else
    {
      I2C_Rcvbuf_String = (char *)&buff; // we want a string of bytes
      I2C_Rcvbuf_Float = NULL;
      I2C_Rcvbuf_Int = NULL;
      I2C_Rcvlen = 2; // get 7 bytes
      I2C_Sendlen = 0;
      if (!DoRtcI2C(0x1F, (unsigned char *)&buff))
        goto i2c_error_exit;
    }
  }
  else
  {
    I2C2_Sendlen = 1; // send one byte
    I2C2_Rcvlen = 0;
    I2C2_Status = 0;
    I2C_Send_Buffer[0] = 9; // the first register to read
    if (!(DoRtcI2C(0x1F, NULL)))
      goto i2c_error_exit;
    I2C2_Rcvbuf_String = (char *)&buff; // we want a string of bytes
    I2C2_Rcvbuf_Float = NULL;
    I2C2_Rcvbuf_Int = NULL;
    I2C2_Rcvlen = 2; // get 7 bytes
    I2C2_Sendlen = 0;
    if (!DoRtcI2C(0x1F, (unsigned char *)&buff))
      goto i2c_error_exit;
  }
  uSec(1000);
  if (buff)
  {
    if (buff == 0x1203)
      ctrlheld = 0;
    else if (buff == 0x1202)
    {
      ctrlheld = 1;
    }
    else if ((buff & 0xff) == 1)
    {
      int c = buff >> 8;
      if (c == 6)
        c = ESC;
      if (c == 0x11)
        c = F1;
      if (c == 5)
        c = F2;
      if (c == 0x7)
        c = F4;
      if (c >= 'a' && c <= 'z' && ctrlheld)
        c = c - 'a' + 1;
      if (c == BreakKey)
      {                                      // if the user wants to stop the progran
        MMAbort = true;                      // set the flag for the interpreter to see
        ConsoleRxBufHead = ConsoleRxBufTail; // empty the buffer
        // break;
      }
      else
      {
        ConsoleRxBuf[ConsoleRxBufHead] = c; // store the byte in the ring buffer
        if (ConsoleRxBuf[ConsoleRxBufHead] == keyselect && KeyInterrupt != NULL)
        {
          Keycomplete = true;
        }
        else
        {
          ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE; // advance the head of the queue
          if (ConsoleRxBufHead == ConsoleRxBufTail)
          {                                                                  // if the buffer has overflowed
            ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
          }
        }
      }
    }
    //		} else readover=1;
  }
  return;
i2c_error_exit:
  if (noerror)
  {
    noI2C = 1;
    return;
  }
  if (CurrentLinePtr)
    error("I2C Keyboard not responding");
  if (Option.KeyboardConfig == CONFIG_I2C)
  {
    MMPrintString("I2C Keyboard not responding");
    MMPrintString("\r\n");
  }
}
#endif

// ============== RTC Type Abstraction ==============
typedef enum
{
  RTC_NONE = 0,
  RTC_DS1307,  // Also DS3231, address 0x68
  RTC_PCF8563, // Address 0x51
  RTC_RV3028   // Address 0x52
} RtcType;

typedef struct
{
  RtcType type;
  uint8_t address;
  uint8_t reg_start;   // First register to read for time
  uint8_t reg_seconds; // Offset within read buffer for seconds
  uint8_t reg_minutes;
  uint8_t reg_hours;
  uint8_t reg_day;
  uint8_t reg_month;
  uint8_t reg_year;
  uint8_t read_count; // Number of bytes to read
} RtcConfig;

static const RtcConfig rtc_configs[] = {
    // DS1307/DS3231: Start at reg 0, read 7 bytes
    // Layout: [0]=sec, [1]=min, [2]=hr, [3]=dow, [4]=day, [5]=mon, [6]=yr
    {RTC_DS1307, 0x68, 0, 0, 1, 2, 4, 5, 6, 7},

    // PCF8563: Start at reg 2, read 7 bytes
    // Layout: [0]=sec, [1]=min, [2]=hr, [3]=day, [4]=dow, [5]=mon, [6]=yr
    {RTC_PCF8563, 0x51, 2, 0, 1, 2, 3, 5, 6, 7},

    // RV3028: Start at reg 0, read 7 bytes
    // Layout: [0]=sec, [1]=min, [2]=hr, [3]=dow, [4]=day, [5]=mon, [6]=yr
    {RTC_RV3028, 0x52, 0, 0, 1, 2, 4, 5, 6, 7},
};

static RtcType detected_rtc = RTC_NONE;
static const RtcConfig *active_rtc = NULL;

// Helper to convert BCD to decimal
static inline int BcdToDec(uint8_t bcd)
{
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// Helper to convert decimal to BCD
static inline uint8_t DecToBcd(int dec)
{
  return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

// Probe for an RTC at the given address
static int RtcProbe(uint8_t address)
{
  if (I2C0locked)
  {
    I2C_Sendlen = 1;
    I2C_Rcvlen = 0;
    I2C_Status = 0;
    I2C_Send_Buffer[0] = 0;
  }
  else
  {
    I2C2_Sendlen = 1;
    I2C2_Rcvlen = 0;
    I2C2_Status = 0;
    I2C_Send_Buffer[0] = 0;
  }
  return DoRtcI2C(address, NULL);
}

// Detect which RTC is connected
static int RtcDetect(void)
{
  for (int i = 0; i < (int)(sizeof(rtc_configs) / sizeof(rtc_configs[0])); i++)
  {
    if (RtcProbe(rtc_configs[i].address))
    {
      detected_rtc = rtc_configs[i].type;
      active_rtc = &rtc_configs[i];
      return 1;
    }
  }
  detected_rtc = RTC_NONE;
  active_rtc = NULL;
  return 0;
}

// Read time registers from active RTC into buffer
static int RtcReadTime(char *buff)
{
  if (!active_rtc)
    return 0;

  // Set register pointer
  if (I2C0locked)
  {
    I2C_Sendlen = 1;
    I2C_Rcvlen = 0;
    I2C_Status = 0;
    I2C_Send_Buffer[0] = active_rtc->reg_start;
  }
  else
  {
    I2C2_Sendlen = 1;
    I2C2_Rcvlen = 0;
    I2C2_Status = 0;
    I2C_Send_Buffer[0] = active_rtc->reg_start;
  }
  if (!DoRtcI2C(active_rtc->address, NULL))
    return 0;

  // Read time registers
  if (I2C0locked)
  {
    I2C_Rcvbuf_String = buff;
    I2C_Rcvbuf_Float = NULL;
    I2C_Rcvbuf_Int = NULL;
    I2C_Rcvlen = active_rtc->read_count;
    I2C_Sendlen = 0;
  }
  else
  {
    I2C2_Rcvbuf_String = buff;
    I2C2_Rcvbuf_Float = NULL;
    I2C2_Rcvbuf_Int = NULL;
    I2C2_Rcvlen = active_rtc->read_count;
    I2C2_Sendlen = 0;
  }
  return DoRtcI2C(active_rtc->address, (unsigned char *)buff);
}

// Decode time from raw buffer using active RTC config
static void RtcDecodeTime(char *buff, int *year, int *month, int *day,
                          int *hour, int *minute, int *second)
{
  const RtcConfig *cfg = active_rtc;
  *second = BcdToDec(buff[cfg->reg_seconds] & 0x7F);
  *minute = BcdToDec(buff[cfg->reg_minutes] & 0x7F);
  *hour = BcdToDec(buff[cfg->reg_hours] & 0x3F);
  *day = BcdToDec(buff[cfg->reg_day] & 0x3F);
  *month = BcdToDec(buff[cfg->reg_month] & 0x1F);
  *year = BcdToDec(buff[cfg->reg_year]) + 2000;
}

// Write time to active RTC
static int RtcWriteTime(int year, int month, int day, int hour, int minute, int second)
{
  if (!active_rtc)
    return 0;

  uint8_t yr = DecToBcd(year % 100);
  uint8_t mo = DecToBcd(month);
  uint8_t dy = DecToBcd(day);
  uint8_t hr = DecToBcd(hour);
  uint8_t mi = DecToBcd(minute);
  uint8_t se = DecToBcd(second);

  if (active_rtc->type == RTC_DS1307)
  {
    // DS1307/DS3231: write 8 bytes starting at reg 0
    I2C_Send_Buffer[0] = 0;  // Register pointer
    I2C_Send_Buffer[1] = se; // Seconds
    I2C_Send_Buffer[2] = mi; // Minutes
    I2C_Send_Buffer[3] = hr; // Hours
    I2C_Send_Buffer[4] = 1;  // Day of week (dummy)
    I2C_Send_Buffer[5] = dy; // Day
    I2C_Send_Buffer[6] = mo; // Month
    I2C_Send_Buffer[7] = yr; // Year
    if (I2C0locked)
    {
      I2C_Sendlen = 8;
      I2C_Rcvlen = 0;
      I2C_Status = 0;
    }
    else
    {
      I2C2_Sendlen = 8;
      I2C2_Rcvlen = 0;
      I2C2_Status = 0;
    }
  }
  else if (active_rtc->type == RTC_PCF8563)
  {
    // PCF8563: write 9 bytes starting at reg 0
    I2C_Send_Buffer[0] = 0;  // Register pointer
    I2C_Send_Buffer[1] = 0;  // Control 1
    I2C_Send_Buffer[2] = 0;  // Control 2
    I2C_Send_Buffer[3] = se; // Seconds
    I2C_Send_Buffer[4] = mi; // Minutes
    I2C_Send_Buffer[5] = hr; // Hours
    I2C_Send_Buffer[6] = dy; // Day
    I2C_Send_Buffer[7] = 1;  // Weekday (dummy)
    I2C_Send_Buffer[8] = mo; // Month
    I2C_Send_Buffer[9] = yr; // Year
    if (I2C0locked)
    {
      I2C_Sendlen = 10;
      I2C_Rcvlen = 0;
      I2C_Status = 0;
    }
    else
    {
      I2C2_Sendlen = 10;
      I2C2_Rcvlen = 0;
      I2C2_Status = 0;
    }
  }
  else if (active_rtc->type == RTC_RV3028)
  {
    // RV3028: write 8 bytes starting at reg 0
    I2C_Send_Buffer[0] = 0;  // Register pointer
    I2C_Send_Buffer[1] = se; // Seconds
    I2C_Send_Buffer[2] = mi; // Minutes
    I2C_Send_Buffer[3] = hr; // Hours
    I2C_Send_Buffer[4] = 1;  // Weekday (dummy)
    I2C_Send_Buffer[5] = dy; // Day
    I2C_Send_Buffer[6] = mo; // Month
    I2C_Send_Buffer[7] = yr; // Year
    if (I2C0locked)
    {
      I2C_Sendlen = 8;
      I2C_Rcvlen = 0;
      I2C_Status = 0;
    }
    else
    {
      I2C2_Sendlen = 8;
      I2C2_Rcvlen = 0;
      I2C2_Status = 0;
    }
  }

  return DoRtcI2C(active_rtc->address, NULL);
}

// Read a single register from active RTC
static int RtcReadRegister(uint8_t reg, uint8_t *value)
{
  if (!active_rtc)
    return 0;

  if (I2C0locked)
  {
    I2C_Sendlen = 1;
    I2C_Rcvlen = 0;
    I2C_Status = 0;
    I2C_Send_Buffer[0] = reg;
  }
  else
  {
    I2C2_Sendlen = 1;
    I2C2_Rcvlen = 0;
    I2C2_Status = 0;
    I2C_Send_Buffer[0] = reg;
  }
  if (!DoRtcI2C(active_rtc->address, NULL))
    return 0;

  char buff[1];
  if (I2C0locked)
  {
    I2C_Rcvbuf_String = buff;
    I2C_Rcvbuf_Float = NULL;
    I2C_Rcvbuf_Int = NULL;
    I2C_Rcvlen = 1;
    I2C_Sendlen = 0;
  }
  else
  {
    I2C2_Rcvbuf_String = buff;
    I2C2_Rcvbuf_Float = NULL;
    I2C2_Rcvbuf_Int = NULL;
    I2C2_Rcvlen = 1;
    I2C2_Sendlen = 0;
  }
  if (!DoRtcI2C(active_rtc->address, (unsigned char *)buff))
    return 0;
  *value = buff[0];
  return 1;
}

// Write a single register to active RTC
static int RtcWriteRegister(uint8_t reg, uint8_t value)
{
  if (!active_rtc)
    return 0;

  I2C_Send_Buffer[0] = reg;
  I2C_Send_Buffer[1] = value;
  if (I2C0locked)
  {
    I2C_Sendlen = 2;
    I2C_Rcvlen = 0;
    I2C_Status = 0;
  }
  else
  {
    I2C2_Sendlen = 2;
    I2C2_Rcvlen = 0;
    I2C2_Status = 0;
  }

  return DoRtcI2C(active_rtc->address, NULL);
}

// ============== End RTC Abstraction ==============

void RtcGetTime(int noerror)
{
  char *buff = GetTempStrMemory();
  clocktimer = (1000 * 60 * 60);

  // Detect which RTC is connected
  if (!RtcDetect())
  {
    goto error_exit;
  }

  // Read time registers
  if (!RtcReadTime(buff))
  {
    goto error_exit;
  }

  // Decode time using active RTC configuration
  int year, month, day, hour, minute, second;
  RtcDecodeTime(buff, &year, &month, &day, &hour, &minute, &second);

  TimeOffsetToUptime = get_epoch(year, month, day, hour, minute, second) - time_us_64() / 1000000;
  return;

error_exit:
  if (noerror)
  {
    noRTC = 1;
    return;
  }
  if (CurrentLinePtr)
    StandardError(30);
  if (Option.RTC)
  {
    MMPrintString("RTC not responding");
    MMPrintString("\r\n");
  }
}
// universal function to send/receive data to/from the RTC
// addr is the I2C address WITHOUT the read/write bit
char CvtToBCD(unsigned char *p, int min, int max)
{
  long long int t;
  t = getint(p, min, max) % 100;
  return ((t / 10) << 4) | (t % 10);
}

char CvtCharsToBCD(unsigned char *p, int min, int max)
{
  int t;
  t = (p[0] - '0') * 10 + (p[1] - '0');
  //    dp("|%c|  |%c|  %d   %d   %d", p[0], p[1], t, min, max);
  if (!isdigit(p[0]) || !isdigit(p[1]) || t < min || t > max)
    error("Date/time format");
  return ((t / 10) << 4) | (t % 10);
}

/*  @endcond */
void MIPS16 cmd_rtc(void)
{
  unsigned char *p;
  void *ptr = NULL;

  if (!(I2C0locked || I2C1locked))
    StandardError(44);

  if (checkstring(cmdline, (unsigned char *)"GETTIME"))
  {
    int repeat = 5;
    noRTC = 0;
    while (1)
    {
      while (!(classicread == 0 && nunchuckread == 0))
      {
        routinechecks();
      }
      RtcGetTime(1);
      if (noRTC == 0)
        break;
      repeat--;
      if (!repeat)
        break;
    }
    if (noRTC)
    {
      if (CurrentLinePtr)
        StandardError(30);
      if (Option.RTC)
      {
        MMPrintString("RTC not responding");
        MMPrintString("\r\n");
      }
    }
    return;
  }

  if ((p = checkstring(cmdline, (unsigned char *)"SETTIME")) != NULL)
  {
    int Fulldate = 0;
    int year, month, day, hour, minute, second;

    getcsargs(&p, 11);

    // Parse the time arguments (same for both I2C buses)
    if (argc == 1)
    {
      // single argument - assume the data is in DATETIME2 format: DD/MM/YY HH:MM:SS or DD/MM/YYYY HH:MM:SS
      p = getCstring(argv[0]);
      if (!(p[2] == '/' || p[2] == '-') || !(p[11] == ':' || p[13] == ':'))
        error("Date/time format");
      if (p[13] == ':')
        Fulldate = 2;

      // Parse date part
      day = (p[0] - '0') * 10 + (p[1] - '0');
      month = (p[3] - '0') * 10 + (p[4] - '0');
      year = (p[6 + Fulldate] - '0') * 10 + (p[7 + Fulldate] - '0');

      // Parse time part
      hour = (p[9 + Fulldate] - '0') * 10 + (p[10 + Fulldate] - '0');
      minute = (p[12 + Fulldate] - '0') * 10 + (p[13 + Fulldate] - '0');
      if (p[14 + Fulldate] == ':')
        second = (p[15 + Fulldate] - '0') * 10 + (p[16 + Fulldate] - '0');
      else
        second = 0;
    }
    else
    {
      // multiple arguments - data should be in the original yy, mm, dd, etc format
      if (argc != 11)
        StandardError(2);
      year = getint(argv[0], 0, 2099) % 100;
      month = getint(argv[2], 1, 12);
      day = getint(argv[4], 1, 31);
      hour = getint(argv[6], 0, 23);
      minute = getint(argv[8], 0, 59);
      second = getint(argv[10], 0, 59);
    }

    // Detect RTC and write time
    if (!RtcDetect())
      StandardError(30);

    if (!RtcWriteTime(year, month, day, hour, minute, second))
      StandardError(30);

    RtcGetTime(0);
    return;
  }

  if ((p = checkstring(cmdline, (unsigned char *)"GETREG")) != NULL)
  {
    getcsargs(&p, 3);
    if (argc != 3)
      StandardError(2);

    ptr = findvar(argv[2], V_FIND);
    if (g_vartbl[g_VarIndex].type & T_CONST)
      StandardError(22);
    if (g_vartbl[g_VarIndex].type & T_STR)
      StandardError(6);

    // Detect RTC
    if (!RtcDetect())
      StandardError(30);

    // Read register
    uint8_t reg = getint(argv[0], 0, 255);
    uint8_t value = 0;
    if (!RtcReadRegister(reg, &value))
      StandardError(30);

    if (g_vartbl[g_VarIndex].type & T_NBR)
      *(MMFLOAT *)ptr = value;
    else
      *(long long int *)ptr = value;
    return;
  }

  if ((p = checkstring(cmdline, (unsigned char *)"SETREG")) != NULL)
  {
    getcsargs(&p, 3);
    if (argc != 3)
      StandardError(2);

    // Detect RTC
    if (!RtcDetect())
      StandardError(30);

    // Write register
    uint8_t reg = getint(argv[0], 0, 255);
    uint8_t value = getint(argv[2], 0, 255);
    if (!RtcWriteRegister(reg, value))
      StandardError(30);
    return;
  }

  StandardError(36);
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

// enable the I2C1 module - master mode
void i2cEnable(unsigned char *p)
{
  int speed, timeout;
  getcsargs(&p, 3);
  if (argc != 3)
    SyntaxError();
  speed = getinteger(argv[0]);
  if (!(speed == 100 || speed == 400 || speed == 1000))
    error("Valid speeds 100, 400, 1000");
  timeout = getinteger(argv[2]);
  if (timeout < 0 || (timeout > 0 && timeout < 100))
    StandardError(21);
  if (I2C_enabled || I2C_Status & I2C_Status_Slave)
    error("I2C already OPEN");
  I2C_Timeout = timeout;
  i2c_enable(speed);
}
// enable the I2C1 module - master mode
void i2c2Enable(unsigned char *p)
{
  int speed, timeout;
  getcsargs(&p, 3);
  if (argc != 3)
    SyntaxError();
  speed = getinteger(argv[0]);
  if (!(speed == 100 || speed == 400 || speed == 1000))
    error("Valid speeds 100, 400, 1000");
  timeout = getinteger(argv[2]);
  if (timeout < 0 || (timeout > 0 && timeout < 100))
    StandardError(21);
  if (I2C2_enabled || I2C2_Status & I2C_Status_Slave)
    error("I2C already OPEN");
  I2C2_Timeout = timeout;
  i2c2_enable(speed);
}

// disable the I2C1 module - master mode
void i2cDisable(unsigned char *p)
{
  if (!I2C0locked)
    i2c_disable();
  else
    error("Allocated to System I2C");
}

// disable the I2C1 module - master mode
void i2c2Disable(unsigned char *p)
{
  if (!I2C1locked)
    i2c2_disable();
  else
    error("Allocated to System I2C");
}

// send data to an I2C slave - master mode
void i2cSend(unsigned char *p)
{
  int addr, i2c_options, sendlen, i;
  unsigned int buf[256];

  getcsargs(&p, MAX_ARG_COUNT);
  if (!(argc & 0x01) || (argc < 7))
    SyntaxError();
  if (!I2C_enabled)
    error("I2C not open");
  addr = getinteger(argv[0]);
  i2c_options = getinteger(argv[2]);
  if (i2c_options < 0 || i2c_options > 3)
    StandardError(21);
  I2C_Status = 0;
  if (i2c_options & 0x01)
    I2C_Status = I2C_Status_BusHold;
  I2C_Addr = addr;
  sendlen = getint(argv[4], 1, 256);

  GetCommsTxData(argv, argc, 6, sendlen, buf);
  for (i = 0; i < sendlen; i++)
    I2C_Send_Buffer[i] = (unsigned char)buf[i];

  I2C_Sendlen = sendlen;
  I2C_Rcvlen = 0;

  i2c_masterCommand(1, NULL, 1); // Foreground - update mmI2Cvalue
}
// send data to an I2C slave - master mode
void i2cSendSlave(unsigned char *p, int channel)
{
  int sendlen, i;
  unsigned int buf[256];
  getcsargs(&p, MAX_ARG_COUNT);
  if (!(argc & 0x01) || (argc < 3))
    SyntaxError();
  if (!((I2C_Status & I2C_Status_Slave && channel == 0) || (I2C2_Status & I2C_Status_Slave && channel == 1)))
    error("I2C slave not open");
  unsigned char *bbuff = I2C_Send_Buffer;
  sendlen = getinteger(argv[0]);
  if (sendlen < 1 || sendlen > 255)
    StandardError(21);

  GetCommsTxData(argv, argc, 2, sendlen, buf);
  for (i = 0; i < sendlen; i++)
    bbuff[i] = (unsigned char)buf[i];

  i2c_inst_t *i2c = (channel == 0) ? i2c0 : i2c1;
  // Clear any stale STOP / TX_ABRT from a previous transaction so we don't bail out immediately.
  (void)i2c->hw->clr_stop_det;
  (void)i2c->hw->clr_tx_abrt;
  I2CTimer = 0;
  int sent = 0;
  while (sent < sendlen && I2CTimer < 100)
  {
    // Master has ended the read (STOP) or NACKed (TX_ABRT) -> stop feeding the FIFO.
    if (i2c->hw->raw_intr_stat & (I2C_IC_RAW_INTR_STAT_STOP_DET_BITS | I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS))
      break;
    if (i2c->hw->status & I2C_IC_STATUS_TFNF_BITS)
      i2c->hw->data_cmd = bbuff[sent++];
  }
  (void)i2c->hw->clr_stop_det;
  (void)i2c->hw->clr_tx_abrt;
}
// send data to an I2C slave - master mode
void i2c2Send(unsigned char *p)
{
  int addr, i2c2_options, sendlen, i;
  unsigned int buf[256];

  getcsargs(&p, MAX_ARG_COUNT);
  if (!(argc & 0x01) || (argc < 7))
    SyntaxError();
  if (!I2C2_enabled)
    error("I2C not open");
  addr = getinteger(argv[0]);
  i2c2_options = getinteger(argv[2]);
  if (i2c2_options < 0 || i2c2_options > 3)
    StandardError(21);
  I2C2_Status = 0;
  if (i2c2_options & 0x01)
    I2C2_Status = I2C_Status_BusHold;
  I2C2_Addr = addr;
  sendlen = getint(argv[4], 1, 256);

  GetCommsTxData(argv, argc, 6, sendlen, buf);
  for (i = 0; i < sendlen; i++)
    I2C_Send_Buffer[i] = (unsigned char)buf[i];

  I2C2_Sendlen = sendlen;
  I2C2_Rcvlen = 0;

  i2c2_masterCommand(1, NULL, 1); // Foreground - update mmI2Cvalue
}

void i2cCheck(unsigned char *p)
{
  int addr;
  uint8_t rxdata;
  getcsargs(&p, 1);
  if (!I2C_enabled)
    error("I2C not open");
  addr = getinteger(argv[0]);
  if (addr < 0 || addr > 0x7F)
    error("Invalid I2C address");
  //	int ret=i2c_read_blocking(i2c0, addr, &rxdata, 1, false);
  int i2cret = i2c_read_timeout_us(i2c0, addr, &rxdata, 1, false, 1000);
  mmI2Cvalue = 0;
  if (i2cret == PICO_ERROR_GENERIC)
    mmI2Cvalue = 1;
  if (i2cret == PICO_ERROR_TIMEOUT)
    mmI2Cvalue = 2;
}
void i2c2Check(unsigned char *p)
{
  int addr;
  uint8_t rxdata;
  getcsargs(&p, 1);
  if (!I2C2_enabled)
    error("I2C not open");
  addr = getinteger(argv[0]);
  if (addr < 0 || addr > 0x7F)
    error("Invalid I2C address");
  //	int ret=i2c_read_blocking(i2c1, addr, &rxdata, 1, false);
  int i2cret = i2c_read_timeout_us(i2c1, addr, &rxdata, 1, false, 1000);
  mmI2Cvalue = 0;
  if (i2cret == PICO_ERROR_GENERIC)
    mmI2Cvalue = 1;
  if (i2cret == PICO_ERROR_TIMEOUT)
    mmI2Cvalue = 2;
}
// receive data from an I2C slave - master mode
void i2cReceive(unsigned char *p)
{
  int addr, i2c_options, rcvlen, i;
  CommsRxDest dest;
  getcsargs(&p, MAX_ARG_COUNT);
  if (!(argc & 0x01) || (argc < 7))
    SyntaxError();
  if (!I2C_enabled)
    error("I2C not open");
  addr = getinteger(argv[0]);
  i2c_options = getint(argv[2], 0, 1);
  I2C_Status = 0;
  I2C_Rcvbuf_Float = NULL;
  I2C_Rcvbuf_Int = NULL;
  I2C_Rcvbuf_String = NULL;
  if (i2c_options & 0x01)
    I2C_Status = I2C_Status_BusHold;
  I2C_Addr = addr;
  rcvlen = getinteger(argv[4]);
  if (rcvlen < 1)
    StandardError(21);
  GetCommsRxDest(argv, argc, 6, rcvlen, &dest);
  I2C_Rcvlen = rcvlen;
  I2C_Sendlen = 0;
  // Receive the raw bytes into a temp buffer (the static I2C_Rcvbuf_* pointers are left NULL so
  // i2c_masterCommand fills only the supplied buffer), then distribute via PutCommsRxData.
  unsigned char *raw = GetTempMainMemory(rcvlen);
  unsigned int *buf = GetTempMainMemory(rcvlen * sizeof(unsigned int));
  i2c_masterCommand(1, raw, 1); // Foreground - update mmI2Cvalue
  for (i = 0; i < rcvlen; i++)
    buf[i] = raw[i];
  PutCommsRxData(&dest, buf);
}
void i2cReceiveSlave(unsigned char *p, int channel)
{
  int rcvlen, count = 0, i;
  void *ptr = NULL;
  MMFLOAT *rcvdlenFloat = NULL;
  long long int *rcvdlenInt = NULL;
  CommsRxDest dest;
  getcsargs(&p, 5);
  if (argc != 5)
    SyntaxError();
  if (!((I2C_Status & I2C_Status_Slave && channel == 0) || (I2C2_Status & I2C_Status_Slave && channel == 1)))
    error("I2C slave not open");
  rcvlen = getinteger(argv[0]);
  if (rcvlen < 1 || rcvlen > 255)
    StandardError(21);
  // Validate the single receive buffer at argv[2]. Pass argc==3 so the buffer is treated as a
  // single destination (the trailing rcvdlen output at argv[4] is resolved separately below).
  GetCommsRxDest(argv, 3, 2, rcvlen, &dest);
  ptr = findvar(argv[4], V_FIND);
  if (g_vartbl[g_VarIndex].type & T_CONST)
    StandardError(22);
  if (g_vartbl[g_VarIndex].type & T_NBR)
    rcvdlenFloat = (MMFLOAT *)ptr;
  else if (g_vartbl[g_VarIndex].type & T_INT)
    rcvdlenInt = (long long int *)ptr;
  else
    StandardError(6);

  unsigned char *bbuff = I2C_Send_Buffer;
  i2c_inst_t *i2c = (channel == 0) ? i2c0 : i2c1;
  // Clear any stale STOP from a previous transaction so the early-exit doesn't trigger immediately.
  (void)i2c->hw->clr_stop_det;
  I2CTimer = 0;
  while (count < rcvlen && I2CTimer < 100)
  {
    if (i2c->hw->status & I2C_IC_STATUS_RFNE_BITS)
    {
      bbuff[count++] = (uint8_t)i2c->hw->data_cmd;
    }
    else if (i2c->hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS)
    {
      // Master ended the transfer; drain any bytes that landed in the FIFO before the STOP, then exit.
      while (count < rcvlen && (i2c->hw->status & I2C_IC_STATUS_RFNE_BITS))
        bbuff[count++] = (uint8_t)i2c->hw->data_cmd;
      break;
    }
  }
  (void)i2c->hw->clr_stop_det;

  // Store only the bytes actually received (count, which may be fewer than rcvlen).
  unsigned int tmp[256];
  for (i = 0; i < count; i++)
    tmp[i] = bbuff[i];
  dest.len = count;
  if (dest.kind == COMMS_RXD_STRING)
    ((char *)dest.ptr)[-1] = (char)count; // shorten the string to the number of bytes received
  PutCommsRxData(&dest, tmp);

  if (!(rcvdlenFloat == NULL))
    *rcvdlenFloat = (MMFLOAT)count;
  else
    *rcvdlenInt = (long long int)count;

  if (channel == 0)
    i2c0->hw->intr_mask = I2C_IC_INTR_STAT_R_RX_FULL_BITS | I2C_IC_INTR_MASK_M_RD_REQ_BITS;
  else
    i2c1->hw->intr_mask = I2C_IC_INTR_STAT_R_RX_FULL_BITS | I2C_IC_INTR_MASK_M_RD_REQ_BITS;
}
// receive data from an I2C slave - master mode
void i2c2Receive(unsigned char *p)
{
  int addr, i2c2_options, rcvlen, i;
  CommsRxDest dest;
  getcsargs(&p, MAX_ARG_COUNT);
  if (!(argc & 0x01) || (argc < 7))
    SyntaxError();
  if (!I2C2_enabled)
    error("I2C not open");
  addr = getinteger(argv[0]);
  i2c2_options = getint(argv[2], 0, 1);
  I2C2_Status = 0;
  if (i2c2_options & 0x01)
    I2C2_Status = I2C_Status_BusHold;
  I2C2_Addr = addr;
  I2C2_Rcvbuf_Float = NULL;
  I2C2_Rcvbuf_Int = NULL;
  I2C2_Rcvbuf_String = NULL;
  rcvlen = getinteger(argv[4]);
  if (rcvlen < 1)
    StandardError(21);
  GetCommsRxDest(argv, argc, 6, rcvlen, &dest);
  I2C2_Rcvlen = rcvlen;
  I2C2_Sendlen = 0;
  // Receive the raw bytes into a temp buffer (the static I2C2_Rcvbuf_* pointers are left NULL so
  // i2c2_masterCommand fills only the supplied buffer), then distribute via PutCommsRxData.
  unsigned char *raw = GetTempMainMemory(rcvlen);
  unsigned int *buf = GetTempMainMemory(rcvlen * sizeof(unsigned int));
  i2c2_masterCommand(1, raw, 1); // Foreground - update mmI2Cvalue
  for (i = 0; i < rcvlen; i++)
    buf[i] = raw[i];
  PutCommsRxData(&dest, buf);
}

/**************************************************************************************************
Enable the I2C1 module - master mode
***************************************************************************************************/
void i2c_enable(int bps)
{
  ExtCfg(I2C0SDApin, EXT_COM_RESERVED, 0);
  ExtCfg(I2C0SCLpin, EXT_COM_RESERVED, 0);
  i2c_init(i2c0, bps * 1000);
  gpio_pull_up(PinDef[I2C0SDApin].GPno);
  gpio_pull_up(PinDef[I2C0SCLpin].GPno);
  I2C_enabled = 1;
}
void i2c2_enable(int bps)
{
  ExtCfg(I2C1SDApin, EXT_COM_RESERVED, 0);
  ExtCfg(I2C1SCLpin, EXT_COM_RESERVED, 0);
  i2c_init(i2c1, bps * 1000);
  gpio_pull_up(PinDef[I2C1SDApin].GPno);
  gpio_pull_up(PinDef[I2C1SCLpin].GPno);
  I2C2_enabled = 1;
}

/**************************************************************************************************
Disable the I2C1 module - master mode
***************************************************************************************************/
void i2c_disable()
{
  if (I2C_Status & I2C_Status_Slave)
  {
    irq_set_enabled(I2C0_IRQ, false);
    irq_remove_handler(I2C0_IRQ, i2c0_irq_handler);
    i2c_set_slave_mode(i2c0, false, I2C_Slave_Addr);
    i2c0->hw->intr_mask = 0;
  }
  I2C_Status = I2C_Status_Disable;
  I2C_Rcvbuf_String = NULL; // pointer to the master receive buffer
  I2C_Rcvbuf_Float = NULL;
  I2C_Rcvbuf_Int = NULL;
  I2C_Sendlen = 0; // length of the master send buffer
  I2C_Rcvlen = 0;  // length of the master receive buffer
  I2C_Addr = 0;    // I2C device address
  I2C_Timeout = 0; // master timeout value
  i2c_deinit(i2c0);
  I2C_enabled = 0;
  if (I2C0SDApin != 99)
    ExtCfg(I2C0SDApin, EXT_NOT_CONFIG, 0);
  if (I2C0SCLpin != 99)
    ExtCfg(I2C0SCLpin, EXT_NOT_CONFIG, 0);
}
void i2c2_disable()
{
  if (I2C2_Status & I2C_Status_Slave)
  {
    irq_set_enabled(I2C1_IRQ, false);
    irq_remove_handler(I2C1_IRQ, i2c1_irq_handler);
    i2c_set_slave_mode(i2c1, false, I2C2_Slave_Addr);
    i2c1->hw->intr_mask = 0;
  }
  I2C2_Status = I2C_Status_Disable;
  I2C2_Rcvbuf_String = NULL; // pointer to the master receive buffer
  I2C2_Rcvbuf_Float = NULL;
  I2C2_Rcvbuf_Int = NULL;
  I2C2_Sendlen = 0; // length of the master send buffer
  I2C2_Rcvlen = 0;  // length of the master receive buffer
  I2C2_Addr = 0;    // I2C device address
  I2C2_Timeout = 0; // master timeout value
  i2c_deinit(i2c1);
  I2C2_enabled = 0;
  if (I2C1SDApin != 99)
    ExtCfg(I2C1SDApin, EXT_NOT_CONFIG, 0);
  if (I2C1SCLpin != 99)
    ExtCfg(I2C1SCLpin, EXT_NOT_CONFIG, 0);
}
/**************************************************************************************************
Send and/or Receive data - master mode
***************************************************************************************************/
int i2c_masterCommand(int timer, unsigned char *I2C_Rcv_Buffer, int update_global)
{
  //	unsigned char start_type,
  int status = 0;
  unsigned char i2caddr = I2C_Addr;
  if (I2C_Sendlen)
  {
    int i2cret = i2c_write_timeout_us(i2c0, (uint8_t)i2caddr, (uint8_t *)I2C_Send_Buffer, I2C_Sendlen, (I2C_Status == I2C_Status_BusHold ? true : false), I2C_Timeout * 1000);
    if (i2cret == PICO_ERROR_GENERIC)
      status = 1;
    if (i2cret == PICO_ERROR_TIMEOUT)
      status = 2;
  }
  if (I2C_Rcvlen)
  {
    int i2cret = i2c_read_timeout_us(i2c0, (uint8_t)i2caddr, (uint8_t *)I2C_Rcv_Buffer, I2C_Rcvlen, (I2C_Status == I2C_Status_BusHold ? true : false), I2C_Timeout * 1000);
    status = 0;
    if (i2cret == PICO_ERROR_GENERIC)
      status = 1;
    if (i2cret == PICO_ERROR_TIMEOUT)
      status = 2;
    for (int i = 0; i < I2C_Rcvlen; i++)
    {
      if (I2C_Rcvbuf_String != NULL)
      {
        *I2C_Rcvbuf_String = I2C_Rcv_Buffer[i];
        I2C_Rcvbuf_String++;
      }
      if (I2C_Rcvbuf_Float != NULL)
      {
        *I2C_Rcvbuf_Float = I2C_Rcv_Buffer[i];
        I2C_Rcvbuf_Float++;
      }
      if (I2C_Rcvbuf_Int != NULL)
      {
        *I2C_Rcvbuf_Int = I2C_Rcv_Buffer[i];
        I2C_Rcvbuf_Int++;
      }
    }
  }
  if (status)
    I2C_Status = 0; // Clear bus hold on error
  if (update_global)
    mmI2Cvalue = status;
  return status;
}

int i2c2_masterCommand(int timer, unsigned char *I2C2_Rcv_Buffer, int update_global)
{
  //	unsigned char start_type,
  int status = 0;
  unsigned char i2c2addr = I2C2_Addr;
  if (I2C2_Sendlen)
  {
    int i2cret = i2c_write_timeout_us(i2c1, (uint8_t)i2c2addr, (uint8_t *)I2C_Send_Buffer, I2C2_Sendlen, (I2C2_Status == I2C_Status_BusHold ? true : false), I2C2_Timeout * 1000);
    if (i2cret == PICO_ERROR_GENERIC)
      status = 1;
    if (i2cret == PICO_ERROR_TIMEOUT)
      status = 2;
  }
  if (I2C2_Rcvlen)
  {
    int i2cret = i2c_read_timeout_us(i2c1, (uint8_t)i2c2addr, (uint8_t *)I2C2_Rcv_Buffer, I2C2_Rcvlen, (I2C2_Status == I2C_Status_BusHold ? true : false), I2C2_Timeout * 1000);
    status = 0;
    if (i2cret == PICO_ERROR_GENERIC)
      status = 1;
    if (i2cret == PICO_ERROR_TIMEOUT)
      status = 2;
    for (int i = 0; i < I2C2_Rcvlen; i++)
    {
      if (I2C2_Rcvbuf_String != NULL)
      {
        *I2C2_Rcvbuf_String = I2C2_Rcv_Buffer[i];
        I2C2_Rcvbuf_String++;
      }
      if (I2C2_Rcvbuf_Float != NULL)
      {
        *I2C2_Rcvbuf_Float = I2C2_Rcv_Buffer[i];
        I2C2_Rcvbuf_Float++;
      }
      if (I2C2_Rcvbuf_Int != NULL)
      {
        *I2C2_Rcvbuf_Int = I2C2_Rcv_Buffer[i];
        I2C2_Rcvbuf_Int++;
      }
    }
  }
  if (status)
    I2C2_Status = 0; // Clear bus hold on error
  if (update_global)
    mmI2Cvalue = status;
  return status;
}
/*  @endcond */

void fun_mmi2c(void)
{
  iret = mmI2Cvalue;
  targ = T_INT;
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */
int GeneralSend(unsigned int addr, int nbr, char *p, int update_global)
{
  int status;

  if (I2C0locked)
  {
    I2C_Sendlen = nbr; // send one byte
    I2C_Rcvlen = 0;
    I2C_Status = 0;
    memcpy(I2C_Send_Buffer, p, nbr);
    I2C_Addr = addr; // address of the device
    status = i2c_masterCommand(1, NULL, update_global);
  }
  else
  {
    I2C2_Sendlen = nbr; // send one byte
    I2C2_Rcvlen = 0;
    I2C2_Status = 0;
    memcpy(I2C_Send_Buffer, p, nbr);
    I2C2_Addr = addr; // address of the device
    status = i2c2_masterCommand(1, NULL, update_global);
  }
  return status;
}

int GeneralReceive(unsigned int addr, int nbr, char *p, int update_global)
{
  int status;

  if (I2C0locked)
  {
    I2C_Rcvbuf_Float = NULL;
    I2C_Rcvbuf_Int = NULL;
    I2C_Rcvbuf_String = NULL;
    I2C_Sendlen = 0; // send one byte
    I2C_Rcvlen = nbr;
    I2C_Status = 0;
    I2C_Addr = addr; // address of the device
    status = i2c_masterCommand(1, (unsigned char *)p, update_global);
  }
  else
  {
    I2C2_Rcvbuf_Float = NULL;
    I2C2_Rcvbuf_Int = NULL;
    I2C2_Rcvbuf_String = NULL;
    I2C2_Sendlen = 0; // send one byte
    I2C2_Rcvlen = nbr;
    I2C2_Status = 0;
    I2C2_Addr = addr; // address of the device
    status = i2c2_masterCommand(1, (unsigned char *)p, update_global);
  }
  return status;
}
int WiiSend(int nbr, char *p)
{
  unsigned int addr = nunaddr;
  return GeneralSend(addr, nbr, p, 0); // Background operation - don't update mmI2Cvalue
}

int WiiReceive(int nbr, char *p)
{
  unsigned int addr = nunaddr;
  return GeneralReceive(addr, nbr, p, 0); // Background operation - don't update mmI2Cvalue
}

uint8_t readRegister8(unsigned int addr, uint8_t reg)
{
  uint8_t buff;
  GeneralSend(addr, 1, (char *)&reg, 1);
  GeneralReceive(addr, 1, (char *)&buff, 1);
  return buff;
}
uint32_t readRegister32(unsigned int addr, uint8_t reg)
{
  uint32_t buff;
  GeneralSend(addr, 1, (char *)&reg, 1);
  GeneralReceive(addr, 4, (char *)&buff, 1);
  return buff;
}
void WriteRegister8(unsigned int addr, uint8_t reg, uint8_t data)
{
  uint8_t buff[2];
  buff[0] = reg;
  buff[1] = data;
  GeneralSend(addr, 2, (char *)buff, 1);
}
void Write8Register16(unsigned int addr, uint16_t reg, uint8_t data)
{
  uint8_t buff[3];
  buff[0] = reg >> 8;
  buff[1] = reg & 0xFF;
  buff[2] = data;
  GeneralSend(addr, 3, (char *)buff, 1);
}
uint8_t read8Register16(unsigned int addr, uint16_t reg)
{
  uint8_t buff;
  uint8_t rbuff[2];
  rbuff[0] = reg >> 8;
  rbuff[1] = reg & 0xFF;
  if (I2C0locked)
    I2C_Status = I2C_Status_BusHold;
  else
    I2C2_Status = I2C_Status_BusHold;
  GeneralSend(addr, 2, (char *)rbuff, 1);
  if (I2C0locked)
    I2C_Status = 0;
  else
    I2C2_Status = 0;
  GeneralReceive(addr, 1, (char *)&buff, 1);
  return buff;
}
void readNRegister16(unsigned int addr, uint16_t reg, uint8_t *buff, int nbr)
{
  uint8_t rbuff[2];
  rbuff[0] = reg >> 8;
  rbuff[1] = reg & 0xFF;
  if (I2C0locked)
    I2C_Status = I2C_Status_BusHold;
  else
    I2C2_Status = I2C_Status_BusHold;
  GeneralSend(addr, 2, (char *)rbuff, 1);
  if (I2C0locked)
    I2C_Status = 0;
  else
    I2C2_Status = 0;
  GeneralReceive(addr, nbr, (char *)buff, 1);
}

void nunproc(void)
{
  static int lastc = 0, lastz = 0;
  nunstruct[5].x = nunbuff[0];
  nunstruct[5].y = nunbuff[1];
  nunstruct[5].ax = nunbuff[2] << 2;
  nunstruct[5].ay = nunbuff[3] << 2;
  nunstruct[5].az = nunbuff[4] << 2;
  nunstruct[5].Z = (~(nunbuff[5] & 1)) & 1;
  nunstruct[5].C = (~((nunbuff[5] & 2) >> 1)) & 1;
  nunstruct[5].ax += ((nunbuff[5] >> 2) & 3);
  nunstruct[5].ay += ((nunbuff[5] >> 4) & 3);
  nunstruct[5].az += ((nunbuff[5] >> 6) & 3);
  if (lastc == 0 && nunstruct[5].C)
  {
    lastc = 1;
    nunfoundc[5] = 1;
  }
  if (lastz == 0 && nunstruct[5].Z)
  {
    lastz = 1;
    nunfoundc[5] = 1;
  }
  if (nunstruct[5].C == 0)
    lastc = 0;
  if (nunstruct[5].Z == 0)
    lastz = 0;
}

/*  @endcond */
void MIPS16 cmd_Nunchuck(void)
{
  unsigned char *tp = NULL;
  uint32_t id = 0;
  if ((tp = checkstring(cmdline, (unsigned char *)"OPEN")))
  {
    getcsargs(&tp, 1);
    if (!(I2C0locked || I2C1locked))
      StandardError(44);
    if (classic1 || nunchuck1)
      StandardError(31);
    memset((void *)&nunstruct[5].x, 0, sizeof(nunstruct[5]));
    int retry = 5;
    int status;
    do
    {
      status = WiiSend(sizeof(nuninit), (char *)nuninit);
      uSec(5000);
    } while (status && retry--);
    if (status)
      error("Nunchuck not connected");
    status = WiiSend(sizeof(nuninit2), (char *)nuninit2);
    if (status)
      error("Nunchuck not connected");
    uSec(5000);
    retry = 5;
    do
    {
      status = WiiSend(sizeof(nunid), (char *)nunid);
      uSec(5000);
      status = WiiReceive(4, (char *)&id);
      uSec(5000);
    } while (status && retry--);
    if (status)
      error("Device ID not returned");
    nunstruct[5].type = swap32(id);
    if (nunstruct[5].type != 0xA4200000)
      error("Device connected is not a Nunchuck");
    uSec(5000);
    retry = 5;
    nunbuff[5] = 0;
    if (argc == 1)
    {
      nunInterruptc[5] = (char *)GetIntAddress(argv[0]); // get the interrupt location
      InterruptUsed = true;
    }
    nunchuck1 = 1;
    while (nunchuck1 == 1)
      routinechecks();
    if (nunbuff[5] == 0 || nunbuff[5] == 255)
    {
      nunchuck1 = 0;
      error("Nunchuck not responding");
    }
    nunproc();
    return;
  }
  else if ((tp = checkstring(cmdline, (unsigned char *)"CLOSE")))
  {
    if (!nunchuck1)
      error("Not open");
    nunchuck1 = 0;
    nunchuckread = false;
    WiiReceive(6, (char *)nunbuff);
    nunInterruptc[5] = NULL;
  }
  else
    SyntaxError();
  ;
}

void MIPS16 cmd_Classic(void)
{
  unsigned char *tp = NULL;
  uint32_t id = 0;
  if ((tp = checkstring(cmdline, (unsigned char *)"OPEN")))
  {
    getcsargs(&tp, 3);
    if (!(I2C0locked || I2C1locked))
      StandardError(44);
    if (classic1 || nunchuck1)
      StandardError(31);
    memset((void *)&nunstruct[0].x, 0, sizeof(nunstruct[0]));
    int retry = 5;
    int status;
    do
    {
      status = WiiSend(sizeof(nuninit), (char *)nuninit);
      uSec(5000);
    } while (status && retry--);
    if (status)
      error("Classic not connected");
    status = WiiSend(sizeof(nuninit2), (char *)nuninit2);
    if (status)
      error("Classic not connected");
    uSec(5000);
    retry = 5;
    do
    {
      status = WiiSend(sizeof(nunid), (char *)nunid);
      uSec(5000);
      status = WiiReceive(4, (char *)&id);
      uSec(5000);
    } while (status && retry--);
    if (status)
      error("Device ID not returned");
    nunstruct[0].type = swap32(id);
    if (nunstruct[0].type == 0xA4200000)
      error("Device connected is a Nunchuck");
    uSec(5000);
    if (argc >= 1)
    {
      nunInterruptc[0] = (char *)GetIntAddress(argv[0]); // get the interrupt location
      InterruptUsed = true;
      nunstruct[0].x1 = 0b111111111111111;
      if (argc == 3)
        nunstruct[0].x1 = getint(argv[2], 0, 0b111111111111111);
    }
    classic1 = 1;
    while (classic1 == 1)
      routinechecks();
    if (nunbuff[0] == 0 || nunbuff[0] == 255)
    {
      classic1 = 0;
      error("Classic not responding");
    }
    classicproc();
    return;
  }
  else if ((tp = checkstring(cmdline, (unsigned char *)"CLOSE")))
  {
    if (!classic1)
      error("Not open");
    classic1 = 0;
    classicread = false;
    WiiReceive(6, (char *)nunbuff);
    nunInterruptc[0] = NULL;
  }
  else
    SyntaxError();
  ;
}

/*
 * @cond
 * The following section will be excluded from the documentation.
 */

void classicproc(void)
{
  //	int ax; //classic left x
  //	int ay; //classic left y
  //	int az; //classic centre
  //	int Z;  //classic right x
  //	int C;  //classic right y
  //	int L;  //classic left analog
  //	int R;  //classic right analog
  //	unsigned short x0; //classic buttons
  static unsigned short buttonlast = 0;
  unsigned short inttest = (((nunbuff[4] >> 1) | (nunbuff[5] << 7)) ^ 0b111111111111111) & nunstruct[0].x1;
  nunstruct[0].classic[0] = nunbuff[0];
  nunstruct[0].classic[1] = nunbuff[1];
  nunstruct[0].classic[2] = nunbuff[2];
  nunstruct[0].classic[3] = nunbuff[3];
  nunstruct[0].classic[4] = nunbuff[4];
  nunstruct[0].classic[5] = nunbuff[5];
  if (inttest != buttonlast)
  {
    nunfoundc[0] = 1;
  }
  buttonlast = inttest;
  nunstruct[0].ax = (nunbuff[0] & 0b111111) << 2;
  nunstruct[0].ay = (nunbuff[1] & 0b111111) << 2;
  nunstruct[0].Z = (((nunbuff[2] & 0b10000000) >> 7) |
                    ((nunbuff[1] & 0b11000000) >> 5) |
                    ((nunbuff[0] & 0b11000000) >> 3))
                   << 3;
  nunstruct[0].C = (nunbuff[2] & 0b11111) << 3;
  nunstruct[0].R = ((nunbuff[3] & 0b00011111)) << 3;
  nunstruct[0].L = (((nunbuff[3] & 0b11100000) >> 5) |
                    ((nunbuff[2] & 0b01100000) >> 2))
                   << 3;
  nunstruct[0].x0 = ((nunbuff[4] >> 1) | (nunbuff[5] << 7)) ^ 0b111111111111111;
}

// Camera is available on the SPI-panel builds as before, and additionally on the
// full-fat HDMI builds (plain HDMI + HDMIUSB) which have a mode-4 320x240 RGB555
// framebuffer to render into. HDMICUTDOWN (HDMIWEB/HDMIBTH) is excluded - those
// variants have no spare command/function token slots.
#if !defined(PICOMITEVGA) || (defined(HDMI) && !defined(HDMICUTDOWN))
#if defined(rp2350) || defined(PICOMITEWEB) || defined(PICOMITE)
#define ov7670_address 0x21
#define ov2640_address 0x30
// OV2640 DVP pixel-clock divider (R_DVP_SP, DSP reg 0xD3). Higher = slower PCLK. Set so
// the bit-banged capture keeps up at the lower RP2350 CPU speeds (e.g. PicoMiteRP2350);
// used for BOTH the RGB565 preview and the JPEG capture so they always match.
#define OV2640_DVP_CLK 0x10
#define CAM_OV7670 0
#define CAM_OV2640 1
int CameraType = CAM_OV7670;             // selected by the CAMERA OPEN keyword (default OV7670)
uint8_t camera_address = ov7670_address; // SCCB/I2C address for ov7670_set/readregister
#define top 120
#define left 160
uint8_t PCLK = 0;
uint8_t XCLK = 0;
uint8_t HREF = 0;
uint8_t VSYNC = 0;
uint8_t RESET = 0;
uint8_t D0 = 0;
uint8_t XCLKGP = 0;
uint8_t PCLKGP = 0;
uint8_t VSYNCGP = 0;
uint8_t HREFGP = 0;
// Active capture geometry. Default 160x120 (QQVGA); QVGA (320x240) is RP2350-only.
int CameraWidth = 160;
int CameraHeight = 120;
// extern volatile int ExtCurrentConfig[];
void cameraclose(void)
{
  if (PCLK)
    ExtCfg(PCLK, EXT_NOT_CONFIG, 0);
  if (HREF)
    ExtCfg(HREF, EXT_NOT_CONFIG, 0);
  if (VSYNC)
    ExtCfg(VSYNC, EXT_NOT_CONFIG, 0);
  if (RESET)
    ExtCfg(RESET, EXT_NOT_CONFIG, 0);
  if (D0)
  {
    int startdata = PinDef[D0].GPno;
    for (int i = startdata; i < startdata + 8; i++)
    {
      ExtCfg(PINMAP[i], EXT_NOT_CONFIG, 0);
    }
  }
  if (XCLK)
  {
    if (CameraSlice != -1) // OV7670 drives XCLK via PWM; a self-clocked OV2640 uses pin1 as PWDN (no PWM)
      PWMoff(CameraSlice);
    ExtCfg(XCLK, EXT_NOT_CONFIG, 0);
  }
  CameraSlice = -1;
  CameraChannel = -1;
  PCLK = HREF = VSYNC = D0 = XCLK = 0;
}
int readregister(int reg)
{
  unsigned char buff[2];
  if (I2C0locked)
  {
    I2C_Sendlen = 1; // send one byte
    I2C_Rcvlen = 0;
    *I2C_Send_Buffer = reg;        // the first register to read
    I2C_Addr = camera_address;     // address of the device
    i2c_masterCommand(1, NULL, 1); // Foreground - update mmI2Cvalue
  }
  else
  {
    I2C2_Sendlen = 1; // send one byte
    I2C2_Rcvlen = 0;
    *I2C_Send_Buffer = reg;         // the first register to read
    I2C2_Addr = camera_address;     // address of the device
    i2c2_masterCommand(1, NULL, 1); // Foreground - update mmI2Cvalue
  }
  if (mmI2Cvalue)
  {
    cameraclose();
    error("I2C failure");
  }
  uSec(1000);
  if (I2C0locked)
  {
    I2C_Rcvbuf_Float = NULL;
    I2C_Rcvbuf_Int = NULL;
    I2C_Rcvlen = 1; // get 7 bytes
    I2C_Sendlen = 0;
    I2C_Addr = camera_address;     // address of the device
    i2c_masterCommand(1, buff, 1); // Foreground - update mmI2Cvalue
  }
  else
  {
    I2C2_Rcvbuf_Float = NULL;
    I2C2_Rcvbuf_Int = NULL;
    I2C2_Rcvlen = 1; // get 7 bytes
    I2C2_Sendlen = 0;
    I2C2_Addr = camera_address;     // address of the device
    i2c2_masterCommand(1, buff, 1); // Foreground - update mmI2Cvalue
  }
  uSec(1000);
  return buff[0];
}

void ov7670_set(char a, char b)
{
  // send the command
  if (I2C0locked)
  {
    I2C_Sendlen = 2; // send one byte
    I2C_Rcvlen = 0;
    I2C_Send_Buffer[0] = a;        // the first register to read
    I2C_Send_Buffer[1] = b;        // the first register to read
    I2C_Addr = camera_address;     // address of the device
    i2c_masterCommand(1, NULL, 1); // Foreground - update mmI2Cvalue
  }
  else
  {
    I2C2_Sendlen = 2; // send one byte
    I2C2_Rcvlen = 0;
    I2C_Send_Buffer[0] = a;         // the first register to read
    I2C_Send_Buffer[1] = b;         // the first register to read
    I2C2_Addr = camera_address;     // address of the device
    i2c2_masterCommand(1, NULL, 1); // Foreground - update mmI2Cvalue
  }
  if (mmI2Cvalue)
  {
    cameraclose();
    error("I2C failure");
  }
  if (a == REG_COM7 && b == COM7_RESET)
  {
    uSec(500000);
    return;
  }
  uSec(1000);
  if (readregister(a) != b)
    error("Camera Config Failure");
  uSec(1000);
  return;
}

#ifdef rp2350 // SCCB no-verify write - only the OV2640 (RP2350) needs it
// SCCB write with no read-back verify. Used for the OV2640 init: many of its
// DSP / bank-select (0xFF) / write-pointer registers do not read back the value
// written, so ov7670_set's verify would false-fail.
void sccb_write(unsigned char a, unsigned char b)
{
  if (I2C0locked)
  {
    I2C_Sendlen = 2;
    I2C_Rcvlen = 0;
    I2C_Send_Buffer[0] = a;
    I2C_Send_Buffer[1] = b;
    I2C_Addr = camera_address;
    i2c_masterCommand(1, NULL, 1);
  }
  else
  {
    I2C2_Sendlen = 2;
    I2C2_Rcvlen = 0;
    I2C_Send_Buffer[0] = a;
    I2C_Send_Buffer[1] = b;
    I2C2_Addr = camera_address;
    i2c2_masterCommand(1, NULL, 1);
  }
  if (mmI2Cvalue)
  {
    cameraclose();
    error("I2C failure");
  }
  uSec(1000);
}
#endif // rp2350 (sccb_write)

// Drive a camera register table: {reg,value} pairs ending with {0xFF,0xFF}.
// verify=1 -> ov7670_set (read-back checked, OV7670). verify=0 -> sccb_write
// (no read-back; required for the OV2640 whose DSP/bank/pointer regs don't read back).
void load_camera_regs(const OV7670_command *t, int verify)
{
  for (int i = 0; !(t[i].reg == 0xFF && t[i].value == 0xFF); i++)
  {
    if (verify)
      ov7670_set(t[i].reg, t[i].value);
#ifdef rp2350
    else
      sccb_write(t[i].reg, t[i].value); // OV2640 tables (no read-back)
#endif
  }
}

void OV7670_test_pattern(OV7670_pattern pattern)
{
  // Read current SCALING_XSC and SCALING_YSC register settings,
  // so image scaling settings aren't corrupted.
  uint8_t xsc = readregister(OV7670_REG_SCALING_XSC);
  uint8_t ysc = readregister(OV7670_REG_SCALING_YSC);
  if (pattern & 1)
  {
    xsc |= 0x80;
  }
  else
  {
    xsc &= ~0x80;
  }
  if (pattern & 2)
  {
    ysc |= 0x80;
  }
  else
  {
    ysc &= ~0x80;
  }
  // Write modified results back to SCALING_XSC and SCALING_YSC registers
  ov7670_set(OV7670_REG_SCALING_XSC, xsc);
  ov7670_set(OV7670_REG_SCALING_YSC, ysc);
}

static const OV7670_command
    OV7670_yuv[] = {
        // Manual output format, YUV, use full output range
        {
            OV7670_REG_COM7,
            OV7670_COM7_YUV},
        {OV7670_REG_COM15,
         OV7670_COM15_R00FF},
        {0xFF,
         0xFF}},
    OV7670_rgb[] = {
        // Manual output format, RGB, use RGB565 and full 0-255 output range
        {OV7670_REG_COM7, OV7670_COM7_RGB},
        {OV7670_REG_RGB444, 0},
        {OV7670_REG_COM15, OV7670_COM15_RGB565 | OV7670_COM15_R00FF},
        {0xFF, 0xFF}};

// OV7670 RGB565 register init - table driven (was ~200 inline ov7670_set calls).
// Driven with verify=1; {REG_COM7,COM7_RESET} entries get the 500ms reset delay
// inside ov7670_set. Saturation and frame size are applied separately afterwards.
static const OV7670_command OV7670_init[] = {
    {REG_COM7, COM7_RESET}, {REG_COM7, COM7_RESET}, {REG_RGB444, 0}, {REG_COM10, 0x02}, {REG_MVFP, 0x37}, {REG_COM11, 0x0A}, {REG_COM7, COM7_RGB}, {REG_COM1, 0}, {REG_COM15, COM15_RGB565}, {REG_COM9, 0x2A}, {REG_TSLB, 0x04}, {REG_COM13, 0xc8}, {REG_HSTART, 0x13}, {REG_HSTOP, 0x01}, {REG_HREF, 0xb6}, {REG_VSTART, 0x02}, {REG_VSTOP, 0x7a}, {REG_VREF, 0x0a}, {REG_COM5, 0x61}, {REG_COM6, 0x4b}, {0x16, 0x02}, {0x21, 0x02}, {0x22, 0x91}, {0x29, 0x07}, {0x33, 0x0b}, {0x35, 0x0b}, {0x37, 0x1d}, {0x38, 0x71}, {0x39, 0x2a}, {REG_COM12, 0x78}, {0x4d, 0x40}, {0x4e, 0x20}, {REG_GFIX, 0}, {0x74, 0x10}, {0x8d, 0x4f}, {0x8e, 0}, {0x8f, 0}, {0x90, 0}, {0x91, 0}, {0x96, 0}, {0x9a, 0}, {0xb0, 0x84}, {0xb1, 0x0c}, {0xb2, 0x0e}, {0xb3, 0x82}, {0xb8, 0x0a}, {0x7a, 0x20}, {0x7b, 0x10}, {0x7c, 0x1e}, {0x7d, 0x35}, {0x7e, 0x5a}, {0x7f, 0x69}, {0x80, 0x76}, {0x81, 0x80}, {0x82, 0x88}, {0x83, 0x8f}, {0x84, 0x96}, {0x85, 0xa3}, {0x86, 0xaf}, {0x87, 0xc4}, {0x88, 0xd7}, {0x89, 0xe8}, {0x13, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT}, {0x00, 0}, {0x10, 0}, {0x0d, 0x40}, {0x14, 0x18}, {0xa5, 0x05}, {0xab, 0x07}, {0x24, 0x95}, {0x25, 0x33}, {0x26, 0xe3}, {0x9f, 0x78}, {0xa0, 0x68}, {0xa1, 0x03}, {0xa6, 0xd8}, {0xa7, 0xd8}, {0xa8, 0xf0}, {0xa9, 0x90}, {0xaa, 0x94}, {0x13, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT | COM8_AGC | COM8_AEC}, {0x0e, 0x61}, {0x0f, 0x4b}, {0x16, 0x02}, {0x21, 0x02}, {0x22, 0x91}, {0x29, 0x07}, {0x33, 0x0b}, {0x35, 0x0b}, {0x37, 0x1d}, {0x38, 0x71}, {0x39, 0x2a}, {0x4d, 0x40}, {0x4e, 0x20}, {0x69, 0}, {0x74, 0x10}, {0x8d, 0x4f}, {0x8e, 0}, {0x8f, 0}, {0x90, 0}, {0x91, 0}, {0x96, 0}, {0x9a, 0}, {0xb0, 0x84}, {0xb1, 0x0c}, {0xb2, 0x0e}, {0xb3, 0x82}, {0xb8, 0x0a}, {0x43, 0x0a}, {0x44, 0xf0}, {0x45, 0x34}, {0x46, 0x58}, {0x47, 0x28}, {0x48, 0x3a}, {0x59, 0x88}, {0x5a, 0x88}, {0x5b, 0x44}, {0x5c, 0x67}, {0x5d, 0x49}, {0x5e, 0x0e}, {0x6c, 0x0a}, {0x6d, 0x55}, {0x6e, 0x11}, {0x6f, 0x9f}, {0x6a, 0x40}, {0x01, 0x40}, {0x02, 0x60}, {0x4f, 0x80}, {0x50, 0x80}, {0x51, 0x00}, {0x52, 0x22}, {0x53, 0x5e}, {0x54, 0x80}, {0x56, 0x40}, {0x58, 0x9e}, {0x59, 0x88}, {0x5a, 0x88}, {0x5b, 0x44}, {0x5c, 0x67}, {0x5d, 0x49}, {0x5e, 0x0e}, {0x69, 0x00}, {0x6a, 0x40}, {0x6b, 0x0a}, {0x6c, 0x0a}, {0x6d, 0x55}, {0x6e, 0x11}, {0x6f, 0x9f}, {0xb0, 0x84}, {0x13, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT | COM8_AGC | COM8_AEC | COM8_AWB}, {0x4f, 0x80}, {0x50, 0x80}, {0x51, 0}, {0x52, 0x22}, {0x53, 0x5e}, {0x54, 0x80}, {0x58, 0x9e}, {0x41, 0x08}, {0x3f, 0}, {0x75, 0x05}, {0x76, 0xe1}, {0x4c, 0}, {0x77, 0x01}, {0x3d, 0xc3}, {0x4b, 0x09}, {0x41, 0x38}, {0x56, 0x40}, {0x34, 0x11}, {0x3b, COM11_EXP | COM11_HZAUTO}, {0xa4, 0x88}, {0x96, 0}, {0x97, 0x30}, {0x98, 0x20}, {0x99, 0x30}, {0x9a, 0x84}, {0x9b, 0x29}, {0x9c, 0x03}, {0x9d, 0x4c}, {0x9e, 0x3f}, {0x78, 0x04}, {0x79, 0x01}, {0xc8, 0xf0}, {0x79, 0x0f}, {0xc8, 0x00}, {0x79, 0x10}, {0xc8, 0x7e}, {0x79, 0x0a}, {0xc8, 0x80}, {0x79, 0x0b}, {0xc8, 0x01}, {0x79, 0x0c}, {0xc8, 0x0f}, {0x79, 0x0d}, {0xc8, 0x20}, {0x79, 0x09}, {0xc8, 0x80}, {0x79, 0x02}, {0xc8, 0xc0}, {0x79, 0x03}, {0xc8, 0x40}, {0x79, 0x05}, {0xc8, 0x30}, {0x79, 0x26}, {0xFF, 0xFF}};

#ifdef rp2350 // OV2640 register tables - RP2350 only (the OV2640 is not supported on the RP2040)
// OV2640 RGB565 QVGA (320x240) init - canonical ArduCAM OV2640_QVGA table.
// Bank-switched: reg 0xFF selects sensor(1)/DSP(0) bank. Output format RGB565
// (DSP IMAGE_MODE 0xDA=0x08). Driven with verify=0 (sccb_write) - OV2640 DSP/
// pointer regs don't read back. {0xFF,0xFF} terminator (0xFF=bank reg, never 0xFF value).
static const OV7670_command OV2640_QVGA[] = {
    {0xff, 0x00}, {0x2c, 0xff}, {0x2e, 0xdf}, {0xff, 0x01}, {0x3c, 0x32}, {0x11, 0x00}, {0x09, 0x02}, {0x04, 0x28}, {0x13, 0xe5}, {0x14, 0x48}, {0x2c, 0x0c}, {0x33, 0x78}, {0x3a, 0x33}, {0x3b, 0xfb}, {0x3e, 0x00}, {0x43, 0x11}, {0x16, 0x10}, {0x39, 0x02}, {0x35, 0x88}, {0x22, 0x0a}, {0x37, 0x40}, {0x23, 0x00}, {0x34, 0xa0}, {0x06, 0x02}, {0x06, 0x88}, {0x07, 0xc0}, {0x0d, 0xb7}, {0x0e, 0x01}, {0x4c, 0x00}, {0x4a, 0x81}, {0x21, 0x99}, {0x24, 0x28}, {0x25, 0x20}, {0x26, 0x82}, {0x5c, 0x00}, {0x63, 0x00}, {0x46, 0x22}, {0x0c, 0x3a}, {0x5d, 0x55}, {0x5e, 0x7d}, {0x5f, 0x7d}, {0x60, 0x55}, {0x61, 0x70}, {0x62, 0x80}, {0x7c, 0x05}, {0x20, 0x80}, {0x28, 0x30}, {0x6c, 0x00}, {0x6d, 0x80}, {0x6e, 0x00}, {0x70, 0x02}, {0x71, 0x94}, {0x73, 0xc1}, {0x3d, 0x34}, {0x12, 0x04}, {0x5a, 0x57}, {0x4f, 0xbb}, {0x50, 0x9c}, {0xff, 0x00}, {0xe5, 0x7f}, {0xf9, 0xc0}, {0x41, 0x24}, {0xe0, 0x14}, {0x76, 0xff}, {0x33, 0xa0}, {0x42, 0x20}, {0x43, 0x18}, {0x4c, 0x00}, {0x87, 0xd0}, {0x88, 0x3f}, {0xd7, 0x03}, {0xd9, 0x10}, {0xd3, 0x82}, {0xc8, 0x08}, {0xc9, 0x80}, {0x7c, 0x00}, {0x7d, 0x02}, {0x7c, 0x03}, {0x7d, 0x68}, {0x7d, 0x68}, // SDE: enable saturation (0x02) + boost U/V (0x68)
    {0x7c, 0x08},
    {0x7d, 0x20},
    {0x7d, 0x10},
    {0x7d, 0x0e},
    {0x90, 0x00},
    {0x91, 0x0e},
    {0x91, 0x1a},
    {0x91, 0x31},
    {0x91, 0x5a},
    {0x91, 0x69},
    {0x91, 0x75},
    {0x91, 0x7e},
    {0x91, 0x88},
    {0x91, 0x8f},
    {0x91, 0x96},
    {0x91, 0xa3},
    {0x91, 0xaf},
    {0x91, 0xc4},
    {0x91, 0xd7},
    {0x91, 0xe8},
    {0x91, 0x20},
    {0x92, 0x00},
    {0x93, 0x06},
    {0x93, 0xe3},
    {0x93, 0x03},
    {0x93, 0x03},
    {0x93, 0x00},
    {0x93, 0x02},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x96, 0x00},
    {0x97, 0x08},
    {0x97, 0x19},
    {0x97, 0x02},
    {0x97, 0x0c},
    {0x97, 0x24},
    {0x97, 0x30},
    {0x97, 0x28},
    {0x97, 0x26},
    {0x97, 0x02},
    {0x97, 0x98},
    {0x97, 0x80},
    {0x97, 0x00},
    {0x97, 0x00},
    {0xa4, 0x00},
    {0xa8, 0x00},
    {0xc5, 0x11},
    {0xc6, 0x51},
    {0xbf, 0x80},
    {0xc7, 0x10},
    {0xb6, 0x66},
    {0xb8, 0xa5},
    {0xb7, 0x64},
    {0xb9, 0x7c},
    {0xb3, 0xaf},
    {0xb4, 0x97},
    {0xb5, 0xff},
    {0xb0, 0xc5},
    {0xb1, 0x94},
    {0xb2, 0x0f},
    {0xc4, 0x5c},
    {0xa6, 0x00},
    {0xa7, 0x20},
    {0xa7, 0xd8},
    {0xa7, 0x1b},
    {0xa7, 0x31},
    {0xa7, 0x00},
    {0xa7, 0x18},
    {0xa7, 0x20},
    {0xa7, 0xd8},
    {0xa7, 0x19},
    {0xa7, 0x31},
    {0xa7, 0x00},
    {0xa7, 0x18},
    {0xa7, 0x20},
    {0xa7, 0xd8},
    {0xa7, 0x19},
    {0xa7, 0x31},
    {0xa7, 0x00},
    {0xa7, 0x18},
    {0x7f, 0x00},
    {0xe5, 0x1f},
    {0xe1, 0x77},
    {0xdd, 0x7f},
    {0xc2, 0x0e},
    {0xff, 0x00},
    {0xe0, 0x04},
    {0xc0, 0xc8},
    {0xc1, 0x96},
    {0x86, 0x3d},
    {0x51, 0x90},
    {0x52, 0x2c},
    {0x53, 0x00},
    {0x54, 0x00},
    {0x55, 0x88},
    {0x57, 0x00},
    {0x50, 0x92},
    {0x5a, 0x50},
    {0x5b, 0x3c},
    {0x5c, 0x00},
    {0xd3, OV2640_DVP_CLK},
    {0xe0, 0x00},
    {0xff, 0x00}, // 0xD3 (R_DVP_SP) slows DVP PCLK for the bit-banged capture
    {0x05, 0x00},
    {0xda, 0x08},
    {0xd7, 0x03},
    {0xe0, 0x00},
    {0x05, 0x00},
    {0xFF, 0xFF}};

// OV2640 JPEG base init (ArduCAM)
static const OV7670_command OV2640_JPEG_INIT[] = {
    {0xff, 0x00}, {0x2c, 0xff}, {0x2e, 0xdf}, {0xff, 0x01}, {0x3c, 0x32}, {0x11, 0x00}, {0x09, 0x02}, {0x04, 0x28}, {0x13, 0xe5}, {0x14, 0x48}, {0x2c, 0x0c}, {0x33, 0x78}, {0x3a, 0x33}, {0x3b, 0xfb}, {0x3e, 0x00}, {0x43, 0x11}, {0x16, 0x10}, {0x39, 0x92}, {0x35, 0xda}, {0x22, 0x1a}, {0x37, 0xc3}, {0x23, 0x00}, {0x34, 0xc0}, {0x36, 0x1a}, {0x06, 0x88}, {0x07, 0xc0}, {0x0d, 0x87}, {0x0e, 0x41}, {0x4c, 0x00}, {0x48, 0x00}, {0x5b, 0x00}, {0x42, 0x03}, {0x4a, 0x81}, {0x21, 0x99}, {0x24, 0x28}, {0x25, 0x20}, {0x26, 0x82}, {0x5c, 0x00}, {0x63, 0x00}, {0x61, 0x70}, {0x62, 0x80}, {0x7c, 0x05}, {0x20, 0x80}, {0x28, 0x30}, {0x6c, 0x00}, {0x6d, 0x80}, {0x6e, 0x00}, {0x70, 0x02}, {0x71, 0x94}, {0x73, 0xc1}, {0x12, 0x40}, {0x17, 0x11}, {0x18, 0x43}, {0x19, 0x00}, {0x1a, 0x4b}, {0x32, 0x09}, {0x37, 0xc0}, {0x4f, 0x60}, {0x50, 0xa8}, {0x6d, 0x00}, {0x3d, 0x38}, {0x46, 0x3f}, {0x4f, 0x60}, {0x0c, 0x3c}, {0xff, 0x00}, {0xe5, 0x7f}, {0xf9, 0xc0}, {0x41, 0x24}, {0xe0, 0x14}, {0x76, 0xff}, {0x33, 0xa0}, {0x42, 0x20}, {0x43, 0x18}, {0x4c, 0x00}, {0x87, 0xd5}, {0x88, 0x3f}, {0xd7, 0x03}, {0xd9, 0x10}, {0xd3, 0x82}, {0xc8, 0x08}, {0xc9, 0x80}, {0x7c, 0x00}, {0x7d, 0x02}, {0x7c, 0x03}, // SDE: enable saturation
    {0x7d, 0x68},
    {0x7d, 0x68},
    {0x7c, 0x08},
    {0x7d, 0x20},
    {0x7d, 0x10},
    {0x7d, 0x0e}, // boosted U/V (0x68)
    {0x90, 0x00},
    {0x91, 0x0e},
    {0x91, 0x1a},
    {0x91, 0x31},
    {0x91, 0x5a},
    {0x91, 0x69},
    {0x91, 0x75},
    {0x91, 0x7e},
    {0x91, 0x88},
    {0x91, 0x8f},
    {0x91, 0x96},
    {0x91, 0xa3},
    {0x91, 0xaf},
    {0x91, 0xc4},
    {0x91, 0xd7},
    {0x91, 0xe8},
    {0x91, 0x20},
    {0x92, 0x00},
    {0x93, 0x06},
    {0x93, 0xe3},
    {0x93, 0x05},
    {0x93, 0x05},
    {0x93, 0x00},
    {0x93, 0x04},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x96, 0x00},
    {0x97, 0x08},
    {0x97, 0x19},
    {0x97, 0x02},
    {0x97, 0x0c},
    {0x97, 0x24},
    {0x97, 0x30},
    {0x97, 0x28},
    {0x97, 0x26},
    {0x97, 0x02},
    {0x97, 0x98},
    {0x97, 0x80},
    {0x97, 0x00},
    {0x97, 0x00},
    {0xc3, 0xed},
    {0xa4, 0x00},
    {0xa8, 0x00},
    {0xc5, 0x11},
    {0xc6, 0x51},
    {0xbf, 0x80},
    {0xc7, 0x10},
    {0xb6, 0x66},
    {0xb8, 0xa5},
    {0xb7, 0x64},
    {0xb9, 0x7c},
    {0xb3, 0xaf},
    {0xb4, 0x97},
    {0xb5, 0xff},
    {0xb0, 0xc5},
    {0xb1, 0x94},
    {0xb2, 0x0f},
    {0xc4, 0x5c},
    {0xc0, 0x64},
    {0xc1, 0x4b},
    {0x8c, 0x00},
    {0x86, 0x3d},
    {0x50, 0x00},
    {0x51, 0xc8},
    {0x52, 0x96},
    {0x53, 0x00},
    {0x54, 0x00},
    {0x55, 0x00},
    {0x5a, 0xc8},
    {0x5b, 0x96},
    {0x5c, 0x00},
    {0xd3, 0x00},
    {0xc3, 0xed},
    {0x7f, 0x00},
    {0xda, 0x00},
    {0xe5, 0x1f},
    {0xe1, 0x67},
    {0xe0, 0x00},
    {0xdd, 0x7f},
    {0x05, 0x00},
    {0x12, 0x40},
    {0xd3, 0x04},
    {0xc0, 0x16},
    {0xc1, 0x12},
    {0x8c, 0x00},
    {0x86, 0x3d},
    {0x50, 0x00},
    {0x51, 0x2c},
    {0x52, 0x24},
    {0x53, 0x00},
    {0x54, 0x00},
    {0x55, 0x00},
    {0x5a, 0x2c},
    {0x5b, 0x24},
    {0x5c, 0x00},
    {0xFF, 0xFF}};

// OV2640 YUV422 colour space (JPEG is encoded from YUV)
static const OV7670_command OV2640_YUV422[] = {
    {0xff, 0x00}, {0x05, 0x00}, {0xda, 0x10}, {0xd7, 0x03}, {0xdf, 0x00}, {0x33, 0x80}, {0x3c, 0x40}, {0xe1, 0x77}, {0x00, 0x00}, {0xFF, 0xFF}};

// OV2640 enable JPEG output
static const OV7670_command OV2640_JPEG[] = {
    {0xe0, 0x14}, {0xe1, 0x77}, {0xe5, 0x1f}, {0xd7, 0x03}, {0xda, 0x10}, {0xe0, 0x00}, {0xff, 0x01}, {0x04, 0x08}, {0xFF, 0xFF}};

// OV2640 VGA 640x480 JPEG framesize
static const OV7670_command OV2640_640x480_JPEG[] = {
    {0xff, 0x01}, {0x11, 0x01}, {0x12, 0x00}, {0x17, 0x11}, {0x18, 0x75}, {0x32, 0x36}, {0x19, 0x01}, {0x1a, 0x97}, {0x03, 0x0f}, {0x37, 0x40}, {0x4f, 0xbb}, {0x50, 0x9c}, {0x5a, 0x57}, {0x6d, 0x80}, {0x3d, 0x34}, {0x39, 0x02}, {0x35, 0x88}, {0x22, 0x0a}, {0x37, 0x40}, {0x34, 0xa0}, {0x06, 0x02}, {0x0d, 0xb7}, {0x0e, 0x01}, {0xff, 0x00}, {0xe0, 0x04}, {0xc0, 0xc8}, {0xc1, 0x96}, {0x86, 0x3d}, {0x50, 0x89}, {0x51, 0x90}, {0x52, 0x2c}, {0x53, 0x00}, {0x54, 0x00}, {0x55, 0x88}, {0x57, 0x00}, {0x5a, 0xa0}, {0x5b, 0x78}, {0x5c, 0x00}, {0xd3, 0x04}, {0xe0, 0x00}, {0xFF, 0xFF}};
// OV2640 SVGA 800x600 JPEG framesize (ArduCAM)
static const OV7670_command OV2640_800x600_JPEG[] = {
    {0xff, 0x01}, {0x11, 0x01}, {0x12, 0x00}, {0x17, 0x11}, {0x18, 0x75}, {0x32, 0x36}, {0x19, 0x01}, {0x1a, 0x97}, {0x03, 0x0f}, {0x37, 0x40}, {0x4f, 0xbb}, {0x50, 0x9c}, {0x5a, 0x57}, {0x6d, 0x80}, {0x3d, 0x34}, {0x39, 0x02}, {0x35, 0x88}, {0x22, 0x0a}, {0x37, 0x40}, {0x34, 0xa0}, {0x06, 0x02}, {0x0d, 0xb7}, {0x0e, 0x01}, {0xff, 0x00}, {0xe0, 0x04}, {0xc0, 0xc8}, {0xc1, 0x96}, {0x86, 0x35}, {0x50, 0x89}, {0x51, 0x90}, {0x52, 0x2c}, {0x53, 0x00}, {0x54, 0x00}, {0x55, 0x88}, {0x57, 0x00}, {0x5a, 0xc8}, {0x5b, 0x96}, {0x5c, 0x00}, {0xd3, 0x02}, {0xe0, 0x00}, {0xFF, 0xFF}};
// OV2640 XGA 1024x768 JPEG framesize (ArduCAM)
static const OV7670_command OV2640_1024x768_JPEG[] = {
    {0xff, 0x01}, {0x11, 0x01}, {0x12, 0x00}, {0x17, 0x11}, {0x18, 0x75}, {0x32, 0x36}, {0x19, 0x01}, {0x1a, 0x97}, {0x03, 0x0f}, {0x37, 0x40}, {0x4f, 0xbb}, {0x50, 0x9c}, {0x5a, 0x57}, {0x6d, 0x80}, {0x3d, 0x34}, {0x39, 0x02}, {0x35, 0x88}, {0x22, 0x0a}, {0x37, 0x40}, {0x34, 0xa0}, {0x06, 0x02}, {0x0d, 0xb7}, {0x0e, 0x01}, {0xff, 0x00}, {0xc0, 0xc8}, {0xc1, 0x96}, {0x8c, 0x00}, {0x86, 0x3d}, {0x50, 0x00}, {0x51, 0x90}, {0x52, 0x2c}, {0x53, 0x00}, {0x54, 0x00}, {0x55, 0x88}, {0x5a, 0x00}, {0x5b, 0xc0}, {0x5c, 0x01}, {0xd3, 0x02}, {0xFF, 0xFF}};
// OV2640 SXGA 1280x1024 JPEG framesize (ArduCAM)
static const OV7670_command OV2640_1280x1024_JPEG[] = {
    {0xff, 0x01}, {0x11, 0x01}, {0x12, 0x00}, {0x17, 0x11}, {0x18, 0x75}, {0x32, 0x36}, {0x19, 0x01}, {0x1a, 0x97}, {0x03, 0x0f}, {0x37, 0x40}, {0x4f, 0xbb}, {0x50, 0x9c}, {0x5a, 0x57}, {0x6d, 0x80}, {0x3d, 0x34}, {0x39, 0x02}, {0x35, 0x88}, {0x22, 0x0a}, {0x37, 0x40}, {0x34, 0xa0}, {0x06, 0x02}, {0x0d, 0xb7}, {0x0e, 0x01}, {0xff, 0x00}, {0xe0, 0x04}, {0xc0, 0xc8}, {0xc1, 0x96}, {0x86, 0x3d}, {0x50, 0x00}, {0x51, 0x90}, {0x52, 0x2c}, {0x53, 0x00}, {0x54, 0x00}, {0x55, 0x88}, {0x57, 0x00}, {0x5a, 0x40}, {0x5b, 0xf0}, {0x5c, 0x01}, {0xd3, 0x02}, {0xe0, 0x00}, {0xFF, 0xFF}};
// OV2640 UXGA 1600x1200 JPEG framesize (ArduCAM) - full 2MP
static const OV7670_command OV2640_1600x1200_JPEG[] = {
    {0xff, 0x01}, {0x11, 0x01}, {0x12, 0x00}, {0x17, 0x11}, {0x18, 0x75}, {0x32, 0x36}, {0x19, 0x01}, {0x1a, 0x97}, {0x03, 0x0f}, {0x37, 0x40}, {0x4f, 0xbb}, {0x50, 0x9c}, {0x5a, 0x57}, {0x6d, 0x80}, {0x3d, 0x34}, {0x39, 0x02}, {0x35, 0x88}, {0x22, 0x0a}, {0x37, 0x40}, {0x34, 0xa0}, {0x06, 0x02}, {0x0d, 0xb7}, {0x0e, 0x01}, {0xff, 0x00}, {0xe0, 0x04}, {0xc0, 0xc8}, {0xc1, 0x96}, {0x86, 0x3d}, {0x50, 0x00}, {0x51, 0x90}, {0x52, 0x2c}, {0x53, 0x00}, {0x54, 0x00}, {0x55, 0x88}, {0x57, 0x00}, {0x5a, 0x90}, {0x5b, 0x2c}, {0x5c, 0x05}, {0xd3, 0x02}, {0xe0, 0x00}, {0xFF, 0xFF}};
#endif // rp2350 (OV2640 register tables)

void OV7670_frame_control(uint8_t size, uint8_t vstart,
                          uint16_t hstart, uint8_t edge_offset,
                          uint8_t pclk_delay)
{
  uint8_t value;

  // Enable downsampling if sub-VGA, and zoom if 1:16 scale
  value = (size > OV7670_SIZE_DIV1) ? OV7670_COM3_DCWEN : 0;
  if (size == OV7670_SIZE_DIV16)
    value |= OV7670_COM3_SCALEEN;
  ov7670_set(OV7670_REG_COM3, value);

  // Enable PCLK division if sub-VGA 2,4,8,16 = 0x19,1A,1B,1C
  value = (size > OV7670_SIZE_DIV1) ? (0x18 + size) : 0;
  ov7670_set(OV7670_REG_COM14, value);

  // Horiz/vert downsample ratio, 1:8 max (H,V are always equal for now)
  value = (size <= OV7670_SIZE_DIV8) ? size : OV7670_SIZE_DIV8;
  ov7670_set(OV7670_REG_SCALING_DCWCTR, value * 0x11);

  // Pixel clock divider if sub-VGA
  value = (size > OV7670_SIZE_DIV1) ? (0xF0 + size) : 0x08;
  ov7670_set(OV7670_REG_SCALING_PCLK_DIV, value);

  // Apply 0.5 digital zoom at 1:16 size (others are downsample only)
  value = (size == OV7670_SIZE_DIV16) ? 0x40 : 0x20; // 0.5, 1.0
  // Read current SCALING_XSC and SCALING_YSC register values because
  // test pattern settings are also stored in those registers and we
  // don't want to corrupt anything there.
  uint8_t xsc = readregister(OV7670_REG_SCALING_XSC);
  uint8_t ysc = readregister(OV7670_REG_SCALING_YSC);
  xsc = (xsc & 0x80) | value; // Modify only scaling bits (not test pattern)
  ysc = (ysc & 0x80) | value;
  // Write modified result back to SCALING_XSC and SCALING_YSC
  ov7670_set(OV7670_REG_SCALING_XSC, xsc);
  ov7670_set(OV7670_REG_SCALING_YSC, ysc);

  // Window size is scattered across multiple registers.
  // Horiz/vert stops can be automatically calc'd from starts.
  uint16_t vstop = vstart + 480;
  uint16_t hstop = (hstart + 640) % 784;
  ov7670_set(OV7670_REG_HSTART, hstart >> 3);
  ov7670_set(OV7670_REG_HSTOP, hstop >> 3);
  ov7670_set(OV7670_REG_HREF,
             (edge_offset << 6) | ((hstop & 0b111) << 3) |
                 (hstart & 0b111));
  ov7670_set(OV7670_REG_VSTART, vstart >> 2);
  ov7670_set(OV7670_REG_VSTOP, vstop >> 2);
  ov7670_set(OV7670_REG_VREF,
             ((vstop & 0b11) << 2) | (vstart & 0b11));

  ov7670_set(OV7670_REG_SCALING_PCLK_DELAY, pclk_delay);
}
void OV7670_set_size(OV7670_size size)
{
  // Array of five window settings, index of each (0-4) aligns with the five
  // OV7670_size enumeration values. If enum changes, list must change!
  static struct
  {
    uint8_t vstart;
    uint8_t hstart;
    uint8_t edge_offset;
    uint8_t pclk_delay;
  } window[] = {
      // Window settings were tediously determined empirically.
      // I hope there's a formula for this, if a do-over is needed.
      {
          9,
          162,
          2,
          2}, // SIZE_DIV1  640x480 VGA
      {
          10,
          174,
          4,
          2}, // SIZE_DIV2  320x240 QVGA
      {
          11,
          186,
          2,
          2}, // SIZE_DIV4  160x120 QQVGA
      {
          12,
          210,
          0,
          2}, // SIZE_DIV8  80x60   ...
      {
          15,
          252,
          3,
          2}, // SIZE_DIV16 40x30
  };

  OV7670_frame_control(size, window[size].vstart, window[size].hstart,
                       window[size].edge_offset, window[size].pclk_delay);
}
#define ST_PCLK gpio_get(PCLKGP)
#define ST_HREF gpio_get(HREFGP)
#define ST_VSYNC gpio_get(VSYNCGP)
#ifndef rp2350
void __not_in_flash_func(capture)(char *buff)
{
  char *k = buff;
  while (ST_VSYNC)
  {
  } /* wait for the old frame to end */
  while (!ST_VSYNC)
  {
  } /* wait for a new frame to start */
  // At this point VSync has gone high and the frame is about to start
  while (!ST_HREF)
  {
  } // wait for the first line to start
  while (!ST_PCLK)
  {
  } // wait for clock to go high /
  while (ST_PCLK)
  {
  } // wait for clock to go back low /
  for (int i = 0; i < 160; i++)
  {
    while (!ST_PCLK)
    {
    } // wait for clock to go high /
    *k++ = gpio_get_all() >> PinDef[D0].GPno;
    while (ST_PCLK)
    {
    } // wait for clock to go back low /

    // second byte/
    while (!ST_PCLK)
    {
    } // wait for clock to go high /
    *k++ = gpio_get_all() >> PinDef[D0].GPno;
    while (ST_PCLK)
    {
    } // wait for clock to go back low /
  }
  while (ST_HREF)
  {
  } // wait for the first line to end*/
  k = buff;
  for (int j = 0; j < 119; j++)
  {
    while (!ST_HREF)
    {
    } // wait for the first line to end
    for (int i = 0; i < 160; i++)
    {
      while (!ST_PCLK)
      {
      } // wait for clock to go high /
      *k++ = gpio_get_all() >> PinDef[D0].GPno;
      while (ST_PCLK)
      {
      } // wait for clock to go back low /

      // second byte/
      while (!ST_PCLK)
      {
      } // wait for clock to go high /
      *k++ = gpio_get_all() >> PinDef[D0].GPno;
      while (ST_PCLK)
      {
      } // wait for clock to go back low /
    }
    while (ST_HREF)
    {
    } // wait for the first line to end
  }
}
#else
void __not_in_flash_func(capture)(char *buff)
{
  const int width = CameraWidth;   // 160 (QQVGA) or 320 (QVGA)
  const int height = CameraHeight; // 120 (QQVGA) or 240 (QVGA)
  char *k = buff;
  while (ST_VSYNC)
  {
  } /* wait for the old frame to end */
  while (!ST_VSYNC)
  {
  } /* wait for a new frame to start */
  // At this point VSync has gone high and the frame is about to start
  while (!ST_HREF)
  {
  } // wait for the first line to start
  while (!ST_PCLK)
  {
  } // wait for clock to go high /
  while (ST_PCLK)
  {
  } // wait for clock to go back low /
  for (int i = 0; i < width; i++)
  {
    while (!ST_PCLK)
    {
    } // wait for clock to go high /
    *k++ = gpio_get_all64() >> PinDef[D0].GPno;
    while (ST_PCLK)
    {
    } // wait for clock to go back low /

    // second byte/
    while (!ST_PCLK)
    {
    } // wait for clock to go high /
    *k++ = gpio_get_all64() >> PinDef[D0].GPno;
    while (ST_PCLK)
    {
    } // wait for clock to go back low /
  }
  while (ST_HREF)
  {
  } // wait for the first line to end*/
  k = buff;
  for (int j = 0; j < height - 1; j++)
  {
    while (!ST_HREF)
    {
    } // wait for the first line to end
    for (int i = 0; i < width; i++)
    {
      while (!ST_PCLK)
      {
      } // wait for clock to go high /
      *k++ = gpio_get_all64() >> PinDef[D0].GPno;
      while (ST_PCLK)
      {
      } // wait for clock to go back low /

      // second byte/
      while (!ST_PCLK)
      {
      } // wait for clock to go high /
      *k++ = gpio_get_all64() >> PinDef[D0].GPno;
      while (ST_PCLK)
      {
      } // wait for clock to go back low /
    }
    while (ST_HREF)
    {
    } // wait for the first line to end
  }
}
#endif
#ifdef rp2350 // OV2640 JPEG capture - RP2350 only
// Capture the OV2640's variable-length JPEG byte stream off the DVP bus, bit-banged
// like capture(): sync to a frame, then read bytes while HREF is active. Stops at the
// JPEG EOI marker (FF D9), when the frame ends, or at maxlen. Returns the byte count;
// the caller trims to SOI..EOI.
int __not_in_flash_func(capture_jpeg)(uint8_t *buff, int maxlen)
{
  int n = 0;
  int gp = PinDef[D0].GPno;
  while (ST_VSYNC)
  {
  } // wait for the current frame to finish
  while (!ST_VSYNC)
  {
  } // wait for the next frame to start
  while (n < maxlen)
  {
    while (!ST_HREF) // skip horizontal blanking
      if (!ST_VSYNC)
        return n; // frame ended
    while (ST_HREF && n < maxlen)
    {
      while (!ST_PCLK)
      {
      }
#ifndef rp2350
      buff[n++] = gpio_get_all() >> gp;
#else
      buff[n++] = gpio_get_all64() >> gp;
#endif
      while (ST_PCLK)
      {
      }
      if (n >= 2 && buff[n - 2] == 0xFF && buff[n - 1] == 0xD9)
        return n; // JPEG end-of-image marker
    }
  }
  return n;
}
#endif                 // rp2350 (OV2640 JPEG capture)
void saturation(int s) //-2 to 2
{
  // color matrix values
  ov7670_set(0x4f, 0x80 + 0x20 * s);
  ov7670_set(0x50, 0x80 + 0x20 * s);
  ov7670_set(0x51, 0x00);
  ov7670_set(0x52, 0x22 + (0x11 * s) / 2);
  ov7670_set(0x53, 0x5e + (0x2f * s) / 2);
  ov7670_set(0x54, 0x80 + 0x20 * s);
  ov7670_set(0x58, 0x9e); // matrix signs
}

/*  @endcond */
void MIPS16 cmd_camera(void)
{
  union colourmap
  {
    char rgbbytes[4];
    unsigned int rgb;
  } c;
  unsigned char *tp = NULL;
  if ((tp = checkstring(cmdline, (unsigned char *)"OPEN")))
  {
    int pin1, pin2, pin3, pin4, pin5, pin6;
    // Optional leading camera-type keyword: CAMERA OPEN [OV7670|OV2640,] pins...
    // If omitted, default to OV7670 for backward compatibility.
    unsigned char *ct;
    int gotkw = 0;
    CameraType = CAM_OV7670;
    camera_address = ov7670_address;
    if ((ct = checkstring(tp, (unsigned char *)"OV2640")))
    {
      CameraType = CAM_OV2640;
      camera_address = ov2640_address;
      tp = ct;
      gotkw = 1;
    }
    else if ((ct = checkstring(tp, (unsigned char *)"OV7670")))
    {
      tp = ct;
      gotkw = 1;
    }
    if (gotkw) // step past the comma separating the keyword from the first pin
    {
      while (*tp == ' ')
        tp++;
      if (*tp == ',')
        tp++;
    }
    getcsargs(&tp, 13);
    if (!(argc == 11 || argc == 13))
      SyntaxError();
    ;
    // Optional trailing resolution keyword (QQVGA default, QVGA = 320x240, RP2350 only)
    CameraWidth = 160;
    CameraHeight = 120;
    if (CameraType == CAM_OV2640)
    {
      // The OV2640 init table is fixed QVGA RGB565 (any trailing keyword is ignored).
#ifndef rp2350
      error("OV2640 is only supported on RP2350");
#endif
      CameraWidth = 320;
      CameraHeight = 240;
    }
    else if (argc == 13)
    {
      if (checkstring(argv[12], (unsigned char *)"QVGA"))
      {
#ifndef rp2350
        error("QVGA (320x240) is only supported on RP2350");
#endif
        CameraWidth = 320;
        CameraHeight = 240;
      }
      else if (!checkstring(argv[12], (unsigned char *)"QQVGA"))
        error("Invalid resolution");
    }
#if defined(HDMI) && !defined(HDMICUTDOWN)
    // On HDMI the camera renders straight into the mode-4 320x240 RGB555 framebuffer
    if (DISPLAY_TYPE != SCREENMODE4)
      error("CAMERA requires MODE 4 (320x240 RGB555)");
    // MODE 4 is only 320x240 at 640x480; at 720x400 it is 360x200 which a QVGA
    // (320x240) capture cannot fit, so block that combination here.
    if (CameraWidth == 320 && Option.Resolution == R720x400)
      error("QVGA not available at 720x400 - use 640x480 resolution");
#endif
    // On non-HDMI builds OPEN is NOT tied to a particular display type: CAMERA CAPTURE JPEG
    // writes to a file and CAMERA CHANGE (motion) needs no display, so OPEN works with any
    // (or no) panel. The display capability is checked in the simple CAPTURE command below
    // (the only sub-command that draws to the panel).
    if (!(I2C0locked || I2C1locked))
      StandardError(44);
    if (XCLK)
      error("Camera already open");
    // pin1: XCLK (master clock out) for the OV7670; for the self-clocked OV2640 this
    // is the PWDN pin instead (no master clock needed).
    pin1 = getpinarg(argv[0]);
    if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)
      StandardErrorParam2(27, pin1, pin1);
    if (CameraType == CAM_OV7670) // XCLK uses a PWM slice - check for conflicts
    {
      int slice = getslice(pin1);
      if ((PinDef[Option.DISPLAY_BL].slice & 0x7f) == slice)
        error("Channel in use for backlight");
      if ((PinDef[pin1].slice & 0x7f) == Option.AUDIO_SLICE)
        error("Channel in use for Audio");
    }

    // PCLK pin
    pin2 = getpinarg(argv[2]);
    if (ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)
      StandardErrorParam2(27, pin2, pin2);

    // HREF pin
    pin3 = getpinarg(argv[4]);
    if (ExtCurrentConfig[pin3] != EXT_NOT_CONFIG)
      StandardErrorParam2(27, pin3, pin3);

    // VSYNC pin
    pin4 = getpinarg(argv[6]);
    if (ExtCurrentConfig[pin4] != EXT_NOT_CONFIG)
      StandardErrorParam2(27, pin4, pin4);

    // RESET pin
    pin5 = getpinarg(argv[8]);
    if (ExtCurrentConfig[pin5] != EXT_NOT_CONFIG)
      StandardErrorParam2(27, pin5, pin5);

    // D0-D7 pins
    pin6 = getpinarg(argv[10]);
    int startdata = PinDef[pin6].GPno;
    for (int i = startdata; i < startdata + 8; i++)
    {
      if (IsInvalidPin(PINMAP[i]))
        StandardError(9);
      if (ExtCurrentConfig[PINMAP[i]] != EXT_NOT_CONFIG)
        StandardErrorParam2(27, PINMAP[i], PINMAP[i]);
    }
    XCLK = pin1; // pin1 = XCLK (OV7670) or PWDN (OV2640); also the "camera open" flag / pin to free
    PCLK = pin2;
    HREF = pin3;
    VSYNC = pin4;
    RESET = pin5;
    D0 = pin6;
    if (CameraType == CAM_OV7670)
    {
      setpwm(pin1, &CameraChannel, &CameraSlice, 12000000.0, 50.0); // 12MHz master clock
      ExtCfg(XCLK, EXT_COM_RESERVED, 0);
    }
    else
    {
      // OV2640 is self-clocked: pin1 is PWDN - drive it LOW to power the sensor up.
      ExtCfg(pin1, EXT_DIG_OUT, 0);
      ExtCfg(pin1, EXT_COM_RESERVED, 0);
    }
    ExtCfg(PCLK, EXT_DIG_IN, 0);
    ExtCfg(PCLK, EXT_COM_RESERVED, 0);
    ExtCfg(HREF, EXT_DIG_IN, 0);
    ExtCfg(HREF, EXT_COM_RESERVED, 0);
    ExtCfg(VSYNC, EXT_DIG_IN, 0);
    ExtCfg(VSYNC, EXT_COM_RESERVED, 0);
    ExtCfg(RESET, EXT_DIG_OUT, 1);
    ExtCfg(RESET, EXT_COM_RESERVED, 0);
    for (int i = startdata; i < startdata + 8; i++)
    {
      ExtCfg(PINMAP[i], EXT_DIG_IN, 0);
      ExtCfg(PINMAP[i], EXT_COM_RESERVED, 0);
    }
    PCLKGP = PinDef[PCLK].GPno;
    VSYNCGP = PinDef[VSYNC].GPno;
    HREFGP = PinDef[HREF].GPno;
    uSec(1000);
    PinSetBit(pin5, LATCLR);
    uSec(1000);
    PinSetBit(pin5, LATSET);
    uSec(1000);
#ifdef rp2350 // OV2640 init path - RP2350 only
    if (CameraType == CAM_OV2640)
    {
      // OV2640: bank-switch reset, confirm the sensor, then load RGB565 QVGA (no verify).
      sccb_write(0xFF, 0x01); // sensor bank
      sccb_write(0x12, 0x80); // COM7 soft reset
      uSec(50000);
      sccb_write(0xFF, 0x01);         // sensor bank (reset returns to bank 0)
      if (readregister(0x0A) != 0x26) // sensor-bank PIDH = 0x26 for OV2640
        error("Camera not found");
      load_camera_regs(OV2640_QVGA, 0); // RGB565 QVGA init (table driven, no read-back)
    }
    else
#endif
    {
      if (readregister(REG_PID) != 118)
        error("Camera not found");
      load_camera_regs(OV7670_init, 1); // OV7670 RGB565 init (table driven)
      saturation(2);                    // boost colour-matrix gains - OV7670 RGB565 is otherwise washed out
#ifdef rp2350                           // QVGA (320x240) is RP2350 only; the RP2040 is always QQVGA (DIV4)
      if (CameraWidth == 320)
      {
        // QVGA's PCLK would otherwise run at 2x the QQVGA rate, which the bit-banged
        // capture loop can't follow without a large CPU overclock (~396MHz). Halving
        // the sensor master clock (CLKRC prescaler /2) brings QVGA's PCLK down to the
        // QQVGA rate so the same capture loop keeps up at normal CPU speeds. CLKRC
        // scales the whole pixel chain, so DCW decimation stays matched to PCLK (unlike
        // forcing SCALING_PCLK_DIV 0x73, which would desync and corrupt the image).
        // Trade-off: lower frame rate, fine for still capture. XCLK stays 12MHz (in spec).
        ov7670_set(REG_CLKRC, 0x01);
        OV7670_set_size(OV7670_SIZE_DIV2);
      }
      else
#endif
        OV7670_set_size(OV7670_SIZE_DIV4);
      ov7670_set(REG_COM10, 0x02); // 0x02   VSYNC negative (http://nasulica.homelinux.org/?p=959)
    }
    // check the input signals
    uint64_t us = time_us_64() + 1000000;
    while (!ST_PCLK && time_us_64() < us)
    {
    } /* wait for clock to go high */
    while (ST_PCLK && time_us_64() < us)
    {
    } /* wait for clock to go back low */
    if (time_us_64() > us)
      error("Timeout on camera PCLK signal");
    while (ST_HREF && time_us_64() < us)
    {
    } /* wait for a line to end */
    while (!ST_HREF && time_us_64() < us)
    {
    } /* wait for a line to end */
    if (time_us_64() > us)
      error("Timeout on camera HREF signal");
    while (ST_VSYNC && time_us_64() < us)
    {
    } /* wait for the old frame to end */
    while (!ST_VSYNC && time_us_64() < us)
    {
    } /* wait for a new frame to start */
    if (time_us_64() > us)
      error("Timeout on camera VSYNC signal");

    //			OV7670_test_pattern(OV7670_TEST_PATTERN_COLOR_BAR);
  }
  else if ((tp = checkstring(cmdline, (unsigned char *)"TEST")))
  {
    if (CameraType != CAM_OV7670)
      error("TEST not supported on this camera");
    getcsargs(&tp, 1);
    OV7670_test_pattern(getint(argv[0], 0, 3));
  }
  else if ((tp = checkstring(cmdline, (unsigned char *)"REGISTER")))
  {
    getcsargs(&tp, 3);
    if (!XCLK)
      error("Camera not open");
    int a = getint(argv[0], 0, 255);
    int b = getint(argv[2], 0, 255);
    int c = readregister(a);
    if (CameraType == CAM_OV7670)
      ov7670_set(a, b); // read-back verified
#ifdef rp2350
    else
      sccb_write(a, b); // OV2640 regs may not read back; set the bank with REGISTER &HFF,n first
#endif
    MMPrintString("Register &H");
    PIntH(a);
    MMPrintString(" was &H");
    PIntH(c);
    MMPrintString(" now &H");
    PIntH(b);
    PRet();
  }
  else if ((tp = checkstring(cmdline, (unsigned char *)"CHANGE")))
  {
    int size = 0;
    unsigned char *cp = NULL;
    int totaldifference = 0, difference;
    if (!XCLK)
      error("Camera not open");
    getcsargs(&tp, 9);
    if (!(argc == 3 || argc == 5 || argc == 9))
      SyntaxError();
    ;
    int xs = 0, ys = 0;
    if (!XCLK)
      error("Camera not open");
    int scale = 1;
    int64_t *aint;
    size = parseintegerarray(argv[0], &aint, 1, 1, NULL, true, NULL);
    cp = (unsigned char *)aint;
    // get the two variables
    MMFLOAT *outdiff = findvar(argv[2], V_FIND);
    if (!(g_vartbl[g_VarIndex].type & T_NBR))
      StandardError(6);
    if (size < CameraWidth * CameraHeight / 8)
      error("Array too small");
    int picout = 0;
    if (argc >= 5)
    {
      scale = getint(argv[4], 1, HRes / CameraWidth);
      picout = 1;
      if (argc == 9)
      {
        xs = getint(argv[6], 0, HRes - 1);
        ys = getint(argv[8], 0, VRes - 1);
      }
    }
    // OV7670: switch to YUV so the first byte per pixel is luma. OV2640 stays in its
    // RGB565 preview mode (no per-call mode switch - keeps the change-detection loop
    // fast); its high RGB565 byte is a fine brightness proxy for motion detection.
    if (CameraType == CAM_OV7670)
      for (int i = 0; OV7670_yuv[i].reg <= OV7670_REG_LAST; i++)
        ov7670_set(OV7670_yuv[i].reg, OV7670_yuv[i].value);
#if defined(HDMI) && !defined(HDMICUTDOWN)
    // A full QVGA capture is too big for a fast temp buffer (GetTempMainMemory returns
    // slow PSRAM for a 150KB request, which garbles the bit-banged capture), so capture
    // it straight into the framebuffer (fast SRAM); it fits 1:1 at 640x480 with scale 1,
    // so the optional difference display can't overwrite the capture. QQVGA is small
    // enough for a fast temp buffer, which also keeps a scaled difference image from
    // overwriting the capture (separate buffer).
    char *buff = (CameraWidth == HRes && CameraHeight <= VRes)
                     ? (char *)WriteBuf
                     : GetTempMainMemory(CameraWidth * CameraHeight * 2);
#else
    char *buff = GetTempMainMemory(CameraWidth * CameraHeight * 2);
#endif
    char *k = buff;
    c.rgb = 0;
    disable_interrupts_pico();
    capture(buff);
    enable_interrupts_pico();
    char *linebuff = NULL;
    if (scale)
      linebuff = GetTempMainMemory(CameraWidth * 3 * scale);
    for (int y = ys; y < CameraHeight * scale + ys; y += scale)
    {
      int kk = 0;
      for (int x = 0; x < CameraWidth; x++)
      {
        c.rgbbytes[1] = *k++;
        c.rgbbytes[0] = *k++;
        if (c.rgbbytes[1] > *cp)
          difference = (c.rgbbytes[1] - *cp);
        else
          difference = (*cp - c.rgbbytes[1]);
        totaldifference += difference;
        *cp++ = c.rgbbytes[1];
        if (picout)
        {
          for (int r = 0; r < scale; r++)
          {
            linebuff[kk++] = difference;
            linebuff[kk++] = difference;
            linebuff[kk++] = difference;
          }
        }
      }
      if (picout)
      {
        for (int r = 0; r < scale; r++)
        {
          if (y + r < VRes)
          {
            int w = CameraWidth * scale;
            if (w > HRes - xs)
              w = HRes - xs;
            DrawBuffer(xs, y + r, xs + w - 1, y + r, (unsigned char *)linebuff);
          }
        }
      }
    }
    *outdiff = (MMFLOAT)totaldifference / ((MMFLOAT)CameraWidth * CameraHeight * 255.0) * 100.0;
  }
#ifdef rp2350 // CAMERA CAPTURE JPEG (OV2640 hardware JPEG to file) - RP2350 only
  else if ((tp = checkstring(cmdline, (unsigned char *)"CAPTURE JPEG")))
  {
    // Capture a 640x480 JPEG using the OV2640's hardware JPEG engine and save it to a file.
    if (!XCLK)
      error("Camera not open");
    if (CameraType != CAM_OV2640)
      error("JPEG capture requires the OV2640");
    getcsargs(&tp, 5);
    if (!(argc == 1 || argc == 3 || argc == 5))
      error("Filename [, resolution [, quality]] expected");
    // Optional 2nd arg: resolution keyword (default VGA). jpegmax sizes the non-HDMI
    // capture buffer; qsbase is the lowest QS (= best quality) that reliably fits at that
    // resolution (bigger frames need a higher QS to stay within the buffer).
    const OV7670_command *framesize = OV2640_640x480_JPEG;
    int jpegmax = 64 * 1024; // VGA
    int qsbase = 2;          // VGA HIGH-quality QS (QS=1 makes the OV2640 emit a blank frame)
    if (argc >= 3)           // resolution is argv[2] whether or not a quality arg (argv[4]) follows
    {
      if (checkstring(argv[2], (unsigned char *)"VGA"))
      {
        framesize = OV2640_640x480_JPEG;
        jpegmax = 64 * 1024;
        qsbase = 2;
      }
      else if (checkstring(argv[2], (unsigned char *)"SVGA"))
      {
        framesize = OV2640_800x600_JPEG;
        jpegmax = 96 * 1024;
        qsbase = 2;
      }
      else if (checkstring(argv[2], (unsigned char *)"XGA"))
      {
        framesize = OV2640_1024x768_JPEG;
        jpegmax = 128 * 1024;
        qsbase = 4;
      }
      else if (checkstring(argv[2], (unsigned char *)"SXGA"))
      {
        framesize = OV2640_1280x1024_JPEG;
        jpegmax = 160 * 1024;
        qsbase = 5;
      }
      else if (checkstring(argv[2], (unsigned char *)"UXGA"))
      {
        framesize = OV2640_1600x1200_JPEG;
        jpegmax = 200 * 1024;
        qsbase = 8;
      }
      else
        error("Resolution must be VGA, SVGA, XGA, SXGA or UXGA");
    }
    // Optional 3rd arg: quality HIGH/MEDIUM/LOW (default MEDIUM). The QS register (DSP 0x44)
    // is a quantization scale: LOWER = higher quality / bigger file. HIGH uses the per-
    // resolution qsbase (best that fits); MEDIUM/LOW raise QS (x2 / x4) for smaller files.
    int qsmult = 2; // MEDIUM
    if (argc == 5)
    {
      if (checkstring(argv[4], (unsigned char *)"HIGH"))
        qsmult = 1;
      else if (checkstring(argv[4], (unsigned char *)"MEDIUM"))
        qsmult = 2;
      else if (checkstring(argv[4], (unsigned char *)"LOW"))
        qsmult = 4;
      else
        error("Quality must be HIGH, MEDIUM or LOW");
    }
    int quality = qsbase * qsmult; // OV2640 QS value to write
    if (quality < 2)
      quality = 2; // QS=1 makes the OV2640 emit a valid but blank JPEG - never go below 2
    unsigned char *fname = getFstring(argv[0]);
    AppendDefaultExtension((char *)fname, ".jpg");
    // Open the output file FIRST - a bad path/unmountable drive then fails cleanly,
    // before the disruptive mode switch + capture.
    if (!InitSDCard())
      return;
    int fnbr = FindFreeFileNbr();
    if (!BasicFileOpen((char *)fname, fnbr, FA_WRITE | FA_CREATE_ALWAYS))
      return;
    // The bit-bang needs FAST internal SRAM - PSRAM is too slow and drops bytes, corrupting
    // the JPEG. On HDMI mode 4 the framebuffer (WriteBuf) is the fast scratch (capturing
    // trashes the screen; the next preview CAMERA CAPTURE restores it). Otherwise
    // GetSystemMemory best-efforts internal RAM and we reject a PSRAM result.
    int maxlen;
    uint8_t *buff;
    int allocated = 0;
#if defined(HDMI) && !defined(HDMICUTDOWN)
    if (DISPLAY_TYPE == SCREENMODE4)
    {
      buff = (uint8_t *)WriteBuf;
      maxlen = ScreenSize;
    }
    else
#endif
    {
      maxlen = jpegmax;
      buff = (uint8_t *)GetSystemMemory(maxlen);
      allocated = 1;
#ifdef rp2350
      if (PSRAMsize && (uint8_t *)buff >= (uint8_t *)PSRAMbase &&
          (uint8_t *)buff < (uint8_t *)PSRAMbase + PSRAMsize)
      {
        FreeMemory(buff);
        FileClose(fnbr);
        error("Not enough fast RAM for JPEG capture");
      }
#endif
    }
    // Snapshot the running preview's converged auto-exposure/gain (sensor bank) before the
    // mode switch, so we can freeze it for the JPEG. The resolution change resets the AEC,
    // which otherwise needs many frames to re-converge - a still taken immediately after a
    // motion trigger would be badly over/under exposed.
    sccb_write(0xff, 0x01);           // sensor bank
    int ae_gain = readregister(0x00); // AGC gain[7:0]
    int ae_aec = readregister(0x10);  // AEC[9:2]
    int ae_r45 = readregister(0x45);  // AEC[15:10] + AGC[9:8]
    // Switch the OV2640 to JPEG mode at the chosen resolution.
    load_camera_regs(OV2640_JPEG_INIT, 0);
    load_camera_regs(OV2640_YUV422, 0);
    load_camera_regs(OV2640_JPEG, 0);
    sccb_write(0xff, 0x01);
    sccb_write(0x15, 0x00);
    load_camera_regs(framesize, 0);
    // Freeze auto-exposure/gain at the snapshotted preview values - the JPEG is then
    // correctly exposed immediately, with no AEC re-convergence wait.
    sccb_write(0xff, 0x01);           // sensor bank
    sccb_write(0x13, 0xe0);           // AEC (bit0) + AGC (bit2) OFF, keep banding/fast-AEC bits
    sccb_write(0x00, ae_gain);        // restore gain[7:0]
    sccb_write(0x10, ae_aec);         // restore AEC[9:2]
    sccb_write(0x45, ae_r45);         // restore AEC[15:10] + AGC[9:8]
    sccb_write(0xff, 0x00);           // DSP bank
    sccb_write(0xd3, OV2640_DVP_CLK); // slow DVP PCLK so the bit-banged read stays byte-perfect (same as preview)
    sccb_write(0x44, quality);        // QS (DSP bank): lower = higher quality / bigger JPEG
    uSec(50000);                      // brief settle for the mode change (exposure is frozen - no AEC wait)
    disable_interrupts_pico();
    int n = capture_jpeg(buff, maxlen);
    enable_interrupts_pico();
    // Restore RGB565 QVGA preview mode (soft reset first - the JPEG init changed
    // resolution/format/windowing the OV2640_QVGA table alone doesn't undo).
    sccb_write(0xFF, 0x01);
    sccb_write(0x12, 0x80);
    uSec(50000);
    load_camera_regs(OV2640_QVGA, 0);
    // Trim to the JPEG SOI (FF D8) .. EOI (FF D9).
    int start = -1, end = -1;
    for (int i = 0; i < n - 1; i++)
      if (buff[i] == 0xFF && buff[i + 1] == 0xD8)
      {
        start = i;
        break;
      }
    if (start >= 0)
      for (int i = n - 2; i > start; i--)
        if (buff[i] == 0xFF && buff[i + 1] == 0xD9)
        {
          end = i + 2;
          break;
        }
    if (start < 0 || end < 0)
    {
      if (allocated)
        FreeMemory(buff);
      FileClose(fnbr); // leaves an empty file
      error("No JPEG frame captured");
    }
    FilePutData((char *)(buff + start), fnbr, end - start);
    FileClose(fnbr);
    if (allocated)
      FreeMemory(buff);
  }
#endif // rp2350 (CAMERA CAPTURE JPEG)
  else if ((tp = checkstring(cmdline, (unsigned char *)"CAPTURE")))
  {
    getcsargs(&tp, 5);
    int xs = 0, ys = 0;
    if (!XCLK)
      error("Camera not open");
#if !(defined(HDMI) && !defined(HDMICUTDOWN))
    // Simple CAPTURE draws the live image to the panel, so it needs a display whose
    // DrawBuffer routine renders at least RGB565 colour. Test the routine (the real
    // capability) rather than a panel-type number: direct SPI TFTs (DrawBufferSPI/SCR) and
    // SSD1963 panels (DrawBufferSSD1963, and DrawBuffer320 which upscales via it) are true
    // RGB565. Excluded: DrawBuffer16 (buffered framebuffer is 4-bit RGB121 / 16 colour),
    // DrawBuffer222 (RGB222), DrawBufferMEM332 (RGB332), DrawBuffer2 (mono) and the
    // not-set stub.
    if (!(DrawBuffer == DrawBufferSPI || DrawBuffer == DrawBufferSPISCR ||
          DrawBuffer == DrawBufferSSD1963 || DrawBuffer == DrawBuffer320))
      error("CAMERA CAPTURE needs a display with at least RGB565 colour");
#endif
    int scale = 1;
    if (argc >= 1)
    {
      scale = getint(argv[0], 1, HRes / CameraWidth);
      if (argc == 5)
      {
        xs = getint(argv[2], 0, HRes - 1);
        ys = getint(argv[4], 0, VRes - 1);
      }
      else if (argc == 3)
        SyntaxError();
      ;
    }
    if (CameraType == CAM_OV7670) // OV2640 is already RGB565 from its init table
      for (int i = 0; OV7670_rgb[i].reg <= OV7670_REG_LAST; i++)
        ov7670_set(OV7670_rgb[i].reg, OV7670_rgb[i].value);
    c.rgb = 0;
#if defined(HDMI) && !defined(HDMICUTDOWN)
    if (CameraWidth == HRes && CameraHeight <= VRes && xs == 0 && ys == 0)
    {
      // Direct path: capture RGB565 straight into the HDMI framebuffer (no temp
      // buffer), then convert RGB565->RGB555 in place. Requires the framebuffer row
      // stride to match the capture width (CameraWidth==HRes, i.e. QVGA into a
      // 640x480/mode-4 320x240 framebuffer) so each camera pixel lands on its own
      // framebuffer pixel with no stride mismatch. RGB565->RGB555 just drops
      // the green LSB. (The image shows wrong colours/tears during the IRQ-disabled
      // capture, then snaps right as the convert pass runs - the cost of drawing live.)
      disable_interrupts_pico();
      capture((char *)WriteBuf);
      enable_interrupts_pico();
      uint16_t *fb = (uint16_t *)WriteBuf;
      for (int y = 0; y < CameraHeight && y < VRes; y++)
      {
        uint16_t *row = fb + y * HRes;
        for (int x = 0; x < CameraWidth; x++)
        {
          // Column 0 is the OV7670 HREF-edge junk pixel - read pixel 1 instead (OV7670 only).
          // Only ever peeks forward, so converting left-to-right is safe in place.
          unsigned char *b = (unsigned char *)(row + ((x == 0 && CameraType == CAM_OV7670) ? 1 : x));
          uint16_t v = (b[0] << 8) | b[1];           // RGB565, high byte first from capture()
          row[x] = ((v & 0xFFC0) >> 1) | (v & 0x1F); // -> RGB555
        }
      }
    }
    else if (CameraWidth == 320)
    {
      // A full QVGA capture only has a fast buffer when it goes direct to the
      // framebuffer (full screen, 640x480). With an x,y offset it can't, and a
      // 150KB PSRAM temp is too slow for the bit-banged capture (garbled image).
      error("QVGA capture must fill the screen (no x,y offset; MODE 4 at 640x480)");
    }
    else
    {
      // QQVGA (160x120): small enough for a fast temp buffer, so capture there and
      // scale up / position into the framebuffer via DrawBufferFast (= DrawBuffer555Fast
      // in mode 4). e.g. scale 2 fills the 320x240 screen. RGB565->RGB555 drops green LSB.
      char *buff = GetTempMainMemory(CameraWidth * CameraHeight * 2);
      disable_interrupts_pico();
      capture(buff);
      enable_interrupts_pico();
      uint16_t *line555 = (uint16_t *)GetTempMainMemory(CameraWidth * scale * 2);
      char *k = buff;
      for (int y = ys; y < CameraHeight * scale + ys; y += scale)
      {
        int kk = 0;
        for (int x = 0; x < CameraWidth; x++)
        {
          uint16_t v = ((unsigned char)k[0] << 8) | (unsigned char)k[1]; // RGB565, high byte first
          uint16_t v555 = ((v & 0xFFC0) >> 1) | (v & 0x1F);
          k += 2;
          for (int r = 0; r < scale; r++)
            line555[kk++] = v555;
        }
        for (int r = 0; r < scale; r++)
        {
          if (y + r < VRes)
          {
            int w = CameraWidth * scale;
            if (w > HRes - xs)
              w = HRes - xs;
            DrawBufferFast(xs, y + r, xs + w - 1, y + r, -1, (unsigned char *)line555);
          }
        }
      }
    }
#else
    char *buff = GetTempMainMemory(CameraWidth * CameraHeight * 2);
    disable_interrupts_pico();
    capture(buff);
    enable_interrupts_pico();
    char *linebuff = GetTempMainMemory(CameraWidth * 3 * scale);
    char *k = buff;
    for (int y = ys; y < CameraHeight * scale + ys; y += scale)
    {
      int kk = 0;
      for (int x = 0; x < CameraWidth; x++)
      {
        // The first pixel of each QVGA line is junk (an OV7670 HREF-edge artifact
        // that can't be removed via the window registers). Show a copy of the
        // second pixel in column 0 instead of garbage (OV7670 only).
        char *px = (x == 0 && CameraWidth == 320 && CameraType == CAM_OV7670) ? k + 2 : k;
        c.rgbbytes[1] = px[0];
        c.rgbbytes[0] = px[1];
        k += 2;
        for (int r = 0; r < scale; r++)
        {
          linebuff[kk++] = (c.rgbbytes[0] & 0x1F) << 3;
          linebuff[kk++] = (c.rgb & 0x07E0) >> 3;
          linebuff[kk++] = (c.rgbbytes[1] & 0xF8);
        }
      }
      for (int r = 0; r < scale; r++)
      {
        if (y + r < VRes)
        {
          int w = CameraWidth * scale;
          if (w > HRes - xs)
            w = HRes - xs;
          DrawBuffer(xs, y + r, xs + w - 1, y + r, (unsigned char *)linebuff);
        }
      }
    }
#endif
  }
  else if (checkstring(cmdline, (unsigned char *)"CLOSE"))
  {
    cameraclose();
  }
  else
    SyntaxError();
  ;
}
#else
void MIPS16 cmd_camera(void)
{
  error("Camera not supported on this device");
}
#endif
#endif
#if PICOCALC
void MIPS16 CheckPicoCalcKeyboard(int noerror, int read)
{
  uint16_t buff = 0x0000;
  int i2cret = 0;
  static int ctrlheld = 0;
  I2C2_Sendlen = 1;       // send one byte
  I2C_Send_Buffer[0] = 9; // the first register to read
  I2C2_Addr = 0x1f;       // address of the device
  I2C2_Status = 0;
  I2C2_Timeout = (Option.SYSTEM_I2C_SLOW ? SystemI2CTimeout * 5 : SystemI2CTimeout);

  i2cret = i2c_write_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)I2C_Send_Buffer, I2C2_Sendlen, false, I2C2_Timeout * 1000);

  if (i2cret != I2C2_Sendlen)
  {
    buff = 0x0000;
    return;
  }

  sleep_ms(2);

  I2C2_Rcvlen = 2; // get 2 bytes
  buff = 0x0000;

  i2cret = i2c_read_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)&buff, I2C2_Rcvlen, false, I2C2_Timeout * 1000);
  if (i2cret != I2C2_Rcvlen)
  {
    buff = 0x0000;
    return;
  }

  if (buff == 0xA503)
  {
    ctrlheld = 0;
  }
  else if (buff == 0xA502)
  {
    ctrlheld = 1;
  }
  else if ((buff & 0xff) == 1)
  { // pressed
    int c = buff >> 8;
    int realc = 0;
    switch (c)
    {
    // Refer to Appendix H in PicoMite User Manual
    // PicoCalc must be mapped to expected PicoMite keys
    case 0xd4:
      realc = DEL;
      break;
    case 0xb5:
      realc = UP;
      break;
    case 0xb6:
      realc = DOWN;
      break;
    case 0xb4:
      realc = LEFT;
      break;
    case 0xb7:
      realc = RIGHT;
      break;
    case 0xd1:
      realc = INSERT;
      break; // ALT + I
    case 0xd2:
      realc = HOME;
      break; // SHIFT + TAB (collision, see below)
    case 0xd5:
      realc = END;
      break; // SHIFT + DEL (collision, see below)
    case 0xd6:
      realc = PUP;
      break; // SHIFT + UP
    case 0xd7:
      realc = PDOWN;
      break; // SHIFT + DOWN (collision, see below)
    case 0xa1:
      realc = ALT;
      break; // Note: SHIFT + ENTER also sends ALT!
    case 0x81:
      realc = F1;
      break;
    case 0x82:
      realc = F2;
      break;
    case 0x83:
      realc = F3;
      break;
    case 0x84:
      realc = F4;
      break;
    case 0x85:
      realc = F5;
      break;
    case 0x86:
      realc = F6;
      break; // SHIFT + F1
    case 0x87:
      realc = F7;
      break; // SHIFT + F2
    case 0x88:
      realc = F8;
      break; // SHIFT + F3
    case 0x89:
      realc = F9;
      break; // SHIFT + F4
    case 0x90:
      realc = F10;
      break; // SHIFT + F5
    // F11 not on PicoCalc
    // F12 not on PicoCalc
    // PrtScr/SysRq not on PicoCalc
    case 0xd0:
      realc = BreakKey;
      break;
    // SHIFT_TAB sends Home on PicoCalc
    // SHIFT_DEL sends End on PicoCalc
    // DOWNSEL (SHIFT_DOWN_ARROW) sends Page Down (PDOWN) on PicoCalc
    //   Note: (SHIFT_UP_ARROW) sends Page Up (PUP) on PicoCalc
    // RIGHTSEL (SHIFT_RIGHT_ARROW) sends nothing on PicoCalc
    //   Note: (SHIFT_LEFT_ARROW) sends nothing on PicoCalc
    // Note: PicoCalc cannot send shifted Fn keys!
    // --- Appendix H ends
    case 0xb1:
      realc = ESC;
      break;
    case 0x0a:
      realc = ENTER;
      break;
    // --- This will only work when using the custom BIOS at:
    //     https://github.com/shtirlic/picocalc_southbridge
    //     Official BIOS (currently 1.4) does not support this key.
    case 0x91:
      realc = 0x66;
      break; // USB_HID_KEYBOARD_KEYPAD_KEYBOARD_POWER
    // --- Modifier keys must be consumed and ignored!
    case 0xa2: // Shift (left)
    case 0xa3: // Shift (right)
    case 0xa5: // Ctrl
    case 0xc1: // CapsLK
      return;
    default:
      realc = c;
      break;
    }
    c = realc;

    if (c >= 'a' && c <= 'z' && ctrlheld)
      c = c - 'a' + 1;
    if (c == BreakKey)
    {                                      // if the user wants to stop the progran
      MMAbort = true;                      // set the flag for the interpreter to see
      ConsoleRxBufHead = ConsoleRxBufTail; // empty the buffer
                                           // break;
    }
    else
    {
      ConsoleRxBuf[ConsoleRxBufHead] = c; // store the byte in the ring buffer
      if (ConsoleRxBuf[ConsoleRxBufHead] == keyselect && KeyInterrupt != NULL)
      {
        Keycomplete = true;
      }
      else
      {
        ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE; // advance the head of the queue
        if (ConsoleRxBufHead == ConsoleRxBufTail)
        {                                                                  // if the buffer has overflowed
          ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE; // throw away the oldest char
        }
      }
    }
  }
  return;
}
void CheckKbdBacklight()
{
  int i2cret = 0;
  char buff[2];
  I2C2_Sendlen = 2;          // send two bytes^
  I2C2_Status = 0;           //
  I2C_Send_Buffer[0] = 0x0a; // the register + write bit
  I2C2_Addr = 0x1f;          // address of the device
  I2C2_Timeout = (Option.SYSTEM_I2C_SLOW ? SystemI2CTimeout * 5 : SystemI2CTimeout);
  ;

  i2cret = i2c_write_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)I2C_Send_Buffer, I2C2_Sendlen, false, I2C2_Timeout * 1000);
  if (i2cret != I2C2_Sendlen)
    return;

  sleep_ms(1);

  buff[0] = 0x00;

  i2cret = i2c_read_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)&buff, 2, false, I2C2_Timeout * 1000);
  if (i2cret == 2 && buff[0] == 0x0a)
  {
    if (Option.BACKLIGHT_KBD != buff[1])
    {
      Option.BACKLIGHT_KBD = buff[1];
      SaveOptions();
    }
  }
  return;
}

int set_kbd_backlight(uint8_t val)
{

  static uint16_t buff = 0x0000; // *EB*
  int i2cret = 0;
  I2C2_Sendlen = 2;          // send two bytes
  I2C2_Status = 0;           //
  I2C_Send_Buffer[0] = 0x8A; // the register + write bit
  I2C_Send_Buffer[1] = val;  // backlight value
  I2C2_Addr = 0x1f;          // address of the device
  I2C2_Timeout = (Option.SYSTEM_I2C_SLOW ? SystemI2CTimeout * 5 : SystemI2CTimeout);

  i2cret = i2c_write_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)I2C_Send_Buffer, I2C2_Sendlen, false, I2C2_Timeout * 1000);
  if (i2cret == PICO_ERROR_GENERIC || i2cret == PICO_ERROR_TIMEOUT)
  {
    return -1;
  }

  sleep_ms(2); // avoid overloading the bios

  I2C2_Rcvlen = 2; // get 2 bytes

  i2cret = i2c_read_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)&buff, I2C2_Rcvlen, false, I2C2_Timeout * 1000);
  if (i2cret == PICO_ERROR_GENERIC || i2cret == PICO_ERROR_TIMEOUT)
  {
    return -1;
  }

  if (buff != 0)
  {
    return buff;
  }
  return -1;
}
void CheckLcdBacklight()
{

  int i2cret = 0;
  char buff[2];
  I2C2_Sendlen = 1;          // send one byte
  I2C2_Status = 0;           //
  I2C_Send_Buffer[0] = 0x05; // the register to read
  I2C2_Addr = 0x1f;          // address of the device
  I2C2_Timeout = (Option.SYSTEM_I2C_SLOW ? SystemI2CTimeout * 5 : SystemI2CTimeout);

  i2cret = i2c_write_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)I2C_Send_Buffer, I2C2_Sendlen, false, I2C2_Timeout * 1000);
  if (i2cret != I2C2_Sendlen)
    return;

  sleep_ms(1);

  buff[0] = 0x00;
  i2cret = i2c_read_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)buff, 2, false, I2C2_Timeout * 1000);
  if (i2cret == 2 && buff[0] == 0x05)
  {
    if (Option.BACKLIGHT_LCD != buff[1])
    {
      Option.BACKLIGHT_LCD = buff[1];
      SaveOptions();
    }
  }
  return;
}
int set_lcd_backlight(uint8_t val)
{

  static uint16_t buff = 0x0000; // *EB*
  int i2cret = 0;
  I2C2_Sendlen = 2;          // send two bytes
  I2C2_Status = 0;           //
  I2C_Send_Buffer[0] = 0x85; // the register + write bit
  I2C_Send_Buffer[1] = val;  // backlight value
  I2C2_Addr = 0x1f;          // address of the device
  I2C2_Timeout = (Option.SYSTEM_I2C_SLOW ? SystemI2CTimeout * 5 : SystemI2CTimeout);

  i2cret = i2c_write_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)I2C_Send_Buffer, I2C2_Sendlen, false, I2C2_Timeout * 1000);
  if (i2cret == PICO_ERROR_GENERIC || I2C2_Status == PICO_ERROR_TIMEOUT)
  {
    // printf("set_lcd_backlight i2c write error\r\n");
    return -1;
  }

  sleep_ms(2); // avoid overloading the bios

  I2C2_Rcvlen = 2; // get 2 bytes

  i2cret = i2c_read_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)&buff, I2C2_Rcvlen, false, I2C2_Timeout * 1000);
  if (i2cret == PICO_ERROR_GENERIC || i2cret == PICO_ERROR_TIMEOUT)
  {
    // printf("set_lcd_backlight i2c read error read\r\n");
    return -1;
  }

  if (buff != 0)
  {
    return buff;
  }
  return -1;
}
int read_battery()
{

  static uint16_t buff = 0x0000; // *EB*
  static uint64_t rqtime;        // *EB*
  int i2cret = 0;
  I2C2_Sendlen = 1;          // send one byte
  I2C2_Status = 0;           //
  I2C_Send_Buffer[0] = 0x0b; // the register to read
  I2C2_Addr = 0x1f;          // address of the device
  I2C2_Timeout = (Option.SYSTEM_I2C_SLOW ? SystemI2CTimeout * 5 : SystemI2CTimeout);

  if (rqtime < time_us_64()) // *EB*
  {
    rqtime = time_us_64() + 2000000;

    i2cret = i2c_write_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)I2C_Send_Buffer, I2C2_Sendlen, false, I2C2_Timeout * 1000);
    if (i2cret == PICO_ERROR_GENERIC || i2cret == PICO_ERROR_TIMEOUT)
    {
      // printf("read_battery i2c write error\n");
      return -1;
    }

    sleep_ms(2); // avoid overloading the bios

    I2C2_Rcvlen = 2; // get 2 bytes
    buff = 0x0000;

    i2cret = i2c_read_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)&buff, I2C2_Rcvlen, false, I2C2_Timeout * 1000);
    if (i2cret == PICO_ERROR_GENERIC || i2cret == PICO_ERROR_TIMEOUT)
    {
      // printf("read_battery i2c read error read\n");
      return -1;
    }
  }

  if (buff != 0)
  {
    return buff;
  }
  return -1;
}
int read_biosversion()
{ // *EB*

  uint16_t buff = 0x0000; // *EB*
  int i2cret = 0;
  I2C2_Sendlen = 1;          // send one byte
  I2C2_Status = 0;           //
  I2C_Send_Buffer[0] = 0x01; // the register to read
  I2C2_Addr = 0x1f;          // address of the device
  I2C2_Timeout = (Option.SYSTEM_I2C_SLOW ? SystemI2CTimeout * 5 : SystemI2CTimeout);

  i2cret = i2c_write_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)I2C_Send_Buffer, I2C2_Sendlen, false, I2C2_Timeout * 1000);
  if (i2cret == PICO_ERROR_GENERIC || i2cret == PICO_ERROR_TIMEOUT)
  {
    // printf("read_biosversion i2c write error\n");
    return -1;
  }

  sleep_ms(2); // avoid overloading the bios

  I2C2_Rcvlen = 2; // get 2 bytes
  buff = 0x0000;   //

  i2cret = i2c_read_timeout_us(i2c1, (uint8_t)I2C2_Addr, (uint8_t *)&buff, I2C2_Rcvlen, false, I2C2_Timeout * 1000);
  if (i2cret == PICO_ERROR_GENERIC || i2cret == PICO_ERROR_TIMEOUT)
  {
    // printf("read_biosversion i2c read error read\n");
    return -1;
  }

  if (buff != 0)
  {
    return buff;
  }
  return -1;
}

#if PICOCALC // *EB*
/**
 * @brief Test if firmware is running on a PicoCalc by attempting I2C battery read
 *
 * This function is called during initialization to detect if the firmware is actually
 * running on a PicoCalc device (vs other PicoMite variants). It temporarily enables I2C,
 * attempts to read the battery monitor, and updates the platform setting if successful.
 */
void MIPS16 TestPicoCalc(void) // *EB*
{
  // Only run if platform is not already set to PicoCalc or something else
  if (*Option.platform)
    return;
  // Set the PicoCalc-specific I2C pins (9 for SDA, 10 for SCL)
  int sda_pin = PinDef[9].GPno;
  int scl_pin = PinDef[10].GPno;

  // If pins are not configured, cannot test
  if (sda_pin == 0 || scl_pin == 0)
    return;

  // Initialize the GPIO pins for I2C
  gpio_init(sda_pin);
  gpio_init(scl_pin);
  gpio_set_function(sda_pin, GPIO_FUNC_I2C);
  gpio_set_function(scl_pin, GPIO_FUNC_I2C);
  gpio_pull_up(sda_pin);
  gpio_pull_up(scl_pin);

  // Initialize I2C1 with standard speed
  i2c_init(i2c1, 100 * 1000); // 100 kHz

  // Give the I2C bus time to settle
  sleep_ms(10);

  // Try to read battery to verify I2C is working
  int battery_result = read_battery();

  // Deinitialize I2C
  i2c_deinit(i2c1);

  // Return pins to uninitialized state
  gpio_deinit(sda_pin);
  gpio_deinit(scl_pin);

  // If battery read succeeded, we're on a PicoCalc
  if (battery_result > 0)
  {
    configure((unsigned char *)"PICOCALC", true);
  }
}
#endif // *EB*

#endif