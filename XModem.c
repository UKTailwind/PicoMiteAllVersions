/***********************************************************************************************************************
PicoMite MMBasic

XModem.c

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
* @file XModem.c
* @author Geoff Graham, Peter Mather
* @brief Source for the MMBasic XMODEM command
*/
/**
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void xmodemTransmit(char *p, int fnbr);
void xmodemReceive(char *sp, int maxbytes, int fnbr, int crunch);
int FindFreeFileNbr(void);
bool rcvnoint;

/*  @endcond */

void MIPS16 cmd_xmodem(void) {
    char *buf, BreakKeySave, *p, *fromp;
    int rcv = 0, fnbr, crunch = false;
    char *fname;
    ClearExternalIO();
    if(toupper(*cmdline) == 'R')
        rcv = true;
    else if(toupper(*cmdline) == 'S')
        rcv = false;
    else if(toupper(*cmdline) == 'C')
        crunch = rcv = true;
    else
        error("Syntax");
    while(isalpha(*cmdline)) cmdline++ ;                            // find the filename (if it is there)
    skipspace(cmdline);

    BreakKeySave = BreakKey;
    BreakKey = 0;

    if(*cmdline == 0 || *cmdline == '\'') {
        // no file name, so this is a transfer to/from program memory
        if(CurrentLinePtr) error("Invalid in a program");
        if(Option.DISPLAY_TYPE>=VIRTUAL && WriteBuf)FreeMemorySafe((void **)&WriteBuf);
        if(rcv)ClearProgram();                                             // we need all the RAM
        else {
            closeframebuffer('A');
            CloseAudio(1);
            ClearVars(0,true);
        }
        buf = GetTempMemory(EDIT_BUFFER_SIZE);
        if(rcv) {
            xmodemReceive(buf, EDIT_BUFFER_SIZE, 0, crunch);
            ClearSavedVars();                                       // clear any saved variables
            SaveProgramToFlash((unsigned char *)buf, true);
        } else {
            int nbrlines = 0;
            // we must copy program memory into RAM expanding tokens as we go
            fromp  = (char *)ProgMemory;
            p = buf;                                                // the RAM buffer
            while(1) {
                if(*fromp == T_NEWLINE) {
                    fromp = (char *)llist((unsigned char *)p, (unsigned char *)fromp);                        // expand the line into the buffer
                    nbrlines++;
                    if(!(nbrlines==1 && p[0]=='\'' && p[1]=='#')){
                        p += strlen(p);
                        if((p - buf) > (EDIT_BUFFER_SIZE - STRINGSIZE)) error("Not enough memory");
                        *p++ = '\n'; *p = 0;                            // terminate that line
                    }
                }
                if(fromp[0] == 0 || fromp[0] == 0xff) break;        // finally, is it the end of the program?
            }
            --p; *p = 0;                                            // erase the last line terminator
            xmodemTransmit(buf, 0);                                 // send it off
        }
    } else {
        // this is a transfer to/from the SD card
        if(crunch) error("Invalid command");
        if(!InitSDCard()) return;
        fnbr = FindFreeFileNbr();
        fname = (char *)getFstring(cmdline);                                // get the file name
        if(Option.SerialConsole){
            rcvnoint=true;
            uart_set_irq_enables((Option.SerialConsole & 3)==1 ? uart0 : uart1, false, false);
        } else rcvnoint=false;
        if(rcv) {
            if(!BasicFileOpen(fname, fnbr, FA_WRITE | FA_CREATE_ALWAYS)) return;
            xmodemReceive(NULL, 0, fnbr, false);
            if(rcvnoint)uart_set_irq_enables((Option.SerialConsole & 3)==1 ? uart0 : uart1, true, false);
        } else {
            if(!BasicFileOpen(fname, fnbr, FA_READ)) return;
            xmodemTransmit(NULL, fnbr);
            if(rcvnoint)uart_set_irq_enables((Option.SerialConsole & 3)==1 ? uart0 : uart1, true, false);
        }
        FileClose(fnbr);
    }
    BreakKey = BreakKeySave;
    cmdline=NULL;
	do_end(false);
	longjmp(mark, 1);												// jump back to the input prompt
}

/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

int _inbyte(int timeout) {
    int c;
    uint64_t timer=time_us_64()+timeout*1000;
    if(!rcvnoint){
        while(time_us_64() < timer) {
            c = getConsole();
            if(c != -1) {
                return c;
            }
        }
    } else {
        while(time_us_64() < timer && !uart_is_readable((Option.SerialConsole & 3)==1 ? uart0 : uart1)) {}
        if(time_us_64() < timer) return uart_getc((Option.SerialConsole & 3)==1 ? uart0 : uart1);
    }
    return -1;
}
char _outbyte(char c, int f){
    if(!rcvnoint)SerialConsolePutC(c,f);
    else uart_putc_raw((Option.SerialConsole & 3)==1 ? uart0 : uart1, c);
    return c;
}

// for the MX470 we don't want any XModem data echoed to the LCD panel
//#define _outbyte(c,d) SerialConsolePutC(c,d)


/***********************************************************************************************
the xmodem protocol
************************************************************************************************/

/* derived from the work of Georges Menie (www.menie.org) Copyright 2001-2010 Georges Menie
 * very much debugged and changed
 *
 * this is just the basic XModem protocol (no 1K blocks, crc, etc).  It has been tested on
 * Terra Term and is intended for use with that software.
 */


#define SOH  0x01
#define STX  0x02
#define EOT  0x04
#define ACK  0x06
#define NAK  0x15
#define CAN  0x18
#define PAD  0x1a

#define DLY_1S 1000
#define MAXRETRANS 25

#define X_BLOCK_SIZE  128
#define X_BUF_SIZE    X_BLOCK_SIZE + 6                              // 128 for XModem + 3 head chars + 2 crc + nul


static int check(const unsigned char *buf, int sz)
{
  int i;
  unsigned char cks = 0;
  for (i = 0; i < sz; ++i) {
      cks += buf[i];
  }
  if (cks == buf[sz])
      return 1;

  return 0;
}


static void flushinput(void)
{
  while (_inbyte(((DLY_1S)*3)>>1) >= 0)
#ifdef PICOMITEWEB
  ProcessWeb(1)
#endif
  ;
}


// receive data
// if sp == NULL we are saving to a file on the SD card (fnbr is the file number)
// otherwise we are saving to RAM which will later be written to program memory
void xmodemReceive(char *sp, int maxbytes, int fnbr, int crunch) {
    unsigned char xbuff[X_BUF_SIZE];
    unsigned char *p;
    unsigned char trychar = NAK; //'C';
    unsigned char packetno = 1;
    int i, c;
    int retry, retrans = MAXRETRANS;
    CrunchData((unsigned char **)&sp, 0);                                         // initialise the crunch subroutine

        // first establish communication with the remote
    while(1) {
        for( retry = 0; retry < 32; ++retry) {
            if(trychar) _outbyte(trychar,1);
            if ((c = _inbyte((DLY_1S)<<1)) >= 0) {
                switch (c) {
                case SOH:
                    goto start_recv;
                case EOT:
                    flushinput();
                    _outbyte(ACK,1);;
                    if(sp != NULL) {
                        if(maxbytes <= 0) error("Not enough memory");
                        *sp++ = 0;                                  // terminate the data
                    }
                    return;                                         // no more data
                case CAN:
                    flushinput();
                    _outbyte(ACK,1);
                    error("Cancelled by remote");
                    break;
                default:
                    break;
                }
            }
        }
        flushinput();
        _outbyte(CAN,1);
        _outbyte(CAN,1);
        _outbyte(CAN,1);
        error("Remote did not respond");                            // no sync

    start_recv:
        trychar = 0;
        p = xbuff;
        *p++ = SOH;
        for (i = 0;  i < (X_BLOCK_SIZE+3); ++i) {
            if ((c = _inbyte(DLY_1S)) < 0) goto reject;
            *p++ = c;
        }
        if (xbuff[1] == (unsigned char)(~xbuff[2]) && (xbuff[1] == packetno || xbuff[1] == (unsigned char)packetno-1) && check(&xbuff[3], X_BLOCK_SIZE)) {
            if (xbuff[1] == packetno) {
                for(i = 0 ; i < X_BLOCK_SIZE ; i++) {
                    if(sp != NULL) {
                        // save the data to the RAM buffer
                        if(--maxbytes > 0) {
                            if(xbuff[i + 3] == PAD) continue;
//                            if(xbuff[i + 3] == PAD)
//                                *sp++ = 0;                          // replace any EOF's (used to pad out a block) with NUL
//                            else
                            if(xbuff[i + 3] == 0) continue;
                                if(crunch)
                                    CrunchData((unsigned char **)&sp, xbuff[i + 3]);
                                else
                                    *sp++ = xbuff[i + 3];           // saving to a memory buffer
                        }
                    } else {
                        // we are saving to a file
                        FilePutChar(xbuff[i + 3], fnbr);
                    }
                }
                ++packetno;
                retrans = MAXRETRANS+1;
            }
            if (--retrans <= 0) {
                flushinput();
                _outbyte(CAN,1);
                _outbyte(CAN,1);
                _outbyte(CAN,1);
                error("Too many errors");
            }
            _outbyte(ACK,1);
            continue;
        }
    reject:
        flushinput();
        _outbyte(NAK,1);
    }
}


// transmit data
// if p == NULL we are reading the data to be sent from a file on the SD card (fnbr is the file number)
// otherwise we are reading from RAM and p points to the start of the data (which is terminated by a zero char)
void xmodemTransmit(char *p, int fnbr) {
  unsigned char xbuff[X_BUF_SIZE];
  unsigned char packetno = 1;
    char prevchar = 0;
  int i, c, len;
  int retry;

  // first establish communication with the remote
  while(1) {
      for( retry = 0; retry < 32; ++retry) {
          if ((c = _inbyte((DLY_1S)<<1)) >= 0) {
              switch (c) {
              case NAK:                                             // start sending
                  goto start_trans;
              case CAN:
                  if ((c = _inbyte(DLY_1S)) == CAN) {
                      _outbyte(ACK,1);;
                      flushinput();
                      error("Cancelled by remote");
                  }
                  break;
              default:
                  break;
              }
          }
      }
      _outbyte(CAN,1);;
      _outbyte(CAN,1);;
      _outbyte(CAN,1);;
      flushinput();
      error("Remote did not respond");                              // no sync

      // send a packet
      while(1) {
      start_trans:
          memset (xbuff, 0, X_BUF_SIZE);                            // start with an empty buffer

          xbuff[0] = SOH;                                           // copy the header
          xbuff[1] = packetno;
          xbuff[2] = ~packetno;

            if(p != NULL) {
                // our data is in RAM
                for(len = 0; len < 128 && *p; len++) {
                    if(*p == '\n' && prevchar != '\r')
                        prevchar = xbuff[len + 3] = '\r';
                    else
                        prevchar = xbuff[len + 3] = *p++;           // copy the data from memory into the packet
                }
            } else {
                // we get the data from a file
                for(len = 0; len < 128 && !FileEOF(fnbr); len++) {
                    xbuff[len + 3] = FileGetChar(fnbr);             // copy the data from the file into the packet
                }
            }
          if (len > 0) {
              unsigned char ccks = 0;
              for (i = 3; i < X_BLOCK_SIZE+3; ++i) {
                  ccks += xbuff[i];
              }
              xbuff[X_BLOCK_SIZE+3] = ccks;

              // now send the block
              for (retry = 0; retry < MAXRETRANS && !MMAbort; ++retry) {
                  // send the block
                  for (i = 0; i < X_BLOCK_SIZE+4 && !MMAbort; ++i) {
                      _outbyte(xbuff[i],1);
                  }
                  // check the response
                  if ((c = _inbyte(DLY_1S)) >= 0 ) {
                      switch (c) {
                      case ACK:
                          ++packetno;
                          goto start_trans;
                      case CAN:                                     // cancelled by remote
                          _outbyte(ACK,1);;
                          flushinput();
                          error("Cancelled by remote");
                          break;
                      case NAK:                                     // receiver got a corrupt block
                      default:
                          break;
                      }
                  }
              }
              // too many retrys... give up
              _outbyte(CAN,1);
              _outbyte(CAN,1);
              _outbyte(CAN,1);
              flushinput();
              error("Too many errors");
          }

          // finished sending - send end of text
          else {
              for (retry = 0; retry < 10; ++retry) {
                  _outbyte(EOT,1);
                  if ((c = _inbyte((DLY_1S)<<1)) == ACK) break;
              }
              flushinput();
              if(c == ACK) return;
              error("Error closing");
          }
      }
  }
}

/*  @endcond */
