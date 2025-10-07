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
 * @author Geoff Graham, Peter Mather
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
void i2c_masterCommand(int timer, unsigned char *buff);
void i2cCheck(unsigned char *p);
void i2c2Enable(unsigned char *p);
void i2c2Disable(unsigned char *p);
void i2c2Send(unsigned char *p);
void i2c2Receive(unsigned char *p);
void i2c2_disable(void);
void i2c2_enable(int bps);
void i2c2_masterCommand(int timer, unsigned char *buff);
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
volatile unsigned int I2C_Status;          // status flags
int mmI2Cvalue;
// value of MM.I2C
static MMFLOAT *I2C2_Rcvbuf_Float;         // pointer to the master receive buffer for a MMFLOAT
static long long int *I2C2_Rcvbuf_Int;     // pointer to the master receive buffer for an integer
static char *I2C2_Rcvbuf_String;           // pointer to the master receive buffer for a string
static unsigned int I2C2_Addr;             // I2C device address
static volatile unsigned int I2C2_Sendlen; // length of the master send buffer
static volatile unsigned int I2C2_Rcvlen;  // length of the master receive buffer
// static unsigned char I2C_Send_Buffer[256];                                   // I2C send buffer
bool I2C2_enabled = false;         // I2C enable marker
unsigned int I2C2_Timeout;         // master timeout value
volatile unsigned int I2C2_Status; // status flags
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
void I2C_Send_Command(char command)
{
  int i2cret;
  int i2caddr = SSD1306_I2C_Addr;
  I2C_Send_Buffer[0] = 0;
  I2C_Send_Buffer[1] = command;
  I2C_Sendlen = 2;
  I2C_Timeout = 1000;
  if (I2C1locked)
    i2cret = i2c_write_timeout_us(i2c1, (uint8_t)i2caddr, (uint8_t *)I2C_Send_Buffer, I2C_Sendlen, false, I2C_Timeout * 1000);
  else
    i2cret = i2c_write_timeout_us(i2c0, (uint8_t)i2caddr, (uint8_t *)I2C_Send_Buffer, I2C_Sendlen, false, I2C_Timeout * 1000);
  mmI2Cvalue = 0;
  if (i2cret == PICO_ERROR_GENERIC)
    mmI2Cvalue = 1;
  if (i2cret == PICO_ERROR_TIMEOUT)
    mmI2Cvalue = 2;
  //	mmI2Cvalue=HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)i2caddr, I2C_Send_Buffer, I2C_Sendlen, I2C_Timeout);
}
void I2C_Send_Data(unsigned char *data, int n)
{
  int i2cret;
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
  mmI2Cvalue = 0;
  if (i2cret == PICO_ERROR_GENERIC)
    mmI2Cvalue = 1;
  if (i2cret == PICO_ERROR_TIMEOUT)
    mmI2Cvalue = 2;
}
#ifndef PICOMITEVGA
void ConfigDisplayI2C(unsigned char *p)
{
  unsigned char DISPLAY_TYPE = 0;
  getcsargs(&p, 5);
  if (!(argc == 3 || argc == 5))
    error("Argument count");
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
    error("SYSTEM I2C not configured");
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
  I2C_Send_Command(0xAE); // DISPLAYOFF

  I2C_Send_Command(0xD5); // DISPLAYCLOCKDIV
  I2C_Send_Command(0xF0); // the suggested ratio &H80

  I2C_Send_Command(0xA8); // MULTIPLEX
  if (Option.DISPLAY_TYPE == SSD1306I2C)
    I2C_Send_Command(0x3F);
  else if (Option.DISPLAY_TYPE == SSD1306I2C32)
    I2C_Send_Command(0x1F);

  I2C_Send_Command(0xD3); // DISPLAYOFFSET
  I2C_Send_Command(0x0);  // no offset

  I2C_Send_Command(0x40); // STARTLINE

  I2C_Send_Command(0x8D); // CHARGEPUMP
  I2C_Send_Command(0x14);

  I2C_Send_Command(0x20); // MEMORYMODE
  I2C_Send_Command(0x00); //&H0 act like ks0108

  I2C_Send_Command(0xA1); // SEGREMAP OR 1
  I2C_Send_Command(0xC8); // COMSCANDEC

  I2C_Send_Command(0xDA); // COMPINS
  if (Option.DISPLAY_TYPE == SSD1306I2C)
    I2C_Send_Command(0x12);
  else if (Option.DISPLAY_TYPE == SSD1306I2C32)
    I2C_Send_Command(0x02);

  I2C_Send_Command(0x81); // SETCONTRAST
  I2C_Send_Command(0xCF);

  I2C_Send_Command(0xd9); // SETPRECHARGE
  I2C_Send_Command(0x22);

  I2C_Send_Command(0xDB); // VCOMDETECT
  I2C_Send_Command(0x20);

  I2C_Send_Command(0xA4); // DISPLAYALLON_RESUME
  I2C_Send_Command(0xA6); // NORMALDISPLAY
  I2C_Send_Command(0xAF); // DISPLAYON
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
    error("Unknown command");
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
    error("Unknown command");
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
    error("Argument count");
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
    error("Argument count");
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
  if (I2C0locked)
  {
    I2C_Addr = addr; // address of the device
    i2c_masterCommand(1, buff);
  }
  else
  {
    I2C2_Addr = addr; // address of the device
    i2c2_masterCommand(1, buff);
  }
  return !mmI2Cvalue;
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

void RtcGetTime(int noerror)
{
  char *buff = GetTempMemory(STRINGSIZE); // Received data is stored here
  int DS1307;
  clocktimer = (1000 * 60 * 60);
  if (I2C0locked)
  {
    I2C_Sendlen = 1; // send one byte
    I2C_Rcvlen = 0;
    I2C_Status = 0;
    I2C_Send_Buffer[0] = 0; // the first register to read
    if (!(DS1307 = DoRtcI2C(0x68, NULL)))
    {
      I2C_Send_Buffer[0] = 2; // the first register is different for the PCF8563
      if (!DoRtcI2C(0x51, NULL))
        goto error_exit;
    }
    I2C_Rcvbuf_String = buff; // we want a string of bytes
    I2C_Rcvbuf_Float = NULL;
    I2C_Rcvbuf_Int = NULL;
    I2C_Rcvlen = 7; // get 7 bytes
    I2C_Sendlen = 0;
    if (!DoRtcI2C(DS1307 ? 0x68 : 0x51, (unsigned char *)buff))
      goto error_exit;
  }
  else
  {
    I2C2_Sendlen = 1; // send one byte
    I2C2_Rcvlen = 0;
    I2C2_Status = 0;
    I2C_Send_Buffer[0] = 0; // the first register to read
    if (!(DS1307 = DoRtcI2C(0x68, NULL)))
    {
      I2C_Send_Buffer[0] = 2; // the first register is different for the PCF8563
      if (!DoRtcI2C(0x51, NULL))
        goto error_exit;
    }
    I2C2_Rcvbuf_String = buff; // we want a string of bytes
    I2C2_Rcvbuf_Float = NULL;
    I2C2_Rcvbuf_Int = NULL;
    I2C2_Rcvlen = 7; // get 7 bytes
    I2C2_Sendlen = 0;
    if (!DoRtcI2C(DS1307 ? 0x68 : 0x51, (unsigned char *)buff))
      goto error_exit;
  }
  //    mT4IntEnable(0);
  int year, month, day, hour, minute, second;
  second = ((buff[0] & 0x7f) >> 4) * 10 + (buff[0] & 0x0f);
  minute = ((buff[1] & 0x7f) >> 4) * 10 + (buff[1] & 0x0f);
  hour = ((buff[2] & 0x3f) >> 4) * 10 + (buff[2] & 0x0f);
  day = ((buff[DS1307 ? 4 : 3] & 0x3f) >> 4) * 10 + (buff[DS1307 ? 4 : 3] & 0x0f);
  month = ((buff[5] & 0x1f) >> 4) * 10 + (buff[5] & 0x0f);
  year = (buff[6] >> 4) * 10 + (buff[6] & 0x0f) + 2000;
  //    mT4IntEnable(1);
  TimeOffsetToUptime = get_epoch(year, month, day, hour, minute, second) - time_us_64() / 1000000;
  return;

error_exit:
  if (noerror)
  {
    noRTC = 1;
    return;
  }
  if (CurrentLinePtr)
    error("RTC not responding");
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
  char buff[7]; // Received data is stored here
  int DS1307;
  unsigned char *p;
  void *ptr = NULL;
  if (!(I2C0locked || I2C1locked))
    error("SYSTEM I2C not configured");
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
        error("RTC not responding");
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
    getcsargs(&p, 11);
    if (I2C0locked)
    {
      if (argc == 1)
      {
        // single argument - assume the data is in DATETIME2 format used by GUI FORMATBOX
        p = getCstring(argv[0]);
        if (!(p[2] == '/' || p[2] == '-') || !(p[11] == ':' || p[13] == ':'))
          error("Date/time format");
        if (p[13] == ':')
          Fulldate = 2;
        if (p[14 + Fulldate] == ':')
          I2C_Send_Buffer[1] = CvtCharsToBCD(p + 15 + Fulldate, 0, 59); // seconds
        else
          I2C_Send_Buffer[1] = 0;                                     // seconds defaults to zero
        I2C_Send_Buffer[2] = CvtCharsToBCD(p + 12 + Fulldate, 0, 59); // minutes
        I2C_Send_Buffer[3] = CvtCharsToBCD(p + 9 + Fulldate, 0, 23);  // hour
        I2C_Send_Buffer[5] = CvtCharsToBCD(p, 1, 31);                 // day
        I2C_Send_Buffer[6] = CvtCharsToBCD(p + 3, 1, 12);             // month
        I2C_Send_Buffer[7] = CvtCharsToBCD(p + 6 + Fulldate, 0, 99);  // year
      }
      else
      {
        // multiple arguments - data should be in the original yy, mm, dd, etc format
        if (argc != 11)
          error("Argument count");
        I2C_Send_Buffer[1] = CvtToBCD(argv[10], 0, 59);  // seconds
        I2C_Send_Buffer[2] = CvtToBCD(argv[8], 0, 59);   // minutes
        I2C_Send_Buffer[3] = CvtToBCD(argv[6], 0, 23);   // hour
        I2C_Send_Buffer[5] = CvtToBCD(argv[4], 1, 31);   // day
        I2C_Send_Buffer[6] = CvtToBCD(argv[2], 1, 12);   // month
        I2C_Send_Buffer[7] = CvtToBCD(argv[0], 0, 2099); // year
      }
      I2C_Send_Buffer[0] = 0; // turn off the square wave
      I2C_Send_Buffer[4] = 1;
      I2C_Rcvlen = 0;
      I2C_Sendlen = 9; // send 7 bytes
      if (!DoRtcI2C(0x68, NULL))
      {
        I2C_Send_Buffer[9] = I2C_Send_Buffer[7]; // year
        I2C_Send_Buffer[8] = I2C_Send_Buffer[6]; // month
        I2C_Send_Buffer[7] = 1;
        I2C_Send_Buffer[6] = I2C_Send_Buffer[5];                          // day
        I2C_Send_Buffer[5] = I2C_Send_Buffer[3];                          // hour
        I2C_Send_Buffer[4] = I2C_Send_Buffer[2];                          // minutes
        I2C_Send_Buffer[3] = I2C_Send_Buffer[1];                          // seconds
        I2C_Send_Buffer[0] = I2C_Send_Buffer[1] = I2C_Send_Buffer[2] = 0; // set the register pointer to the first register then zero the first two registers
        I2C_Sendlen = 10;                                                 // send 10 bytes
        if (!DoRtcI2C(0x51, NULL))
          error("RTC not responding");
      }
    }
    else
    {
      if (argc == 1)
      {
        // single argument - assume the data is in DATETIME2 format used by GUI FORMATBOX
        p = getCstring(argv[0]);
        if (!(p[2] == '/' || p[2] == '-') || !(p[11] == ':' || p[13] == ':'))
          error("Date/time format");
        if (p[13] == ':')
          Fulldate = 2;
        if (p[14 + Fulldate] == ':')
          I2C_Send_Buffer[1] = CvtCharsToBCD(p + 15 + Fulldate, 0, 59); // seconds
        else
          I2C_Send_Buffer[1] = 0;                                     // seconds defaults to zero
        I2C_Send_Buffer[2] = CvtCharsToBCD(p + 12 + Fulldate, 0, 59); // minutes
        I2C_Send_Buffer[3] = CvtCharsToBCD(p + 9 + Fulldate, 0, 23);  // hour
        I2C_Send_Buffer[5] = CvtCharsToBCD(p, 1, 31);                 // day
        I2C_Send_Buffer[6] = CvtCharsToBCD(p + 3, 1, 12);             // month
        I2C_Send_Buffer[7] = CvtCharsToBCD(p + 6 + Fulldate, 0, 99);  // year
      }
      else
      {
        // multiple arguments - data should be in the original yy, mm, dd, etc format
        if (argc != 11)
          error("Argument count");
        I2C_Send_Buffer[1] = CvtToBCD(argv[10], 0, 59);  // seconds
        I2C_Send_Buffer[2] = CvtToBCD(argv[8], 0, 59);   // minutes
        I2C_Send_Buffer[3] = CvtToBCD(argv[6], 0, 23);   // hour
        I2C_Send_Buffer[5] = CvtToBCD(argv[4], 1, 31);   // day
        I2C_Send_Buffer[6] = CvtToBCD(argv[2], 1, 12);   // month
        I2C_Send_Buffer[7] = CvtToBCD(argv[0], 0, 2099); // year
      }
      I2C_Send_Buffer[0] = 0; // turn off the square wave
      I2C_Send_Buffer[4] = 1;
      I2C2_Rcvlen = 0;
      I2C2_Sendlen = 9; // send 7 bytes
      if (!DoRtcI2C(0x68, NULL))
      {
        I2C_Send_Buffer[9] = I2C_Send_Buffer[7]; // year
        I2C_Send_Buffer[8] = I2C_Send_Buffer[6]; // month
        I2C_Send_Buffer[7] = 1;
        I2C_Send_Buffer[6] = I2C_Send_Buffer[5];                          // day
        I2C_Send_Buffer[5] = I2C_Send_Buffer[3];                          // hour
        I2C_Send_Buffer[4] = I2C_Send_Buffer[2];                          // minutes
        I2C_Send_Buffer[3] = I2C_Send_Buffer[1];                          // seconds
        I2C_Send_Buffer[0] = I2C_Send_Buffer[1] = I2C_Send_Buffer[2] = 0; // set the register pointer to the first register then zero the first two registers
        I2C2_Sendlen = 10;                                                // send 10 bytes
        if (!DoRtcI2C(0x51, NULL))
          error("RTC not responding");
      }
    }
    RtcGetTime(0);
  }
  else if ((p = checkstring(cmdline, (unsigned char *)"GETREG")) != NULL)
  {
    getcsargs(&p, 3);
    if (argc != 3)
      error("Argument count");
    if (I2C0locked)
    {
      I2C_Sendlen = 1; // send one byte
      I2C_Rcvlen = 0;
      *I2C_Send_Buffer = getint(argv[0], 0, 255); // the register to read
    }
    else
    {
      I2C2_Sendlen = 1; // send one byte
      I2C2_Rcvlen = 0;
      *I2C_Send_Buffer = getint(argv[0], 0, 255); // the register to read
    }
    ptr = findvar(argv[2], V_FIND);
    if (g_vartbl[g_VarIndex].type & T_CONST)
      error("Cannot change a constant");
    if (g_vartbl[g_VarIndex].type & T_STR)
      error("Invalid variable");

    if (!(DS1307 = DoRtcI2C(0x68, NULL)))
    {
      if (!DoRtcI2C(0x51, NULL))
        error("RTC not responding");
    }
    if (I2C0locked)
    {
      I2C_Rcvbuf_String = buff; // we want a string of bytes
      I2C_Rcvbuf_Float = NULL;
      I2C_Rcvbuf_Int = NULL;
      I2C_Rcvlen = 1; // get 1 byte
      I2C_Sendlen = 0;
    }
    else
    {
      I2C2_Rcvbuf_String = buff; // we want a string of bytes
      I2C2_Rcvbuf_Float = NULL;
      I2C2_Rcvbuf_Int = NULL;
      I2C2_Rcvlen = 1; // get 1 byte
      I2C2_Sendlen = 0;
    }
    if (!DoRtcI2C(DS1307 ? 0x68 : 0x51, (unsigned char *)buff))
      error("RTC not responding1");
    if (g_vartbl[g_VarIndex].type & T_NBR)
      *(MMFLOAT *)ptr = buff[0];
    else
      *(long long int *)ptr = buff[0];
  }
  else if ((p = checkstring(cmdline, (unsigned char *)"SETREG")) != NULL)
  {
    getcsargs(&p, 3);
    if (argc != 3)
      error("Argument count");
    if (I2C0locked)
    {
      I2C_Rcvlen = 0;
      I2C_Send_Buffer[0] = getint(argv[0], 0, 255); // set the register pointer
      I2C_Send_Buffer[1] = getint(argv[2], 0, 255); // and the data to be written
      I2C_Sendlen = 2;                              // send 2 bytes
    }
    else
    {
      I2C2_Rcvlen = 0;
      I2C_Send_Buffer[0] = getint(argv[0], 0, 255); // set the register pointer
      I2C_Send_Buffer[1] = getint(argv[2], 0, 255); // and the data to be written
      I2C2_Sendlen = 2;                             // send 2 bytes
    }
    if (!DoRtcI2C(0x68, NULL))
    {
      if (!DoRtcI2C(0x51, NULL))
        error("RTC not responding");
    }
  }
  else
    error("Unknown command");
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
    error("Invalid syntax");
  speed = getinteger(argv[0]);
  if (!(speed == 100 || speed == 400 || speed == 1000))
    error("Valid speeds 100, 400, 1000");
  timeout = getinteger(argv[2]);
  if (timeout < 0 || (timeout > 0 && timeout < 100))
    error("Number out of bounds");
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
    error("Invalid syntax");
  speed = getinteger(argv[0]);
  if (!(speed == 100 || speed == 400 || speed == 1000))
    error("Valid speeds 100, 400, 1000");
  timeout = getinteger(argv[2]);
  if (timeout < 0 || (timeout > 0 && timeout < 100))
    error("Number out of bounds");
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
  void *ptr = NULL;
  unsigned char *cptr = NULL;

  getcsargs(&p, MAX_ARG_COUNT);
  if (!(argc & 0x01) || (argc < 7))
    error("Invalid syntax");
  if (!I2C_enabled)
    error("I2C not open");
  addr = getinteger(argv[0]);
  i2c_options = getinteger(argv[2]);
  if (i2c_options < 0 || i2c_options > 3)
    error("Number out of bounds");
  I2C_Status = 0;
  if (i2c_options & 0x01)
    I2C_Status = I2C_Status_BusHold;
  I2C_Addr = addr;
  sendlen = getint(argv[4], 1, 256);

  if (sendlen == 1 || argc > 7)
  { // numeric expressions for data
    if (sendlen != ((argc - 5) >> 1))
      error("Incorrect argument count");
    for (i = 0; i < sendlen; i++)
    {
      I2C_Send_Buffer[i] = getinteger(argv[i + i + 6]);
    }
  }
  else
  { // an array of MMFLOAT, integer or a string
    ptr = findvar(argv[6], V_NOFIND_NULL | V_EMPTY_OK);
    if (ptr == NULL)
      error("Invalid variable");
    if ((g_vartbl[g_VarIndex].type & T_STR) && g_vartbl[g_VarIndex].dims[0] == 0)
    { // string
      if (sendlen > 255)
        error("Number out of bounds");
      cptr = (unsigned char *)ptr;
      cptr++; // skip the length byte in a MMBasic string
      for (i = 0; i < sendlen; i++)
      {
        I2C_Send_Buffer[i] = (int)(*(cptr + i));
      }
    }
    else if ((g_vartbl[g_VarIndex].type & T_NBR) && g_vartbl[g_VarIndex].dims[0] > 0 && g_vartbl[g_VarIndex].dims[1] == 0)
    { // numeric array
      if ((((MMFLOAT *)ptr - g_vartbl[g_VarIndex].val.fa) + sendlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
      {
        error("Insufficient data");
      }
      else
      {
        for (i = 0; i < sendlen; i++)
        {
          I2C_Send_Buffer[i] = (int)(*((MMFLOAT *)ptr + i));
        }
      }
    }
    else if ((g_vartbl[g_VarIndex].type & T_INT) && g_vartbl[g_VarIndex].dims[0] > 0 && g_vartbl[g_VarIndex].dims[1] == 0)
    { // integer array
      if ((((long long int *)ptr - g_vartbl[g_VarIndex].val.ia) + sendlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
      {
        error("Insufficient data");
      }
      else
      {
        for (i = 0; i < sendlen; i++)
        {
          I2C_Send_Buffer[i] = (int)(*((long long int *)ptr + i));
        }
      }
    }
    else
      error("Invalid variable");
  }
  I2C_Sendlen = sendlen;
  I2C_Rcvlen = 0;

  i2c_masterCommand(1, NULL);
}
// send data to an I2C slave - master mode
void i2cSendSlave(unsigned char *p, int channel)
{
  int sendlen, i;
  void *ptr = NULL;
  unsigned char *cptr = NULL;
  getcsargs(&p, MAX_ARG_COUNT);
  if (!(argc >= 3))
    error("Invalid syntax");
  if (!((I2C_Status & I2C_Status_Slave && channel == 0) || (I2C2_Status & I2C_Status_Slave && channel == 1)))
    error("I2C slave not open");
  unsigned char *bbuff;
  if (channel == 0)
  {
    bbuff = I2C_Send_Buffer;
  }
  else
  {
    bbuff = I2C_Send_Buffer;
  }
  sendlen = getinteger(argv[0]);
  if (sendlen < 1 || sendlen > 255)
    error("Number out of bounds");

  if (sendlen == 1 || argc > 3)
  { // numeric expressions for data
    if (sendlen != ((argc - 1) >> 1))
      error("Incorrect argument count");
    for (i = 0; i < sendlen; i++)
    {
      bbuff[i] = getinteger(argv[i + i + 2]);
    }
  }
  else
  { // an array of MMFLOAT, integer or a string
    ptr = findvar(argv[2], V_NOFIND_NULL | V_EMPTY_OK);
    if (ptr == NULL)
      error("Invalid variable");
    if ((g_vartbl[g_VarIndex].type & T_STR) && g_vartbl[g_VarIndex].dims[0] == 0)
    { // string
      cptr = (unsigned char *)ptr;
      cptr++; // skip the length byte in a MMBasic string
      for (i = 0; i < sendlen; i++)
      {
        bbuff[i] = (int)(*(cptr + i));
      }
    }
    else if ((g_vartbl[g_VarIndex].type & T_NBR) && g_vartbl[g_VarIndex].dims[0] > 0 && g_vartbl[g_VarIndex].dims[1] == 0)
    { // numeric array
      if ((((MMFLOAT *)ptr - g_vartbl[g_VarIndex].val.fa) + sendlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
      {
        error("Insufficient data");
      }
      else
      {
        for (i = 0; i < sendlen; i++)
        {
          bbuff[i] = (int)(*((MMFLOAT *)ptr + i));
        }
      }
    }
    else if ((g_vartbl[g_VarIndex].type & T_INT) && g_vartbl[g_VarIndex].dims[0] > 0 && g_vartbl[g_VarIndex].dims[1] == 0)
    { // integer array
      if ((((long long int *)ptr - g_vartbl[g_VarIndex].val.ia) + sendlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
      {
        error("Insufficient data");
      }
      else
      {
        for (i = 0; i < sendlen; i++)
        {
          bbuff[i] = (int)(*((long long int *)ptr + i));
        }
      }
    }
    else
      error("Invalid variable");
  }
  if (channel == 0)
    i2c_write_raw_blocking(i2c0, bbuff, sendlen);
  else
    i2c_write_raw_blocking(i2c1, bbuff, sendlen);
}
// send data to an I2C slave - master mode
void i2c2Send(unsigned char *p)
{
  int addr, i2c2_options, sendlen, i;
  void *ptr = NULL;
  unsigned char *cptr = NULL;

  getcsargs(&p, MAX_ARG_COUNT);
  if (!(argc & 0x01) || (argc < 7))
    error("Invalid syntax");
  if (!I2C2_enabled)
    error("I2C not open");
  addr = getinteger(argv[0]);
  i2c2_options = getinteger(argv[2]);
  if (i2c2_options < 0 || i2c2_options > 3)
    error("Number out of bounds");
  I2C2_Status = 0;
  if (i2c2_options & 0x01)
    I2C2_Status = I2C_Status_BusHold;
  I2C2_Addr = addr;
  sendlen = getint(argv[4], 1, 256);

  if (sendlen == 1 || argc > 7)
  { // numeric expressions for data
    if (sendlen != ((argc - 5) >> 1))
      error("Incorrect argument count");
    for (i = 0; i < sendlen; i++)
    {
      I2C_Send_Buffer[i] = getinteger(argv[i + i + 6]);
    }
  }
  else
  { // an array of MMFLOAT, integer or a string
    ptr = findvar(argv[6], V_NOFIND_NULL | V_EMPTY_OK);
    if (ptr == NULL)
      error("Invalid variable");
    if ((g_vartbl[g_VarIndex].type & T_STR) && g_vartbl[g_VarIndex].dims[0] == 0)
    { // string
      if (sendlen > 255)
        error("Number out of bounds");
      cptr = (unsigned char *)ptr;
      cptr++; // skip the length byte in a MMBasic string
      for (i = 0; i < sendlen; i++)
      {
        I2C_Send_Buffer[i] = (int)(*(cptr + i));
      }
    }
    else if ((g_vartbl[g_VarIndex].type & T_NBR) && g_vartbl[g_VarIndex].dims[0] > 0 && g_vartbl[g_VarIndex].dims[1] == 0)
    { // numeric array
      if ((((MMFLOAT *)ptr - g_vartbl[g_VarIndex].val.fa) + sendlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
      {
        error("Insufficient data");
      }
      else
      {
        for (i = 0; i < sendlen; i++)
        {
          I2C_Send_Buffer[i] = (int)(*((MMFLOAT *)ptr + i));
        }
      }
    }
    else if ((g_vartbl[g_VarIndex].type & T_INT) && g_vartbl[g_VarIndex].dims[0] > 0 && g_vartbl[g_VarIndex].dims[1] == 0)
    { // integer array
      if ((((long long int *)ptr - g_vartbl[g_VarIndex].val.ia) + sendlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
      {
        error("Insufficient data");
      }
      else
      {
        for (i = 0; i < sendlen; i++)
        {
          I2C_Send_Buffer[i] = (int)(*((long long int *)ptr + i));
        }
      }
    }
    else
      error("Invalid variable");
  }
  I2C2_Sendlen = sendlen;
  I2C2_Rcvlen = 0;

  i2c2_masterCommand(1, NULL);
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
  int addr, i2c_options, rcvlen;
  void *ptr = NULL;
  getcsargs(&p, 7);
  if (argc != 7)
    error("Invalid syntax");
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
    error("Number out of bounds");
  ptr = findvar(argv[6], V_FIND | V_EMPTY_OK);
  if (g_vartbl[g_VarIndex].type & T_CONST)
    error("Cannot change a constant");
  if (ptr == NULL)
    error("Invalid variable");
  if (g_vartbl[g_VarIndex].type & T_NBR)
  {
    if (g_vartbl[g_VarIndex].dims[1] != 0)
      error("Invalid variable");
    if (g_vartbl[g_VarIndex].dims[0] <= 0)
    { // Not an array
      if (rcvlen != 1)
        error("Invalid variable");
    }
    else
    { // An array
      if ((((MMFLOAT *)ptr - g_vartbl[g_VarIndex].val.fa) + rcvlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
        error("Insufficient space in array");
    }
    I2C_Rcvbuf_Float = (MMFLOAT *)ptr;
  }
  else if (g_vartbl[g_VarIndex].type & T_INT)
  {
    if (g_vartbl[g_VarIndex].dims[1] != 0)
      error("Invalid variable");
    if (g_vartbl[g_VarIndex].dims[0] <= 0)
    { // Not an array
      if (rcvlen != 1)
        error("Invalid variable");
    }
    else
    { // An array
      if ((((long long int *)ptr - g_vartbl[g_VarIndex].val.ia) + rcvlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
        error("Insufficient space in array");
    }
    I2C_Rcvbuf_Int = (long long int *)ptr;
  }
  else if (g_vartbl[g_VarIndex].type & T_STR)
  {
    if (rcvlen < 1 || rcvlen > 255)
      error("Number out of bounds");
    if (g_vartbl[g_VarIndex].dims[0] != 0)
      error("Invalid variable");
    *(char *)ptr = rcvlen;
    I2C_Rcvbuf_String = (char *)ptr + 1;
  }
  else
    error("Invalid variable");
  I2C_Rcvlen = rcvlen;

  I2C_Sendlen = 0;
  char *buff = GetTempMemory(rcvlen > 255 ? rcvlen + 2 : STRINGSIZE);
  //	PInt((uint32_t)I2C_Rcvbuf_String);
  i2c_masterCommand(1, (unsigned char *)buff);
  //	PIntComma(rcvlen);
  //	PInt((uint32_t)I2C_Rcvbuf_String);PRet();
  //	if(g_vartbl[g_VarIndex].type & T_STR)*(char *)ptr = rcvlen;
}
void i2cReceiveSlave(unsigned char *p, int channel)
{
  int rcvlen;
  void *ptr = NULL;
  MMFLOAT *rcvdlenFloat = NULL;
  long long int *rcvdlenInt = NULL;
  int count = 1;
  I2C_Rcvbuf_Float = NULL;
  I2C_Rcvbuf_Int = NULL;
  I2C_Rcvbuf_String = NULL;
  I2C2_Rcvbuf_Float = NULL;
  I2C2_Rcvbuf_Int = NULL;
  I2C2_Rcvbuf_String = NULL;
  getcsargs(&p, 5);
  if (argc != 5)
    error("Invalid syntax");
  if (!((I2C_Status & I2C_Status_Slave && channel == 0) || (I2C2_Status & I2C_Status_Slave && channel == 1)))
    error("I2C slave not open");
  rcvlen = getinteger(argv[0]);
  if (rcvlen < 1 || rcvlen > 255)
    error("Number out of bounds");
  ptr = findvar(argv[2], V_FIND | V_EMPTY_OK);
  if (g_vartbl[g_VarIndex].type & T_CONST)
    error("Cannot change a constant");
  if (ptr == NULL)
    error("Invalid variable");
  if (g_vartbl[g_VarIndex].type & T_NBR)
  {
    if (g_vartbl[g_VarIndex].dims[1] != 0)
      error("Invalid variable");
    if (g_vartbl[g_VarIndex].dims[0] <= 0)
    { // Not an array
      if (rcvlen != 1)
        error("Invalid variable");
    }
    else
    { // An array
      if ((((MMFLOAT *)ptr - g_vartbl[g_VarIndex].val.fa) + rcvlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
        error("Insufficient space in array");
    }
    I2C_Rcvbuf_Float = (MMFLOAT *)ptr;
  }
  else if (g_vartbl[g_VarIndex].type & T_INT)
  {
    if (g_vartbl[g_VarIndex].dims[1] != 0)
      error("Invalid variable");
    if (g_vartbl[g_VarIndex].dims[0] <= 0)
    { // Not an array
      if (rcvlen != 1)
        error("Invalid variable");
    }
    else
    { // An array
      if ((((long long int *)ptr - g_vartbl[g_VarIndex].val.ia) + rcvlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
        error("Insufficient space in array");
    }
    I2C_Rcvbuf_Int = (long long int *)ptr;
  }
  else if (g_vartbl[g_VarIndex].type & T_STR)
  {
    if (g_vartbl[g_VarIndex].dims[0] != 0)
      error("Invalid variable");
    *(char *)ptr = rcvlen;
    I2C_Rcvbuf_String = (char *)ptr + 1;
  }
  else
    error("Invalid variable");
  ptr = findvar(argv[4], V_FIND);
  if (g_vartbl[g_VarIndex].type & T_CONST)
    error("Cannot change a constant");
  if (g_vartbl[g_VarIndex].type & T_NBR)
    rcvdlenFloat = (MMFLOAT *)ptr;
  else if (g_vartbl[g_VarIndex].type & T_INT)
    rcvdlenInt = (long long int *)ptr;
  else
    error("Invalid variable");

  unsigned char *bbuff;
  if (channel == 0)
  {
    bbuff = I2C_Send_Buffer;
    i2c_read_raw_blocking(i2c0, bbuff, 1);
    if (rcvlen > 1)
    {
      I2CTimer = 0;
      while (count < rcvlen && I2CTimer < rcvlen / 10 + 2)
      {
        if (i2c0->hw->status & 8)
          i2c_read_raw_blocking(i2c0, &bbuff[count++], 1);
      }
    }
  }
  else
  {
    bbuff = I2C_Send_Buffer;
    i2c_read_raw_blocking(i2c1, bbuff, 1);
    if (rcvlen > 1)
    {
      I2CTimer = 0;
      while (count < rcvlen && I2CTimer < rcvlen / 10 + 2)
      {
        if (i2c1->hw->status & 8)
          i2c_read_raw_blocking(i2c1, &bbuff[count++], 1);
      }
    }
  }
  for (int i = 0; i < rcvlen; i++)
  {
    if (I2C_Rcvbuf_String != NULL)
    {
      *I2C_Rcvbuf_String = bbuff[i];
      I2C_Rcvbuf_String++;
    }
    if (I2C_Rcvbuf_Float != NULL)
    {
      *I2C_Rcvbuf_Float = bbuff[i];
      I2C_Rcvbuf_Float++;
    }
    if (I2C_Rcvbuf_Int != NULL)
    {
      *I2C_Rcvbuf_Int = bbuff[i];
      I2C_Rcvbuf_Int++;
    }
  }
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
  int addr, i2c2_options, rcvlen;
  void *ptr = NULL;
  getcsargs(&p, 7);
  if (argc != 7)
    error("Invalid syntax");
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
    error("Number out of bounds");
  ptr = findvar(argv[6], V_FIND | V_EMPTY_OK);
  if (g_vartbl[g_VarIndex].type & T_CONST)
    error("Cannot change a constant");
  if (ptr == NULL)
    error("Invalid variable");
  if (g_vartbl[g_VarIndex].type & T_NBR)
  {
    if (g_vartbl[g_VarIndex].dims[1] != 0)
      error("Invalid variable");
    if (g_vartbl[g_VarIndex].dims[0] <= 0)
    { // Not an array
      if (rcvlen != 1)
        error("Invalid variable");
    }
    else
    { // An array
      if ((((MMFLOAT *)ptr - g_vartbl[g_VarIndex].val.fa) + rcvlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
        error("Insufficient space in array");
    }
    I2C2_Rcvbuf_Float = (MMFLOAT *)ptr;
  }
  else if (g_vartbl[g_VarIndex].type & T_INT)
  {
    if (g_vartbl[g_VarIndex].dims[1] != 0)
      error("Invalid variable");
    if (g_vartbl[g_VarIndex].dims[0] <= 0)
    { // Not an array
      if (rcvlen != 1)
        error("Invalid variable");
    }
    else
    { // An array
      if ((((long long int *)ptr - g_vartbl[g_VarIndex].val.ia) + rcvlen) > (g_vartbl[g_VarIndex].dims[0] + 1 - g_OptionBase))
        error("Insufficient space in array");
    }
    I2C2_Rcvbuf_Int = (long long int *)ptr;
  }
  else if (g_vartbl[g_VarIndex].type & T_STR)
  {
    if (rcvlen < 1 || rcvlen > 255)
      error("Number out of bounds");
    if (g_vartbl[g_VarIndex].dims[0] != 0)
      error("Invalid variable");
    *(char *)ptr = rcvlen;
    I2C2_Rcvbuf_String = (char *)ptr + 1;
  }
  else
    error("Invalid variable");
  I2C2_Rcvlen = rcvlen;

  I2C2_Sendlen = 0;

  char *buff = GetTempMemory(rcvlen > 255 ? rcvlen + 2 : STRINGSIZE);
  i2c2_masterCommand(1, (unsigned char *)buff);
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
    i2c_set_slave_mode(i2c1, false, I2C_Slave_Addr);
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
void i2c_masterCommand(int timer, unsigned char *I2C_Rcv_Buffer)
{
  //	unsigned char start_type,
  unsigned char i2caddr = I2C_Addr;
  if (I2C_Sendlen)
  {
    int i2cret = i2c_write_timeout_us(i2c0, (uint8_t)i2caddr, (uint8_t *)I2C_Send_Buffer, I2C_Sendlen, (I2C_Status == I2C_Status_BusHold ? true : false), I2C_Timeout * 1000);
    mmI2Cvalue = 0;
    if (i2cret == PICO_ERROR_GENERIC)
      mmI2Cvalue = 1;
    if (i2cret == PICO_ERROR_TIMEOUT)
      mmI2Cvalue = 2;
  }
  if (I2C_Rcvlen)
  {
    int i2cret = i2c_read_timeout_us(i2c0, (uint8_t)i2caddr, (uint8_t *)I2C_Rcv_Buffer, I2C_Rcvlen, (I2C_Status == I2C_Status_BusHold ? true : false), I2C_Timeout * 1000);
    mmI2Cvalue = 0;
    if (i2cret == PICO_ERROR_GENERIC)
      mmI2Cvalue = 1;
    if (i2cret == PICO_ERROR_TIMEOUT)
      mmI2Cvalue = 2;
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
}

void i2c2_masterCommand(int timer, unsigned char *I2C2_Rcv_Buffer)
{
  //	unsigned char start_type,
  unsigned char i2c2addr = I2C2_Addr;
  if (I2C2_Sendlen)
  {
    int i2cret = i2c_write_timeout_us(i2c1, (uint8_t)i2c2addr, (uint8_t *)I2C_Send_Buffer, I2C2_Sendlen, (I2C2_Status == I2C_Status_BusHold ? true : false), I2C2_Timeout * 1000);
    mmI2Cvalue = 0;
    if (i2cret == PICO_ERROR_GENERIC)
      mmI2Cvalue = 1;
    if (i2cret == PICO_ERROR_TIMEOUT)
      mmI2Cvalue = 2;
  }
  if (I2C2_Rcvlen)
  {
    int i2cret = i2c_read_timeout_us(i2c1, (uint8_t)i2c2addr, (uint8_t *)I2C2_Rcv_Buffer, I2C2_Rcvlen, (I2C2_Status == I2C_Status_BusHold ? true : false), I2C2_Timeout * 1000);
    mmI2Cvalue = 0;
    if (i2cret == PICO_ERROR_GENERIC)
      mmI2Cvalue = 1;
    if (i2cret == PICO_ERROR_TIMEOUT)
      mmI2Cvalue = 2;
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
void GeneralSend(unsigned int addr, int nbr, char *p)
{
  if (I2C0locked)
  {
    I2C_Sendlen = nbr; // send one byte
    I2C_Rcvlen = 0;
    memcpy(I2C_Send_Buffer, p, nbr);
    I2C_Addr = addr; // address of the device
    i2c_masterCommand(1, NULL);
  }
  else
  {
    I2C2_Sendlen = nbr; // send one byte
    I2C2_Rcvlen = 0;
    memcpy(I2C_Send_Buffer, p, nbr);
    I2C2_Addr = addr; // address of the device
    i2c2_masterCommand(1, NULL);
  }
}

void GeneralReceive(unsigned int addr, int nbr, char *p)
{
  if (I2C0locked)
  {
    I2C_Rcvbuf_Float = NULL;
    I2C_Rcvbuf_Int = NULL;
    I2C_Rcvbuf_String = NULL;
    I2C_Sendlen = 0; // send one byte
    I2C_Rcvlen = nbr;
    I2C_Addr = addr; // address of the device
    i2c_masterCommand(1, (unsigned char *)p);
  }
  else
  {
    I2C2_Rcvbuf_Float = NULL;
    I2C2_Rcvbuf_Int = NULL;
    I2C2_Rcvbuf_String = NULL;
    I2C2_Sendlen = 0; // send one byte
    I2C2_Rcvlen = nbr;
    I2C2_Addr = addr; // address of the device
    i2c2_masterCommand(1, (unsigned char *)p);
  }
}
void WiiSend(int nbr, char *p)
{
  unsigned int addr = nunaddr;
  GeneralSend(addr, nbr, p);
}

void WiiReceive(int nbr, char *p)
{
  unsigned int addr = nunaddr;
  GeneralReceive(addr, nbr, p);
}

uint8_t readRegister8(unsigned int addr, uint8_t reg)
{
  uint8_t buff;
  GeneralSend(addr, 1, (char *)&reg);
  GeneralReceive(addr, 1, (char *)&buff);
  return buff;
}
uint32_t readRegister32(unsigned int addr, uint8_t reg)
{
  uint32_t buff;
  GeneralSend(addr, 1, (char *)&reg);
  GeneralReceive(addr, 4, (char *)&buff);
  return buff;
}
void WriteRegister8(unsigned int addr, uint8_t reg, uint8_t data)
{
  uint8_t buff[2];
  buff[0] = reg;
  buff[1] = data;
  GeneralSend(addr, 2, (char *)buff);
}
void Write8Register16(unsigned int addr, uint16_t reg, uint8_t data)
{
  uint8_t buff[3];
  buff[0] = reg >> 8;
  buff[1] = reg & 0xFF;
  buff[2] = data;
  GeneralSend(addr, 3, (char *)buff);
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
  GeneralSend(addr, 2, (char *)rbuff);
  if (I2C0locked)
    I2C_Status = 0;
  else
    I2C2_Status = 0;
  GeneralReceive(addr, 1, (char *)&buff);
  return buff;
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
      error("SYSTEM I2C not configured");
    if (classic1 || nunchuck1)
      error("Already open");
    memset((void *)&nunstruct[5].x, 0, sizeof(nunstruct[5]));
    int retry = 5;
    do
    {
      WiiSend(sizeof(nuninit), (char *)nuninit);
      uSec(5000);
    } while (mmI2Cvalue && retry--);
    if (mmI2Cvalue)
      error("Nunchuck not connected");
    WiiSend(sizeof(nuninit2), (char *)nuninit2);
    if (mmI2Cvalue)
      error("Nunchuck not connected");
    uSec(5000);
    retry = 5;
    do
    {
      WiiSend(sizeof(nunid), (char *)nunid);
      uSec(5000);
      WiiReceive(4, (char *)&id);
      uSec(5000);
    } while (mmI2Cvalue && retry--);
    if (mmI2Cvalue)
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
    error("Syntax");
}

void MIPS16 cmd_Classic(void)
{
  unsigned char *tp = NULL;
  uint32_t id = 0;
  if ((tp = checkstring(cmdline, (unsigned char *)"OPEN")))
  {
    getcsargs(&tp, 3);
    if (!(I2C0locked || I2C1locked))
      error("SYSTEM I2C not configured");
    if (classic1 || nunchuck1)
      error("Already open");
    memset((void *)&nunstruct[0].x, 0, sizeof(nunstruct[0]));
    int retry = 5;
    do
    {
      WiiSend(sizeof(nuninit), (char *)nuninit);
      uSec(5000);
    } while (mmI2Cvalue && retry--);
    if (mmI2Cvalue)
      error("Classic not connected");
    WiiSend(sizeof(nuninit2), (char *)nuninit2);
    if (mmI2Cvalue)
      error("Classic not connected");
    uSec(5000);
    retry = 5;
    do
    {
      WiiSend(sizeof(nunid), (char *)nunid);
      uSec(5000);
      WiiReceive(4, (char *)&id);
      uSec(5000);
    } while (mmI2Cvalue && retry--);
    if (mmI2Cvalue)
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
    error("Syntax");
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

#ifndef PICOMITEVGA
#define ov7670_address 0x21
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
    *I2C_Send_Buffer = reg;    // the first register to read
    I2C_Addr = ov7670_address; // address of the device
    i2c_masterCommand(1, NULL);
  }
  else
  {
    I2C2_Sendlen = 1; // send one byte
    I2C2_Rcvlen = 0;
    *I2C_Send_Buffer = reg;     // the first register to read
    I2C2_Addr = ov7670_address; // address of the device
    i2c2_masterCommand(1, NULL);
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
    I2C_Addr = ov7670_address; // address of the device
    i2c_masterCommand(1, buff);
  }
  else
  {
    I2C2_Rcvbuf_Float = NULL;
    I2C2_Rcvbuf_Int = NULL;
    I2C2_Rcvlen = 1; // get 7 bytes
    I2C2_Sendlen = 0;
    I2C2_Addr = ov7670_address; // address of the device
    i2c2_masterCommand(1, buff);
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
    I2C_Send_Buffer[0] = a;    // the first register to read
    I2C_Send_Buffer[1] = b;    // the first register to read
    I2C_Addr = ov7670_address; // address of the device
    i2c_masterCommand(1, NULL);
  }
  else
  {
    I2C2_Sendlen = 2; // send one byte
    I2C2_Rcvlen = 0;
    I2C_Send_Buffer[0] = a;     // the first register to read
    I2C_Send_Buffer[1] = b;     // the first register to read
    I2C2_Addr = ov7670_address; // address of the device
    i2c2_masterCommand(1, NULL);
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
    getcsargs(&tp, 11);
    if (argc != 11)
      error("Syntax");
    if (!(Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel))
      error("Invalid display type");
    if (!(I2C0locked || I2C1locked))
      error("SYSTEM I2C not configured");
    if (XCLK)
      error("Camera already open");
    unsigned char code;
    // XCLK pin
    if (!(code = codecheck(argv[0])))
      argv[0] += 2;
    pin1 = getinteger(argv[0]);
    if (!code)
      pin1 = codemap(pin1);
    if (IsInvalidPin(pin1))
      error("Invalid pin");
    if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)
      error("Pin %/| is in use", pin1, pin1);
    int slice = getslice(pin1);
    if ((PinDef[Option.DISPLAY_BL].slice & 0x7f) == slice)
      error("Channel in use for backlight");
    if ((PinDef[pin1].slice & 0x7f) == Option.AUDIO_SLICE)
      error("Channel in use for Audio");

    // PCLK pin
    if (!(code = codecheck(argv[2])))
      argv[2] += 2;
    pin2 = getinteger(argv[2]);
    if (!code)
      pin2 = codemap(pin2);
    if (IsInvalidPin(pin2))
      error("Invalid pin");
    if (ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)
      error("Pin %/| is in use", pin2, pin2);

    // HREF pin
    if (!(code = codecheck(argv[4])))
      argv[4] += 2;
    pin3 = getinteger(argv[4]);
    if (!code)
      pin3 = codemap(pin3);
    if (IsInvalidPin(pin3))
      error("Invalid pin");
    if (ExtCurrentConfig[pin3] != EXT_NOT_CONFIG)
      error("Pin %/| is in use", pin3, pin3);

    // VSYNC pin
    if (!(code = codecheck(argv[6])))
      argv[6] += 2;
    pin4 = getinteger(argv[6]);
    if (!code)
      pin4 = codemap(pin4);
    if (IsInvalidPin(pin4))
      error("Invalid pin");
    if (ExtCurrentConfig[pin4] != EXT_NOT_CONFIG)
      error("Pin %/| is in use", pin4, pin4);

    // RESET pin
    if (!(code = codecheck(argv[8])))
      argv[8] += 2;
    pin5 = getinteger(argv[8]);
    if (!code)
      pin5 = codemap(pin5);
    if (IsInvalidPin(pin5))
      error("Invalid pin");
    if (ExtCurrentConfig[pin5] != EXT_NOT_CONFIG)
      error("Pin %/| is in use", pin5, pin5);

    // D0-D7 pins
    if (!(code = codecheck(argv[10])))
      argv[10] += 2;
    pin6 = getinteger(argv[10]);
    if (!code)
      pin6 = codemap(pin6);
    if (IsInvalidPin(pin6))
      error("Invalid pin");
    int startdata = PinDef[pin6].GPno;
    for (int i = startdata; i < startdata + 8; i++)
    {
      if (IsInvalidPin(PINMAP[i]))
        error("Invalid pin");
      if (ExtCurrentConfig[PINMAP[i]] != EXT_NOT_CONFIG)
        error("Pin %/| is in use", PINMAP[i], PINMAP[i]);
    }
    XCLK = pin1;
    PCLK = pin2;
    HREF = pin3;
    VSYNC = pin4;
    RESET = pin5;
    D0 = pin6;
    setpwm(pin1, &CameraChannel, &CameraSlice, 12000000.0, 50.0);
    ExtCfg(XCLK, EXT_COM_RESERVED, 0);
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
    if (readregister(REG_PID) != 118)
      error("Camera not found");
    ov7670_set(REG_COM7, COM7_RESET); // RESET CAMERA
    ov7670_set(REG_COM7, COM7_RESET); // RESET CAMERA
    ov7670_set(REG_RGB444, 0);
    ov7670_set(REG_COM10, 0x02); // 0x02   VSYNC negative (http://nasulica.homelinux.org/?p=959)
    ov7670_set(REG_MVFP, 0x37);
    // 		ov7670_set( REG_CLKRC, 0x40);
    ov7670_set(REG_COM11, 0x0A);
    ov7670_set(REG_COM7, COM7_RGB);
    ov7670_set(REG_COM1, 0);
    ov7670_set(REG_COM15, COM15_RGB565);
    ov7670_set(REG_COM9, 0x2A);
    ov7670_set(REG_TSLB, 0x04); // 0D = UYVY  04 = YUYV
    ov7670_set(REG_COM13, 0x88);
    ov7670_set(REG_HSTART, 0x13);
    ov7670_set(REG_HSTOP, 0x01);
    ov7670_set(REG_HREF, 0xb6);
    ov7670_set(REG_VSTART, 0x02);
    ov7670_set(REG_VSTOP, 0x7a);
    ov7670_set(REG_VREF, 0x0a);
    ov7670_set(REG_COM5, 0x61);
    ov7670_set(REG_COM6, 0x4b);
    ov7670_set(0x16, 0x02);
    ov7670_set(0x21, 0x02);
    ov7670_set(0x22, 0x91);
    ov7670_set(0x29, 0x07);
    ov7670_set(0x33, 0x0b);
    ov7670_set(0x35, 0x0b);
    ov7670_set(0x37, 0x1d);
    ov7670_set(0x38, 0x71);
    ov7670_set(0x39, 0x2a);
    ov7670_set(REG_COM12, 0x78);

    ov7670_set(0x4d, 0x40);
    ov7670_set(0x4e, 0x20);
    ov7670_set(REG_GFIX, 0);
    ov7670_set(0x74, 0x10);
    ov7670_set(0x8d, 0x4f);
    ov7670_set(0x8e, 0);
    ov7670_set(0x8f, 0);
    ov7670_set(0x90, 0);
    ov7670_set(0x91, 0);
    ov7670_set(0x96, 0);
    ov7670_set(0x9a, 0);

    ov7670_set(0xb0, 0x84);
    ov7670_set(0xb1, 0x0c);
    ov7670_set(0xb2, 0x0e);
    ov7670_set(0xb3, 0x82); //
    ov7670_set(0xb8, 0x0a);
    ov7670_set(0x7a, 0x20); // gamma correction
    ov7670_set(0x7b, 0x10);
    ov7670_set(0x7c, 0x1e);
    ov7670_set(0x7d, 0x35);
    ov7670_set(0x7e, 0x5a);
    ov7670_set(0x7f, 0x69);
    ov7670_set(0x80, 0x76);
    ov7670_set(0x81, 0x80);
    ov7670_set(0x82, 0x88);
    ov7670_set(0x83, 0x8f);
    ov7670_set(0x84, 0x96);
    ov7670_set(0x85, 0xa3);
    ov7670_set(0x86, 0xaf);
    ov7670_set(0x87, 0xc4);
    ov7670_set(0x88, 0xd7);
    ov7670_set(0x89, 0xe8);
    // AGC and AEC parameters. Note we start by disabling those features,
    // then turn them only after tweaking the values.
    ov7670_set(0x13, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT);
    ov7670_set(0x00, 0);
    ov7670_set(0x10, 0);
    ov7670_set(0x0d, 0x40);
    ov7670_set(0x14, 0x18);
    ov7670_set(0xa5, 0x05);
    ov7670_set(0xab, 0x07);
    ov7670_set(0x24, 0x95);
    ov7670_set(0x25, 0x33);
    ov7670_set(0x26, 0xe3);
    ov7670_set(0x9f, 0x78);
    ov7670_set(0xa0, 0x68);
    ov7670_set(0xa1, 0x03);
    ov7670_set(0xa6, 0xd8);
    ov7670_set(0xa7, 0xd8);
    ov7670_set(0xa8, 0xf0);
    ov7670_set(0xa9, 0x90);
    ov7670_set(0xaa, 0x94);
    ov7670_set(0x13, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT | COM8_AGC | COM8_AEC);
    // Almost all of these are magic "reserved" values. */
    ov7670_set(0x0e, 0x61);
    ov7670_set(0x0f, 0x4b);
    ov7670_set(0x16, 0x02);
    // 		ov7670_set(0x1e, 0x27);
    ov7670_set(0x21, 0x02);
    ov7670_set(0x22, 0x91);
    ov7670_set(0x29, 0x07);
    ov7670_set(0x33, 0x0b);
    ov7670_set(0x35, 0x0b);
    ov7670_set(0x37, 0x1d);
    ov7670_set(0x38, 0x71);
    ov7670_set(0x39, 0x2a);
    // 		ov7670_set(0x3c, 0x78);
    ov7670_set(0x4d, 0x40);
    ov7670_set(0x4e, 0x20);
    ov7670_set(0x69, 0);
    // 		ov7670_set(0x6b, 0x0a);
    ov7670_set(0x74, 0x10);
    ov7670_set(0x8d, 0x4f);
    ov7670_set(0x8e, 0);
    ov7670_set(0x8f, 0);
    ov7670_set(0x90, 0);
    ov7670_set(0x91, 0);
    ov7670_set(0x96, 0);
    ov7670_set(0x9a, 0);
    ov7670_set(0xb0, 0x84);
    ov7670_set(0xb1, 0x0c);
    ov7670_set(0xb2, 0x0e);
    ov7670_set(0xb3, 0x82);
    ov7670_set(0xb8, 0x0a);
    // More reserved magic, some of which tweaks white balance */
    ov7670_set(0x43, 0x0a);
    ov7670_set(0x44, 0xf0);
    ov7670_set(0x45, 0x34);
    ov7670_set(0x46, 0x58);
    ov7670_set(0x47, 0x28);
    ov7670_set(0x48, 0x3a);
    ov7670_set(0x59, 0x88);
    ov7670_set(0x5a, 0x88);
    ov7670_set(0x5b, 0x44);
    ov7670_set(0x5c, 0x67);
    ov7670_set(0x5d, 0x49);
    ov7670_set(0x5e, 0x0e);
    ov7670_set(0x6c, 0x0a);
    ov7670_set(0x6d, 0x55);
    ov7670_set(0x6e, 0x11);
    ov7670_set(0x6f, 0x9f);
    ov7670_set(0x6a, 0x40);
    ov7670_set(0x01, 0x40);
    ov7670_set(0x02, 0x60);
    // COLOR SETTING
    ov7670_set(0x4f, 0x80);
    ov7670_set(0x50, 0x80);
    ov7670_set(0x51, 0x00);
    ov7670_set(0x52, 0x22);
    ov7670_set(0x53, 0x5e);
    ov7670_set(0x54, 0x80);
    ov7670_set(0x56, 0x40);
    ov7670_set(0x58, 0x9e);
    ov7670_set(0x59, 0x88);
    ov7670_set(0x5a, 0x88);
    ov7670_set(0x5b, 0x44);
    ov7670_set(0x5c, 0x67);
    ov7670_set(0x5d, 0x49);
    ov7670_set(0x5e, 0x0e);
    ov7670_set(0x69, 0x00);
    ov7670_set(0x6a, 0x40);
    ov7670_set(0x6b, 0x0a);
    ov7670_set(0x6c, 0x0a);
    ov7670_set(0x6d, 0x55);
    ov7670_set(0x6e, 0x11);
    ov7670_set(0x6f, 0x9f);

    ov7670_set(0xb0, 0x84);
    ov7670_set(0x13, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT | COM8_AGC | COM8_AEC | COM8_AWB);
    // Matrix coefficients */
    ov7670_set(0x4f, 0x80);
    ov7670_set(0x50, 0x80);
    ov7670_set(0x51, 0);
    ov7670_set(0x52, 0x22);
    ov7670_set(0x53, 0x5e);
    ov7670_set(0x54, 0x80);
    ov7670_set(0x58, 0x9e);

    ov7670_set(0x41, 0x08);
    ov7670_set(0x3f, 0);
    ov7670_set(0x75, 0x05);
    ov7670_set(0x76, 0xe1);
    ov7670_set(0x4c, 0);
    ov7670_set(0x77, 0x01);
    ov7670_set(0x3d, 0xc3);
    ov7670_set(0x4b, 0x09);
    ov7670_set(0x41, 0x38);
    ov7670_set(0x56, 0x40);

    ov7670_set(0x34, 0x11);
    ov7670_set(0x3b, COM11_EXP | COM11_HZAUTO);
    ov7670_set(0xa4, 0x88);
    ov7670_set(0x96, 0);
    ov7670_set(0x97, 0x30);
    ov7670_set(0x98, 0x20);
    ov7670_set(0x99, 0x30);
    ov7670_set(0x9a, 0x84);
    ov7670_set(0x9b, 0x29);
    ov7670_set(0x9c, 0x03);
    ov7670_set(0x9d, 0x4c);
    ov7670_set(0x9e, 0x3f);
    ov7670_set(0x78, 0x04);
    // Extra-weird stuff. Some sort of multiplexor register */
    ov7670_set(0x79, 0x01);
    ov7670_set(0xc8, 0xf0);
    ov7670_set(0x79, 0x0f);
    ov7670_set(0xc8, 0x00);
    ov7670_set(0x79, 0x10);
    ov7670_set(0xc8, 0x7e);
    ov7670_set(0x79, 0x0a);
    ov7670_set(0xc8, 0x80);
    ov7670_set(0x79, 0x0b);
    ov7670_set(0xc8, 0x01);
    ov7670_set(0x79, 0x0c);
    ov7670_set(0xc8, 0x0f);
    ov7670_set(0x79, 0x0d);
    ov7670_set(0xc8, 0x20);
    ov7670_set(0x79, 0x09);
    ov7670_set(0xc8, 0x80);
    ov7670_set(0x79, 0x02);
    ov7670_set(0xc8, 0xc0);
    ov7670_set(0x79, 0x03);
    ov7670_set(0xc8, 0x40);
    ov7670_set(0x79, 0x05);
    ov7670_set(0xc8, 0x30);
    ov7670_set(0x79, 0x26);
    //			for (int i = 0; OV7670_init[i].reg <= OV7670_REG_LAST; i++) {
    //				ov7670_set(OV7670_init[i].reg, OV7670_init[i].value);
    //			}
    saturation(1);
    OV7670_set_size(OV7670_SIZE_DIV4);
    ov7670_set(REG_COM10, 0x02); // 0x02   VSYNC negative (http://nasulica.homelinux.org/?p=959)
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
    ov7670_set(a, b);
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
      error("Syntax");
    int xs = 0, ys = 0;
    if (!XCLK)
      error("Camera not open");
    int scale = 1;
    int64_t *aint;
    size = parseintegerarray(argv[0], &aint, 1, 1, NULL, true);
    cp = (unsigned char *)aint;
    // get the two variables
    MMFLOAT *outdiff = findvar(argv[2], V_FIND);
    if (!(g_vartbl[g_VarIndex].type & T_NBR))
      error("Invalid variable");
    if (size < 160 * 120 / 8)
      error("Array too small");
    int picout = 0;
    if (argc >= 5)
    {
      scale = getint(argv[4], 1, HRes / 160);
      picout = 1;
      if (argc == 9)
      {
        xs = getint(argv[6], 0, HRes - 1);
        ys = getint(argv[8], 0, VRes - 1);
      }
    }
    for (int i = 0; OV7670_yuv[i].reg <= OV7670_REG_LAST; i++)
    {
      ov7670_set(OV7670_yuv[i].reg, OV7670_yuv[i].value);
    }
    char *buff = GetTempMemory(160 * 120 * 2);
    char *k = buff;
    c.rgb = 0;
    disable_interrupts_pico();
    capture(buff);
    enable_interrupts_pico();
    char *linebuff = NULL;
    if (scale)
      linebuff = GetTempMemory(160 * 3);
    for (int y = ys; y < 120 * scale + ys; y += scale)
    {
      int kk = 0;
      for (int x = 0; x < 160; x++)
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
            int w = 160 * scale;
            if (w > HRes - xs)
              w = HRes - xs;
            DrawBuffer(xs, y + r, xs + w - 1, y + r, (unsigned char *)linebuff);
          }
        }
      }
    }
    *outdiff = (MMFLOAT)totaldifference / (160.0 * 120.0 * 255.0) * 100.0;
  }
  else if ((tp = checkstring(cmdline, (unsigned char *)"CAPTURE")))
  {
    getcsargs(&tp, 5);
    int xs = 0, ys = 0;
    if (!XCLK)
      error("Camera not open");
    int scale = 1;
    if (argc >= 1)
    {
      scale = getint(argv[0], 1, HRes / 160);
      if (argc == 5)
      {
        xs = getint(argv[2], 0, HRes - 1);
        ys = getint(argv[4], 0, VRes - 1);
      }
      else if (argc == 3)
        error("Syntax");
    }
    for (int i = 0; OV7670_rgb[i].reg <= OV7670_REG_LAST; i++)
    {
      ov7670_set(OV7670_rgb[i].reg, OV7670_rgb[i].value);
    }
    char *buff = GetTempMemory(160 * 120 * 2);
    c.rgb = 0;
    disable_interrupts_pico();
    capture(buff);
    enable_interrupts_pico();
    char *linebuff = GetTempMemory(160 * 3 * scale);
    char *k = buff;
    for (int y = ys; y < 120 * scale + ys; y += scale)
    {
      int kk = 0;
      for (int x = 0; x < 160; x++)
      {
        c.rgbbytes[1] = *k++;
        c.rgbbytes[0] = *k++;
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
          int w = 160 * scale;
          if (w > HRes - xs)
            w = HRes - xs;
          DrawBuffer(xs, y + r, xs + w - 1, y + r, (unsigned char *)linebuff);
        }
      }
    }
  }
  else if (checkstring(cmdline, (unsigned char *)"CLOSE"))
  {
    cameraclose();
  }
  else
    error("Syntax");
}
#endif