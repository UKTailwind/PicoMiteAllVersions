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
#include <complex.h>
#include "MMBasic.h"

extern "C" {

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include <time.h>
//#include "upng.h"
#include "pico/bootrom.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/pwm.h"
#include "hardware/dma.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/flash.h"
#include "hardware/spi.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include <malloc.h>
#include "xregex.h"
#include "hardware/structs/pwm.h"
#include "aes.h"
#ifdef rp2350
#include "pico/rand.h"
#endif
#ifdef USBKEYBOARD
extern int caps_lock;
extern int num_lock;
extern int scroll_lock;
extern int KeyDown[7];
#else
extern char *mouse0Interruptc;
extern volatile int mouse0foundc;
#endif
int64_t TimeOffsetToUptime=1704067200;
extern int last_adc;
extern char banner[];
extern char *pinsearch(int pin);
extern uint8_t getrnd(void);
extern uint32_t restart_reason;
extern unsigned int b64d_size(unsigned int in_size);
extern unsigned int b64e_size(unsigned int in_size);
extern unsigned int b64_encode(const unsigned char* in, unsigned int in_len, unsigned char* out);
extern unsigned int b64_decode(const unsigned char* in, unsigned int in_len, unsigned char* out);
static void parselongAES(CombinedPtr p, int ivadd, uint8_t *keyx, uint8_t *ivx, int64_t **inint, int64_t **outint);
CombinedPtr ADCInterrupt;

#ifndef PICOMITEVGA
    extern int SSD1963data;
#endif
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
static inline CommandToken commandtbl_decode(CombinedPtr p){
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f)<<7);
}
extern int busfault;
//#include "pico/stdio_usb/reset_interface.h"
const char *OrientList[] = {"LANDSCAPE", "PORTRAIT", "RLANDSCAPE", "RPORTRAIT"};
const char *KBrdList[] = {"", "US", "FR", "GR", "IT", "BE", "UK", "ES" };
extern const void * const CallTable[];
struct s_inttbl inttbl[NBRINTERRUPTS];
CombinedPtr InterruptReturn;
extern const char *FErrorMsg[];
uint8_t *buff320= nullptr;
#ifdef PICOMITEWEB
	char *MQTTInterrupt= nullptr;
	volatile bool MQTTComplete=false;
    char *UDPinterrupt= nullptr;
    volatile bool UDPreceive=false;
    void setwifi(unsigned char *tp){
        getargs(&tp,11,(unsigned char *)",");
        if(!(argc==3 || argc==5 || argc==11))error("Syntax");
   	    if(CurrentLinePtr) error("Invalid in a program");
        char *ssid=GetTempMemory(STRINGSIZE);
        char *password=GetTempMemory(STRINGSIZE);
        char *hostname=GetTempMemory(STRINGSIZE);
        char *ipaddress=GetTempMemory(STRINGSIZE);
        char *mask=GetTempMemory(STRINGSIZE);
        char *gateway=GetTempMemory(STRINGSIZE);
        strcpy(ssid,(char *)getCstring(argv[0]));
        strcpy(password,(char *)getCstring(argv[2]));
        if(strlen(ssid)>MAXKEYLEN-1)error("SSID too long, max 63 chars");
        if(strlen(password)>MAXKEYLEN-1)error("Password too long, max 63 chars");
        if(argc==11){
            strcpy(ipaddress,(char *)getCstring(argv[6]));
            strcpy(mask,(char *)getCstring(argv[8]));
            strcpy(gateway,(char *)getCstring(argv[10]));
            ip4_addr_t ipaddr;
            if(!ip4addr_aton(ipaddress, &ipaddr))error("Invalid IP address");
            if(!ip4addr_aton(mask, &ipaddr))error("Invalid mask address");
            if(!ip4addr_aton(gateway, &ipaddr))error("Invalid gateway address");
        }
        if(argc>=5 && *argv[4]){
            strcpy(hostname,(char *)getCstring(argv[4]));
            if(strlen(hostname)>31)error("Hostname too long, max 31 chars");
        }  else {
            strcpy(hostname,"PICO");
            strcat(hostname,id_out);
        }
        strcpy((char *)Option.SSID,ssid);
        strcpy((char *)Option.PASSWORD,password);
        if(argc==11){
            strcpy(Option.ipaddress,ipaddress);    
            strcpy(Option.mask,mask);
            strcpy(Option.gateway,gateway);
        } else {
            memset(Option.ipaddress,0,16);
            memset(Option.mask,0,16);
            memset(Option.gateway,0,16);
        }
        strcpy(Option.hostname,hostname);
        SaveOptions();
    }
#endif
#ifdef PICOMITEVGA
#ifndef HDMI
void VGArecovery(int pin) {
        // TODO: separate place
        // PSRAM_PIN_CS 18
        ExtCurrentConfig[24]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[24].GPno+1]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[24].GPno+2]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[24].GPno+3]]=EXT_BOOT_RESERVED;
        // PICO_DEFAULT_LED_PIN GP25
        ExtCurrentConfig[43]=EXT_BOOT_RESERVED;

        ExtCurrentConfig[Option.VGA_BLUE]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+1]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+2]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+3]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+4]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+5]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+6]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+7]]=EXT_BOOT_RESERVED;

        if(pin)error("Pin %/| is in use",pin,pin);
#ifdef rp2350
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.VGA_BLUE].GPno);
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+1));
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+2));
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+3));
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+4));
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+5));
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+6));
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+7));
        if(Option.audio_i2s_bclk){
            piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.audio_i2s_data].GPno);
            piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.audio_i2s_bclk].GPno);
            piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.audio_i2s_bclk].GPno+1));
        }
#endif
    }
#endif
#endif
const FSIZE_t sd_target_contents = 0;
int TickPeriod[NBRSETTICKS]={0};
volatile int TickTimer[NBRSETTICKS]={0};
CombinedPtr TickInt[NBRSETTICKS];
volatile unsigned char TickActive[NBRSETTICKS]={0};
CombinedPtr OnKeyGOSUB;
CombinedPtr OnPS2GOSUB;
const char *daystrings[] = {"dummy","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"};
const char *CaseList[] = {"", "LOWER", "UPPER"};
int OptionErrorCheck;
unsigned char EchoOption = true;
unsigned long long int __attribute__((section(".my_section"))) saved_variable;  //  __attribute__ ((persistent));  // and this is the address
unsigned int CurrentCpuSpeed;
unsigned int PeripheralBusSpeed;
extern volatile int ConsoleTxBufHead;
extern volatile int ConsoleTxBufTail;
extern char *LCDList[];
extern volatile BYTE SDCardStat;
extern uint64_t TIM12count;
extern char id_out[12];
extern void WriteComand(int cmd);
extern void WriteData(int data);
CombinedPtr CSubInterrupt;
MMFLOAT optionangle=1.0;
bool useoptionangle=false;
bool optionfastaudio=false;
bool optionfulltime=false;
bool screen320=false;
bool optionlogging=false;
volatile bool CSubComplete=false;
uint64_t timeroffset=0;
int SaveOptionErrorSkip=0;
char SaveErrorMessage[MAXERRMSG] = { 0 };
int Saveerrno = 0;
void integersort(int64_t *iarray, int n, long long *index, int flags, int startpoint){
    int i, j = n, s = 1;
    int64_t t;
    if((flags & 1) == 0){
		while (s) {
			s = 0;
			for (i = 1; i < j; i++) {
				if (iarray[i] < iarray[i - 1]) {
					t = iarray[i];
					iarray[i] = iarray[i - 1];
					iarray[i - 1] = t;
					s = 1;
			        if(index!= nullptr){
			        	t=index[i-1+startpoint];
			        	index[i-1+startpoint]=index[i+startpoint];
			        	index[i+startpoint]=t;
			        }
				}
			}
			j--;
		}
    } else {
		while (s) {
			s = 0;
			for (i = 1; i < j; i++) {
				if (iarray[i] > iarray[i - 1]) {
					t = iarray[i];
					iarray[i] = iarray[i - 1];
					iarray[i - 1] = t;
					s = 1;
			        if(index!= nullptr){
			        	t=index[i-1+startpoint];
			        	index[i-1+startpoint]=index[i+startpoint];
			        	index[i+startpoint]=t;
			        }
				}
			}
			j--;
		}
    }
}
void floatsort(MMFLOAT *farray, int n, long long *index, int flags, int startpoint){
    int i, j = n, s = 1;
    int64_t t;
    MMFLOAT f;
    if((flags & 1) == 0){
		while (s) {
			s = 0;
			for (i = 1; i < j; i++) {
				if (farray[i] < farray[i - 1]) {
					f = farray[i];
					farray[i] = farray[i - 1];
					farray[i - 1] = f;
					s = 1;
			        if(index!= nullptr){
			        	t=index[i-1+startpoint];
			        	index[i-1+startpoint]=index[i+startpoint];
			        	index[i+startpoint]=t;
			        }
				}
			}
			j--;
		}
    } else {
		while (s) {
			s = 0;
			for (i = 1; i < j; i++) {
				if (farray[i] > farray[i - 1]) {
					f = farray[i];
					farray[i] = farray[i - 1];
					farray[i - 1] = f;
					s = 1;
			        if(index!= nullptr){
			        	t=index[i-1+startpoint];
			        	index[i-1+startpoint]=index[i+startpoint];
			        	index[i+startpoint]=t;
			        }
				}
			}
			j--;
		}
    }
}

void stringsort(unsigned char *sarray, int n, int offset, long long *index, int flags, int startpoint){
	int ii,i, s = 1,isave;
	int k;
	unsigned char *s1,*s2,*p1,*p2;
	unsigned char temp;
	int reverse= 1-((flags & 1)<<1);
    while (s){
        s=0;
        for(i=1;i<n;i++){
            s2=i*offset+sarray;
            s1=(i-1)*offset+sarray;
            ii = *s1 < *s2 ? *s1 : *s2; //get the smaller  length
            p1 = s1 + 1; p2 = s2 + 1;
            k=0; //assume the strings match
            while((ii--) && (k==0)) {
            if(flags & 2){
                if(toupper(*p1) > toupper(*p2)){
                    k=reverse; //earlier in the array is bigger
                }
                if(toupper(*p1) < toupper(*p2)){
                    k=-reverse; //later in the array is bigger
                }
            } else {
                if(*p1 > *p2){
                    k=reverse; //earlier in the array is bigger
                }
                if(*p1 < *p2){
                    k=-reverse; //later in the array is bigger
                }
            }
            p1++; p2++;
            }
        // if up to this point the strings match
        // make the decision based on which one is shorter
            if(k==0){
                if(*s1 > *s2) k=reverse;
                if(*s1 < *s2) k=-reverse;
            }
            if (k==1){ // if earlier is bigger swap them round
                ii = *s1 > *s2 ? *s1 : *s2; //get the bigger length
                ii++;
                p1=s1;p2=s2;
                while(ii--){
                temp=*p1;
                *p1=*p2;
                *p2=temp;
                p1++; p2++;
                }
                s=1;
                if(index!= nullptr){
                    isave=index[i-1+startpoint];
                    index[i-1+startpoint]=index[i+startpoint];
                    index[i+startpoint]=isave;
                }
            }
        }
    }
    if((flags & 5) == 5){
        for(i=n-1;i>=0;i--){
            s2=i*offset+sarray;
            if(*s2 !=0)break;
        }
        i++;
        if(i){
            s2=(n-i)*offset+sarray;
            memmove(s2,sarray,offset*i);
            memset(sarray,0,offset*(n-i));
            if(index!= nullptr){
                long long int *newindex=(long long int *)GetTempMemory(n* sizeof(long long int));
                memmove(&newindex[n-i],&index[startpoint],i*sizeof(long long int));
                memmove(newindex,&index[startpoint+i],(n-i)*sizeof(long long int));
                memmove(&index[startpoint],newindex,n*sizeof(long long int));
            }
        }
    } else if(flags & 4){
        for(i=0;i<n;i++){
            s2=i*offset+sarray;
            if(*s2 !=0)break;
        }
        if(i){
            s2=i*offset+sarray;
            memmove(sarray,s2,offset*(n-i));
            s2=(n-i)*offset+sarray;
            memset(s2,0,offset*i);
            if(index!= nullptr){
                long long int *newindex=(long long int *)GetTempMemory(n* sizeof(long long int));
                memmove(newindex,&index[startpoint+i],(n-i)*sizeof(long long int));
                memmove(&newindex[n-i],&index[startpoint],i*sizeof(long long int));
                memmove(&index[startpoint],newindex,n*sizeof(long long int));
            }
        }
    }
}

void cmd_sort(void){
    MMFLOAT *a3float= nullptr;
    int64_t *a3int= nullptr,*a4int= nullptr;
    unsigned char *a3str= nullptr;
    int i, size=0, truesize,flags=0, maxsize=0, startpoint=0;
	getargs(&cmdline,9,(unsigned char *)",");
    size=parseany(argv[0],&a3float,&a3int,&a3str,&maxsize,true)-1;
    truesize=size;
    if(argc>=3 && *argv[2]){
        int card=parseintegerarray(argv[2],&a4int,2,1,NULL,true)-1;
    	if(card !=size)error("Array size mismatch");
    }
    if(argc>=5 && *argv[4])flags=getint(argv[4],0,7);
    if(argc>=7 && *argv[6])startpoint=getint(argv[6],g_OptionBase,size+g_OptionBase);
    size-=startpoint;
    if(argc==9)size=getint(argv[8],1,size+1+g_OptionBase)-1;
    if(startpoint)startpoint-=g_OptionBase;
    if(a3float!= nullptr){
    	a3float+=startpoint;
    	if(a4int!= nullptr)for(i=0;i<truesize+1;i++)a4int[i]=i+g_OptionBase;
    	floatsort(a3float, size+1, a4int, flags, startpoint);
    } else if(a3int!= nullptr){
    	a3int+=startpoint;
    	if(a4int!= nullptr)for(i=0;i<truesize+1;i++)a4int[i]=i+g_OptionBase;
    	integersort(a3int,  size+1, a4int, flags, startpoint);
    } else if(a3str!= nullptr){
    	a3str+=((startpoint)*(maxsize+1));
    	if(a4int!= nullptr)for(i=0;i<truesize+1;i++)a4int[i]=i+g_OptionBase;
    	stringsort(a3str, size+1,maxsize+1, a4int, flags, startpoint);
    }
}
// this is invoked as a command (ie, TIMER = 0)
// search through the line looking for the equals sign and step over it,
// evaluate the rest of the command and save in the timer
void cmd_timer(void) {
  uint64_t mytime=time_us_64();
  while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
  if(!*cmdline) error("Syntax");
  timeroffset=mytime-(uint64_t)getint(++cmdline,0,mytime/1000)*1000;
}
// this is invoked as a function
void __not_in_flash_func(fun_timer)(void) {
    fret = (MMFLOAT)(time_us_64()-timeroffset)/1000.0;
    targ = T_NBR;
}
uint64_t gettimefromepoch(int *year, int *month, int *day, int *hour, int *minute, int *second){
    struct tm  *tm;
    struct tm tma;
    tm=&tma;
    uint64_t fulltime=time_us_64();
    time_t epochnow=fulltime/1000000 + TimeOffsetToUptime;
    tm=gmtime(&epochnow);
    *year=tm->tm_year+1900;
    *month=tm->tm_mon+1;
    *day=tm->tm_mday;
    *hour=tm->tm_hour;
    *minute=tm->tm_min;
    *second=tm->tm_sec;
    return fulltime;
}

void fun_datetime(void){
    uint8_t* s = (uint8_t*)GetTempMemory(STRINGSIZE);                                    // this will last for the life of the command
    if(checkstring(ep, (unsigned char *)"NOW")){
        int year, month, day, hour, minute, second;
        gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
        IntToStrPad((char *)s, day, '0', 2, 10);
        s[2] = '-'; IntToStrPad((char *)s + 3, month, '0', 2, 10);
        s[5] = '-'; IntToStr((char *)s + 6, year, 10);
        s[10] = ' ';
        IntToStrPad((char *)s+11, hour, '0', 2, 10);
        s[13] = ':'; IntToStrPad((char *)s + 14, minute, '0', 2, 10);
        s[16] = ':'; IntToStrPad((char *)s + 17, second, '0', 2, 10);
    } else {
        struct tm  *tm;
        struct tm tma;
        tm=&tma;
        time_t timestamp = getinteger(ep); /* See README.md if your system lacks timegm(). */
        tm=gmtime(&timestamp);
        IntToStrPad((char *)s, tm->tm_mday, '0', 2, 10);
        s[2] = '-'; IntToStrPad((char *)s + 3, tm->tm_mon+1, '0', 2, 10);
        s[5] = '-'; IntToStr((char *)s + 6, tm->tm_year+1900, 10);
        s[10] = ' ';
        IntToStrPad((char *)s+11, tm->tm_hour, '0', 2, 10);
        s[13] = ':'; IntToStrPad((char *)s + 14, tm->tm_min, '0', 2, 10);
        s[16] = ':'; IntToStrPad((char *)s + 17, tm->tm_sec, '0', 2, 10);
    }
    CtoM(s);
    targ = T_STR;
    sret = s;
}
time_t get_epoch(int year, int month,int day, int hour,int minute, int second){
    struct tm  *tm;
    struct tm tma;
    tm=&tma;
    tm->tm_year = year - 1900;
    tm->tm_mon = month - 1;
    tm->tm_mday = day;
    tm->tm_hour = hour;
    tm->tm_min = minute;
    tm->tm_sec = second;
    return timegm(tm);
}

void fun_epoch(void){
    unsigned char *arg;
    struct tm  *tm;
    struct tm tma;
    tm=&tma;
    int d, m, y, h, min, s;
    if(!checkstring(ep, (unsigned char *)"NOW"))
    {
        arg = getCstring(ep);
        CombinedPtr _arg = arg;
        getargs(&_arg, 11, (unsigned char *)"-/ :");                                      // this is a macro and must be the first executable stmt in a block
        if(!(argc == 11)) error("Syntax");
            d = atoi((char *)argv[0].raw());
            m = atoi((char *)argv[2].raw());
            y = atoi((char *)argv[4].raw());
			if(d>1000){
				int tmp=d;
				d=y;
				y=tmp;
			}
            if(y >= 0 && y < 100) y += 2000;
            if(d < 1 || d > 31 || m < 1 || m > 12 || y < 1902 || y > 2999) error("Invalid date");
            h = atoi((char *)argv[6].raw());
            min  = atoi((char *)argv[8].raw());
            s = atoi((char *)argv[10].raw());
            if(h < 0 || h > 23 || min < 0 || m > 59 || s < 0 || s > 59) error("Invalid time");
//            day = d;
//            month = m;
//            year = y;
            tm->tm_year = y - 1900;
            tm->tm_mon = m - 1;
            tm->tm_mday = d;
            tm->tm_hour = h;
            tm->tm_min = min;
            tm->tm_sec = s;
    } else {
        int year, month, day, hour, minute, second;
        gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
        tm->tm_year = year - 1900;
        tm->tm_mon = month - 1;
        tm->tm_mday = day;
        tm->tm_hour = hour;
        tm->tm_min = minute;
        tm->tm_sec = second;
    }
        time_t timestamp = timegm(tm); /* See README.md if your system lacks timegm(). */
        iret=timestamp;
        targ = T_INT;
}


void cmd_pause(void) {
    static int interrupted = false;
    MMFLOAT f;
    static uint64_t PauseTimer, IntPauseTimer;
    f = getnumber(cmdline)*1000;                                         // get the pulse width
    if(f < 0) error("Number out of bounds");
    if(f < 2) return;

    if(f < 1500) {
        PauseTimer=time_us_64()+(uint64_t)f;   
        while(time_us_64() <  PauseTimer){}                                       // if less than 1.5mS do the pause right now
        return;                                                     // and exit straight away
      }

    if(InterruptReturn == nullptr) {
        // we are running pause in a normal program
        // first check if we have reentered (from an interrupt) and only zero the timer if we have NOT been interrupted.
        // This means an interrupted pause will resume from where it was when interrupted
        if(!interrupted) PauseTimer = time_us_64();
        interrupted = false;

        while(time_us_64() < FloatToInt32(f) + PauseTimer) {
            CheckAbort();
            if(check_interrupt()) {
                // if there is an interrupt fake the return point to the start of this stmt
                // and return immediately to the program processor so that it can send us off
                // to the interrupt routine.  When the interrupt routine finishes we should reexecute
                // this stmt and because the variable interrupted is static we can see that we need to
                // resume pausing rather than start a new pause time.
                while(*cmdline && *cmdline != cmdtoken) cmdline--;  // step back to find the command token
                InterruptReturn = cmdline;                          // point to it
                interrupted = true;                                 // show that this stmt was interrupted
                return;                                             // and let the interrupt run
            }
        }
          interrupted = false;
    }
    else {
        // we are running pause in an interrupt, this is much simpler but note that
        // we use a different timer from the main pause code (above)
        IntPauseTimer = time_us_64();
        while( time_us_64()< FloatToInt32(f) + IntPauseTimer ) CheckAbort();
    }
}

void cmd_longString(void){
    CombinedPtr tp;
    tp = checkstring(cmdline, (unsigned char *)"SETBYTE");
    if(tp){
        int64_t *dest= nullptr;
        int p=0;
        uint8_t *q= nullptr;
        int nbr;
        int j=0;
    	getargs(&tp, 5, (unsigned char *)",");
        if(argc != 5)error("Argument count");
        j=(parseintegerarray(argv[0],&dest,1,1,NULL,true)-1)*8-1;
        q=(uint8_t *)&dest[1];
        p = getint(argv[2],g_OptionBase,j-g_OptionBase);
        nbr=getint(argv[4],0,255);
        q[p-g_OptionBase]=nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"APPEND");
    if(tp){
        int64_t *dest= nullptr;
        CombinedPtr p;
        char *q= nullptr;
        int i,j,nbr;
        getargs(&tp, 3, (unsigned char *)",");
        if(argc != 3)error("Argument count");
        j=parseintegerarray(argv[0],&dest,1,1,NULL,true)-1;
        q=(char *)&dest[1];
        q+=dest[0];
        p = getstring(argv[2]);
        nbr = i = *p++;
        if(j*8 < dest[0]+i)error("Integer array too small");
        while(i--)*q++=*p++;
        dest[0]+=nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"TRIM");
    if(tp){
        int64_t *dest= nullptr;
        uint32_t trim;
        char *p, *q= nullptr;
        int i;
        getargs(&tp, 3, (unsigned char *)",");
        if(argc != 3)error("Argument count");
        parseintegerarray(argv[0],&dest,1,1,NULL,true);
        q=(char *)&dest[1];
        trim=getint(argv[2],1,dest[0]);
        i = dest[0]-trim;
        p=q+trim;
        while(i--)*q++=*p++;
        dest[0]-=trim;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"REPLACE");
    if(tp){
        int64_t *dest= nullptr;
        char *q= nullptr;
        int i,nbr;
        getargs(&tp, 5, (unsigned char *)",");
        if(argc != 5)error("Argument count");
        parseintegerarray(argv[0],&dest,1,1,NULL,true);
        q=(char *)&dest[1];
        CombinedPtr p = getstring(argv[2]);
        nbr=getint(argv[4],1,dest[0]-*p+1);
        q+=nbr-1;
        i = *p++;
        while(i--)*q++=*p++;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"LOAD");
    if(tp){
        int64_t *dest= nullptr;
        CombinedPtr p;
        char *q= nullptr;
        int i,j;
        getargs(&tp, 5, (unsigned char *)",");
        if(argc != 5)error("Argument count");
        int64_t nbr=getinteger(argv[2]);
        i=nbr;
        j=parseintegerarray(argv[0],&dest,1,1,NULL,true)-1;
        q=(char *)&dest[1];
        dest[0]=0;
        p=getstring(argv[4]);
        if(nbr> *p)nbr=*p;
        p++;
        if(j*8 < dest[0]+nbr)error("Integer array too small");
        while(i--)*q++=*p++;
        dest[0]+=nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"LEFT");
    if(tp){
        int64_t *dest= nullptr, *src= nullptr;
        char *p= nullptr;
        char *q= nullptr;
        int i,j,nbr;
        getargs(&tp, 5, (unsigned char *)",");
        if(argc != 5)error("Argument count");
        j=parseintegerarray(argv[0],&dest,1,1,NULL,true)-1;
        q=(char *)&dest[1];
        parseintegerarray(argv[2],&src,2,1,NULL,false);
        p=(char *)&src[1];
        nbr=i=getinteger(argv[4]);
        if(nbr>src[0])nbr=i=src[0];
        if(j*8 < i)error("Destination array too small");
        while(i--)*q++=*p++;
        dest[0]=nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"RIGHT");
    if(tp){
        int64_t *dest= nullptr, *src= nullptr;
        char *p= nullptr;
        char *q= nullptr;
        int i,j,nbr;
        getargs(&tp, 5, (unsigned char *)",");
        if(argc != 5)error("Argument count");
        j=parseintegerarray(argv[0],&dest,1,1,NULL,true)-1;
        q=(char *)&dest[1];
        parseintegerarray(argv[2],&src,2,1,NULL,false);
        p=(char *)&src[1];
        nbr=i=getinteger(argv[4]);
        if(nbr>src[0]){
            nbr=i=src[0];
        } else p+=(src[0]-nbr);
        if(j*8 < i)error("Destination array too small");
        while(i--)*q++=*p++;
        dest[0]=nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"MID");
    if(tp){
       int64_t *dest= nullptr, *src= nullptr;
        char *p= nullptr;
        char *q= nullptr;
        int i,j,nbr,start;
        getargs(&tp, 7,(unsigned char *)",");
        if(argc < 5)error("Argument count");
        j=parseintegerarray(argv[0],&dest,1,1,NULL,true)-1;
        q=(char *)&dest[1];
        parseintegerarray(argv[2],&src,2,1,NULL,false);
        p=(char *)&src[1];
        start=getint(argv[4],1,src[0]);
        if(argc==7)nbr=getinteger(argv[6]);
        else nbr=src[0];
        p+=start-1;
        if(nbr+start>src[0]){
            nbr=src[0]-start+1;
        }
        i=nbr;
        if(j*8 < nbr)error("Destination array too small");
        while(i--)*q++=*p++;
        dest[0]=nbr;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"CLEAR");
    if(tp){
        int64_t *dest= nullptr;
        getargs(&tp, 1, (unsigned char *)",");
        if(argc != 1)error("Argument count");
        parseintegerarray(argv[0],&dest,1,1,NULL,true);
        dest[0]=0;
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"RESIZE");
    if(tp){
        int64_t *dest= nullptr;
        int j=0;
        getargs(&tp, 3, (unsigned char *)",");
        if(argc != 3)error("Argument count");
        j=(parseintegerarray(argv[0],&dest,1,1,NULL,true)-1)*8;
        dest[0] = getint(argv[2], 0, j);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"UCASE");
    if(tp){
        int64_t *dest= nullptr;
        char *q= nullptr;
        int i;
        getargs(&tp, 1, (unsigned char *)",");
        if(argc != 1)error("Argument count");
        parseintegerarray(argv[0],&dest,1,1,NULL,true);
        q=(char *)&dest[1];
        i=dest[0];
        while(i--){
        if(*q >= 'a' && *q <= 'z')
            *q -= 0x20;
        q++;
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"PRINT");
    if(tp){
        int64_t *dest= nullptr;
        char *q= nullptr;
        int j, fnbr=0;
        bool docrlf=true;
        getargs(&tp, 5, (unsigned char *)",;");
        if(argc==5)error("Syntax");
        if(argc >= 3){
            if(*argv[0] == '#')argv[0]++;                                 // check if the first arg is a file number
            fnbr = getinteger(argv[0]);                                 // get the number
            parseintegerarray(argv[2],&dest,2,1,NULL,true);
            if(*argv[3]==';')docrlf=false;
        } else {
            parseintegerarray(argv[0],&dest,1,1,NULL,true);
            if(*argv[1]==';')docrlf=false;
         }
        q=(char *)&dest[1];
        j=dest[0];
        while(j--){
            MMfputc(*q++, fnbr);
        }
        if(docrlf)MMfputs((unsigned char *)"\2\r\n", fnbr);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"LCASE");
    if(tp){
        int64_t *dest= nullptr;
        char *q= nullptr;
        int i;
        getargs(&tp, 1, (unsigned char *)",");
        if(argc != 1)error("Argument count");
        parseintegerarray(argv[0],&dest,1,1,NULL,true);
        q=(char *)&dest[1];
        i=dest[0];
        while(i--){
            if(*q >= 'A' && *q <= 'Z')
                *q += 0x20;
            q++;
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"COPY");
    if(tp){
       int64_t *dest= nullptr, *src= nullptr;
        char *p= nullptr;
        char *q= nullptr;
        int i=0,j;
        getargs(&tp, 3, (unsigned char *)",");
        if(argc != 3)error("Argument count");
        j=parseintegerarray(argv[0],&dest,1,1,NULL,true);
        q=(char *)&dest[1];
        dest[0]=0;
        parseintegerarray(argv[2],&src,2,1,NULL,false);
        p=(char *)&src[1];
        if(j*8 < src[0])error("Destination array too small");
        i=src[0];
        while(i--)*q++=*p++;
        dest[0]=src[0];
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"CONCAT");
    if(tp){
        int64_t *dest= nullptr, *src= nullptr;
        char *p= nullptr;
        char *q= nullptr;
        int i=0,j,d=0,s=0;
        getargs(&tp, 3, (unsigned char *)",");
        if(argc != 3)error("Argument count");
        j=parseintegerarray(argv[0],&dest,1,1,NULL,true)-1;
        q=(char *)&dest[1];
        d=dest[0];
        parseintegerarray(argv[2],&src,2,1,NULL,false);
        p=(char *)&src[1];
        i = s = src[0];
        if(j*8 < (d+s))error("Destination array too small");
        q+=d;
        while(i--)*q++=*p++;
        dest[0]+=src[0];
        return;
    }
//unsigned char * parselongAES(uint8_t *p, int ivadd, uint8_t *keyx, uint8_t *ivx, int64_t **inint, int64_t **outint)
    tp = checkstring(cmdline, (unsigned char *)"AES128");
    if(tp) {
        struct AES_ctx ctx;
        unsigned char keyx[16];
        CombinedPtr p;
        int64_t *dest= nullptr, *src= nullptr;
        char *qq= nullptr;
        char *q= nullptr;
//void parselongAES(uint8_t *p, int ivadd, uint8_t *keyx, uint8_t *ivx, int64_t **inint, int64_t **outint){
        if((p=checkstring(tp, (unsigned char *)"ENCRYPT CBC"))){
            uint8_t iv[16];
            for(int i=0;i<16;i++)iv[i]=getrnd();
            parselongAES(p, 16, &keyx[0], &iv[0], &src, &dest);
            qq=(char *)&src[1];
            q=(char *)&dest[1];
            dest[0]=src[0]+16;
            memcpy(&q[16],qq,src[0]);
            memcpy(q,iv,16);
            AES_init_ctx_iv(&ctx, keyx, iv);
            AES_CBC_encrypt_buffer(&ctx, (unsigned char *)&q[16], src[0]);
            return;
        } else if((p=checkstring(tp, (unsigned char *)"DECRYPT CBC"))){
            uint8_t iv[16];
            parselongAES(p, -16, &keyx[0], NULL, &src, &dest);
            dest[0]=src[0]-16;
            qq=(char *)&src[1];
            q=(char *)&dest[1];
            memcpy((void*)iv,qq,16); //restore the IV
            memcpy(q,&qq[16],dest[0]);
            AES_init_ctx_iv(&ctx, keyx, iv);
            AES_CBC_decrypt_buffer(&ctx, (unsigned char *)q, dest[0]);
            return;
        } else if((p=checkstring(tp, (unsigned char *)"ENCRYPT ECB"))){
            struct AES_ctx ctxcopy;
            parselongAES(p, 0, &keyx[0], NULL, &src, &dest);
            qq=(char *)&src[1];
            q=(char *)&dest[1];
            dest[0]=src[0];
            memcpy(q,qq,src[0]);
            AES_init_ctx(&ctxcopy, keyx);
            for(int i=0;i<src[0];i+=16){
                memcpy(&ctx,&ctxcopy,sizeof(ctx));
                AES_ECB_encrypt(&ctx, (unsigned char *)&q[i]);
            }
            return;
        } else if((p=checkstring(tp, (unsigned char *)"DECRYPT ECB"))){
            struct AES_ctx ctxcopy;
            parselongAES(p, 0, &keyx[0], NULL, &src, &dest);
            qq=(char *)&src[1];
            q=(char *)&dest[1];
            dest[0]=src[0];
            memcpy(q,qq,src[0]);
            AES_init_ctx(&ctxcopy, keyx);
            for(int i=0;i<src[0];i+=16){
                memcpy(&ctx,&ctxcopy,sizeof(ctx));
                AES_ECB_decrypt(&ctx, (unsigned char *)&q[i]);
            }
            return;
        } else if((p=checkstring(tp, (unsigned char *)"ENCRYPT CTR"))){
            uint8_t iv[16];
            for(int i=0;i<16;i++)iv[i]=getrnd();
            parselongAES(p, 16, &keyx[0], &iv[0], &src, &dest);
            qq=(char *)&src[1];
            q=(char *)&dest[1];
            dest[0]=src[0]+16;
            memcpy(&q[16],qq,src[0]);
            memcpy(q,iv,16);
            AES_init_ctx_iv(&ctx, keyx, iv);
            AES_CTR_xcrypt_buffer(&ctx, (unsigned char *)&q[16], src[0]);
            return;
        } else if((p=checkstring(tp, (unsigned char *)"DECRYPT CTR"))){
            uint8_t iv[16];
            parselongAES(p, -16, &keyx[0], NULL, &src, &dest);
            dest[0]=src[0]-16;
            qq=(char *)&src[1];
            q=(char *)&dest[1];
            memcpy((void*)iv,qq,16); //restore the IV
            memcpy(q,&qq[16],dest[0]);
            AES_init_ctx_iv(&ctx, keyx, iv);
            AES_CTR_xcrypt_buffer(&ctx, (unsigned char *)q, dest[0]);
            return;
        } else error("Syntax");
    }
    tp = checkstring(cmdline, (unsigned char *)"BASE64");
    if(tp) {
        CombinedPtr p;
        if((p=checkstring(tp, (unsigned char *)"ENCODE"))){
            int64_t *dest= nullptr, *src= nullptr;
            unsigned char *qq= nullptr;
            unsigned char *q= nullptr;
            int j;
            getargs(&p, 3, (unsigned char *)",");
            if(argc != 3)error("Argument count");
            j=parseintegerarray(argv[2],&dest,2,1,NULL,true)-1;
            q=(unsigned char *)&dest[1];
            parseintegerarray(argv[0],&src,1,1,NULL,false);
            qq=(unsigned char *)&src[1];
            if(j*8 < b64e_size(src[0]))error("Destination array too small");
            dest[0]=b64_encode(qq, src[0], q);
            return;
        } else if((p=checkstring(tp, (unsigned char *)"DECODE"))){
            int64_t *dest= nullptr, *src= nullptr;
            unsigned char *qq= nullptr;
            unsigned char *q= nullptr;
            int j;
            getargs(&p, 3, (unsigned char *)",");
            if(argc != 3)error("Argument count");
            j=parseintegerarray(argv[2],&dest,2,1,NULL,true)-1;
            q=(unsigned char *)&dest[1];
            parseintegerarray(argv[0],&src,1,1,NULL,false);
            qq=(unsigned char *)&src[1];
            if(j*8 < b64d_size(src[0]))error("Destination array too small");
            dest[0]=b64_decode(qq, src[0], q);
            return;
        } else error("Syntax");
    }
    error("Invalid option");
}
static void parselongAES(CombinedPtr p, int ivadd, uint8_t *keyx, uint8_t *ivx, int64_t **inint, int64_t **outint){
	int64_t *a1int= nullptr, *a2int= nullptr, *a3int= nullptr, *a4int= nullptr;
	unsigned char *a1str= nullptr,*a4str= nullptr;
	MMFLOAT *a1float= nullptr, *a4float= nullptr;
	int card1, card3;
	getargs(&p,7,(unsigned char *)",");
	if(ivx== nullptr){
		if(argc!=5)error("Syntax");
	} else {
		if(argc<5)error("Syntax");
	}
	*outint= nullptr;
// first process the key
	int length=0;
	card1= parseany(argv[0], &a1float, &a1int, &a1str, &length, false);
	if(card1!=16)error("Key must be 16 elements long");
	if(a1int!= nullptr){
		for(int i=0;i<16;i++){
			if(a1int[i]<0 || a1int[i]>255)error("Key number out of bounds 0-255");
			keyx[i]=a1int[i];
		}
	} else if (a1float!= nullptr){
		for(int i=0;i<16;i++){
			if(a1float[i]<0 || a1float[i]>255)error("Key number out of bounds 0-255");
			keyx[i]=a1float[i];
		}
	} else if(a1str!= nullptr){
		for(int i=0;i<16;i++){
			keyx[i]=a1str[i+1];
		}
	}
//next process the initialisation vector if any
	if(argc==7){
		length=0;
		card1= parseany(argv[6], &a4float, &a4int, &a4str, &length, false);
		if(card1!=16)error("Initialisation vector must be 16 elements long");
		if(a4int!= nullptr){
			for(int i=0;i<16;i++){
				if(a4int[i]<0 || a4int[i]>255)error("Key number out of bounds 0-255");
				ivx[i]=a4int[i];
			}
		} else if (a4float!= nullptr){
			for(int i=0;i<16;i++){
				if(a4float[i]<0 || a4float[i]>255)error("Key number out of bounds 0-255");
				ivx[i]=a4float[i];
			}
		} else if(a4str!= nullptr){
			for(int i=0;i<16;i++){
				ivx[i]=a4str[i+1];
			}
		}
	}
//now process the longstring used for input
	parseintegerarray(argv[2],&a2int,2,1,NULL,false);
	if(*a2int % 16)error("input must be multiple of 16 elements long");
    *inint=a2int;
	card3=parseintegerarray(argv[4],&a3int,3,1,NULL,false);
	if((card3-1)*8<*a2int + ivadd)error("Output array too small");
    *outint=a3int;
}

void fun_LGetStr(void){
        char *s= nullptr;
        int64_t *src= nullptr;
        int start,nbr,j;
        getargs(&ep, 5, (unsigned char *)",");
        if(argc != 5)error("Argument count");
        j=(parseintegerarray(argv[0],&src,2,1,NULL,false)-1)*8;
        start = getint(argv[2],1,j);
        nbr = getinteger(argv[4]);
        if(nbr < 1 || nbr > MAXSTRLEN) error("Number out of bounds");
        if(start+nbr>src[0])nbr=src[0]-start+1;
        sret = (uint8_t*)GetTempMemory(STRINGSIZE);                                       // this will last for the life of the command
        s=(char *)&src[1];
        s+=(start-1);
        char* p=(char *)sret.raw()+1;
        sret.write_byte(nbr);
        while(nbr--)*p++=*s++;
        *p=0;
        targ = T_STR;
}

void fun_LGetByte(void){
        uint8_t *s= nullptr;
        int64_t *src= nullptr;
        int start,j;
    	getargs(&ep, 3, (unsigned char *)",");
        if(argc != 3)error("Argument count");
        j=(parseintegerarray(argv[0],&src,2,1,NULL,false)-1)*8;
        s=(uint8_t *)&src[1];
        start = getint(argv[2],g_OptionBase,j-g_OptionBase);
        iret=s[start-g_OptionBase];
        targ = T_INT;
}


void fun_LInstr(void){
        int64_t *src= nullptr;
        char srch[STRINGSIZE];
        char *str= nullptr;
        int slen,found=0,i,j,n;
        getargs(&ep, 7, (unsigned char *)",");
        if(argc <3  || argc > 7)error("Argument count");
        int64_t start;
        if(argc>=5 && *argv[4])start=getinteger(argv[4])-1;
        else start=0;
        j=(parseintegerarray(argv[0],&src,2,1,NULL,false)-1);
        str=(char *)&src[0];
        Mstrcpy((unsigned char *)srch, getstring(argv[2]));
        if(argc<7){
            slen=*srch;
            iret=0;
            if(start>src[0] || start<0 || slen==0 || src[0]==0 || slen>src[0]-start)found=1;
            if(!found){
                n=src[0]- slen - start;

                for(i = start; i <= n + start; i++) {
                    if(str[i + 8] == srch[1]) {
                        for(j = 0; j < slen; j++)
                            if(str[j + i + 8] != srch[j + 1])
                                break;
                        if(j == slen) {iret= i + 1; break;}
                    }
                }
            }
        } else { //search string is a regular expression
            regex_t regex;
            int reti;
            regmatch_t pmatch;
            MMFLOAT *temp= nullptr;
            MtoC((unsigned char *)srch);
            temp = (double*)findvar(argv[6], V_FIND);
            if(!(g_vartbl[g_VarIndex].type & T_NBR)) error("Invalid variable");
            reti = regcomp(&regex, srch, 0);
            if( reti ) {
                regfree(&regex);
                error("Could not compile regex");
            }
	        reti = regexec(&regex, &str[start+8], 1, &pmatch, 0);
            if( !reti ){
                iret=pmatch.rm_so+1+start;
                if(temp)*temp=(MMFLOAT)(pmatch.rm_eo-pmatch.rm_so);
            }
            else if( reti == REG_NOMATCH ){
                iret=0;
                if(temp)*temp=0.0;
            }
            else{
		        regfree(&regex);
                error("Regex execution error");
            }
		    regfree(&regex);
        }
        targ = T_INT;
}

void fun_LCompare(void){
    int64_t *dest, *src;
    char *p= nullptr;
    char *q= nullptr;
    int d=0,s=0,found=0;
    getargs(&ep, 3, (unsigned char *)",");
    if(argc != 3)error("Argument count");
    parseintegerarray(argv[0],&dest,1,1,NULL,false);
    q=(char *)&dest[1];
    d=dest[0];
    parseintegerarray(argv[2],&src,1,1,NULL,false);
    p=(char *)&src[1];
    s=src[0];
    while(!found) {
        if(d == 0 && s == 0) {found=1;iret=0;}
        if(d == 0 && !found) {found=1;iret=-1;}
        if(s == 0 && !found) {found=1;iret=1;}
        if(*q < *p && !found) {found=1;iret=-1;}
        if(*q > *p && !found) {found=1;iret=1;}
        q++;  p++;  d--; s--;
    }
    targ = T_INT;
}

void fun_LLen(void) {
    int64_t *dest= nullptr;
    getargs(&ep, 1, (unsigned char *)",");
    if(argc != 1)error("Argument count");
    parseintegerarray(argv[0],&dest,1,1,NULL,false);
    iret=dest[0];
    targ = T_INT;
}



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
void cmd_date(void) {
	struct tm  *tm;
	struct tm tma;
	tm=&tma;
	int dd, mm, yy;
	while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
	if(!*cmdline) error("Syntax");
	++cmdline;
	CombinedPtr arg = getCstring(cmdline);
	{
		getargs(&arg, 5, (unsigned char *)"-/");										// this is a macro and must be the first executable stmt in a block
		if(argc != 5) error("Syntax");
		dd = atoi((char *)argv[0].raw());
		mm = atoi((char *)argv[2].raw());
		yy = atoi((char *)argv[4].raw());
        if(dd>1000){
            int tmp=dd;
            dd=yy;
            yy=tmp;
        }
		if(yy >= 0 && yy < 100) yy += 2000;
	    //check year
	    if(yy>=1900 && yy<=9999)
	    {
	        //check month
	        if(mm>=1 && mm<=12)
	        {
	            //check days
	            if((dd>=1 && dd<=31) && (mm==1 || mm==3 || mm==5 || mm==7 || mm==8 || mm==10 || mm==12))
	                {}
	            else if((dd>=1 && dd<=30) && (mm==4 || mm==6 || mm==9 || mm==11))
	                {}
	            else if((dd>=1 && dd<=28) && (mm==2))
	                {}
	            else if(dd==29 && mm==2 && (yy%400==0 ||(yy%4==0 && yy%100!=0)))
	                {}
	            else
	                error("Day is invalid");
	        }
	        else
	        {
	            error("Month is not valid");
	        }
	    }
	    else
	    {
	        error("Year is not valid");
	    }
        int year, month, day, hour, minute, second;
        gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
//		mT4IntEnable(0);       										// disable the timer interrupt to prevent any conflicts while updating
		day = dd;
		month = mm;
		year = yy;
	    tm->tm_year = year - 1900;
	    tm->tm_mon = month - 1;
	    tm->tm_mday = day;
	    tm->tm_hour = hour;
	    tm->tm_min = minute;
	    tm->tm_sec = second;
	    time_t timestamp = timegm(tm); /* See README.md if your system lacks timegm(). */
	    tm=gmtime(&timestamp);
	    day_of_week=tm->tm_wday;
	    if(day_of_week==0)day_of_week=7;
        TimeOffsetToUptime=get_epoch(year, month, day, hour, minute, second)-time_us_64()/1000000;
//		update_clock();
//		mT4IntEnable(1);       										// enable interrupt
	}
}

// this is invoked as a function
void fun_date(void) {
    int year, month, day, hour, minute, second;
    gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
    uint8_t* s = (uint8_t*)GetTempMemory(STRINGSIZE);                                    // this will last for the life of the command
    IntToStrPad((char *)s, day, '0', 2, 10);
    s[2] = '-'; IntToStrPad((char *)s + 3, month, '0', 2, 10);
    s[5] = '-'; IntToStr((char *)s + 6, year, 10);
    CtoM(s);
    targ = T_STR;
    sret = s;
}

// this is invoked as a function
void fun_day(void) {
    struct tm  *tm;
    struct tm tma;
    tm=&tma;
    time_t time_of_day;
    int i;
    sret = (uint8_t*)GetTempMemory(STRINGSIZE);                                    // this will last for the life of the command
    int d, m, y;
    if(!checkstring(ep, (unsigned char *)"NOW"))
    {
        CombinedPtr arg = getCstring(ep);
        getargs(&arg, 5, (unsigned char *)"-/");                                     // this is a macro and must be the first executable stmt in a block
        if(!(argc == 5))error("Syntax");
        d = atoi((char *)argv[0].raw());
        m = atoi((char *)argv[2].raw());
        y = atoi((char *)argv[4].raw());
		if(d>1000){
			int tmp=d;
			d=y;
			y=tmp;
		}
        if(y >= 0 && y < 100) y += 2000;
        if(d < 1 || d > 31 || m < 1 || m > 12 || y < 1902 || y > 2999) error("Invalid date");
        tm->tm_year = y - 1900;
        tm->tm_mon = m - 1;
        tm->tm_mday = d;
        tm->tm_hour = 0;
        tm->tm_min = 0;
        tm->tm_sec = 0;
        time_of_day = timegm(tm);
        tm=gmtime(&time_of_day);
        i=tm->tm_wday;
        if(i==0)i=7;
        strcpy((char *)sret.raw(), daystrings[i]);
    } else {
        int year, month, day, hour, minute, second;
        gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
        tm->tm_year = year - 1900;
        tm->tm_mon = month - 1;
        tm->tm_mday = day;
        tm->tm_hour = 0;
        tm->tm_min = 0;
        tm->tm_sec = 0;
        time_of_day = timegm(tm);
        tm=gmtime(&time_of_day);
        i=tm->tm_wday;
        if(i==0)i=7;
        strcpy((char *)sret.raw() ,daystrings[i]);
    }
    CtoM(sret.raw());
    targ = T_STR;
}

// this is invoked as a command (ie, time$ = "6:10:45")
// search through the line looking for the equals sign and step over it,
// evaluate the rest of the command, split it up and save in the system counters
void cmd_time(void) {
	int h = 0;
	int m = 0;
	int s = 0;
    MMFLOAT f;
    long long int i64;
    CombinedPtr ss;
    int t=0;
    int offset;
	while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
	if(!*cmdline) error("Syntax");
	++cmdline;
    evaluate(cmdline, &f, &i64, &ss, &t, false);
    int year, month, day, hour, minute, second;
    gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
	if(t & T_STR){
        CombinedPtr arg = getCstring(cmdline);
        {
            getargs(&arg, 5,(unsigned char *)":");								// this is a macro and must be the first executable stmt in a block
            if(argc%2 == 0) error("Syntax");
            h = atoi((char *)argv[0].raw());
            if(argc >= 3) m = atoi((char *)argv[2].raw());
            if(argc == 5) s = atoi((char *)argv[4].raw());
            if(h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) error("Invalid time");
//            mT4IntEnable(0);       										// disable the timer interrupt to prevent any conflicts while updating
            hour = h;
            minute = m;
            second = s;
            SecondsTimer = 0;
    //		update_clock();
//            mT4IntEnable(1);       										// enable interrupt
        }
	} else {
		struct tm  *tm;
		struct tm tma;
		tm=&tma;
		offset=getinteger(cmdline);
		tm->tm_year = year - 1900;
		tm->tm_mon = month - 1;
		tm->tm_mday = day;
		tm->tm_hour = hour;
		tm->tm_min = minute;
		tm->tm_sec = second;
	    time_t timestamp = timegm(tm); /* See README.md if your system lacks timegm(). */
	    timestamp+=offset;
	    tm=gmtime(&timestamp);
//		mT4IntEnable(0);       										// disable the timer interrupt to prevent any conflicts while updating
		hour = tm->tm_hour;
		minute = tm->tm_min;
		second = tm->tm_sec;
		SecondsTimer = 0;
//		update_clock();
//    	mT4IntEnable(1);       										// enable interrupt
	}
    TimeOffsetToUptime=get_epoch(year, month, day, hour, minute, second)-time_us_64()/1000000;
}




// this is invoked as a function
void fun_time(void) {
    int year, month, day, hour, minute, second;
    uint8_t* s = (uint8_t*)GetTempMemory(STRINGSIZE);                                  // this will last for the life of the command
    uint64_t fulltime = gettimefromepoch(&year, &month, &day, &hour, &minute, &second);
    IntToStrPad((char *)s, hour, '0', 2, 10);
    s[2] = ':'; IntToStrPad((char *)s + 3, minute, '0', 2, 10);
    s[5] = ':'; IntToStrPad((char *)s + 6, second, '0', 2, 10);
    if(optionfulltime){
        s[8] = '.'; IntToStrPad((char *)s + 9, (fulltime/1000) % 1000, '0', 3, 10);
    }
    CtoM(s);
    sret = s;
    targ = T_STR;
}



void cmd_ireturn(void){
    if(InterruptReturn == nullptr) error("Not in interrupt");
    checkend(cmdline);
    nextstmt = InterruptReturn;
    InterruptReturn = nullptr;
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
    CombinedPtr tp;
    /********************************************************************************************************************
     ******* LIBRARY SAVE **********************************************************************************************/
    if(checkstring(cmdline, (unsigned char *)"SAVE")) {  
        unsigned char *m, *MemBuff;
        unsigned short rem, tkn;
        int i, j, k, InCFun, InQuote, CmdExpected;
        unsigned int CFunDefAddr[100], *CFunHexAddr[100] ;
        if(CurrentLinePtr) error("Invalid in a program");
        if(*ProgMemory != 0x01) return;
        //checkend(p);
        ClearRuntime(true);
        CombinedPtr TempPtr = m = MemBuff = (uint8_t*)GetTempMemory(EDIT_BUFFER_SIZE);

        rem = GetCommandValue((unsigned char *)"Rem");
        InQuote = InCFun = j = 0;
        CmdExpected = true;
        if(Option.LIBRARY_FLASH_SIZE != MAX_PROG_SIZE){
            CombinedPtrI c = CombinedPtr(sd_progmemory - MAX_PROG_SIZE);
            if (*c != 0xFFFFFFFF)
                error("Flash Slot % already in use",MAXFLASHSLOTS);
        }
        // first copy the current program code residing in the Library area to RAM
        if(Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE){
            CombinedPtr p = ProgMemory - Option.LIBRARY_FLASH_SIZE;
            while(!(p[0] == 0 && p[1] == 0)) *m++ = *p++;
              *m++ = 0;                                               // terminate the last line
        }
        //dump(m, 256);
        // then copy the current contents of the program memory to RAM
        
       //MMPrintString("\r\n Size=1 ");PInt(m - MemBuff);
        CombinedPtr p = ProgMemory;
        while(!(p[0] == 0xff && p[1] == 0xff)){
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
                *m++ = *p++; *m++ = *p++; *m++ = *p++;              // copy the line number
                skipspace(p);
            }

            if(*p == T_LABEL) {
                for(i = p[1] + 2; i > 0; i--) *m++ = *p++;          // copy the label
                skipspace(p);
            }
            tkn=commandtbl_decode(p);
            //if(CmdExpected && ( *p == GetCommandValue("End CFunction") || *p == GetCommandValue("End CSub") || *p == GetCommandValue("End DefineFont"))) {
            if(CmdExpected && (  tkn == GetCommandValue((unsigned char *)"End CSub") || tkn == GetCommandValue((unsigned char *)"End DefineFont"))) {
                InCFun = false;                                     // found an  END CSUB or END DEFINEFONT token
            }
            if(InCFun) {
                skipline(p);                                        // skip the body of a CFunction
                m = TempPtr.raw();                                        // don't copy the new line
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
                m = TempPtr.raw();                                        // don't copy the new line tokens
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
                    m = TempPtr.raw();                                    // if the comment was the only thing on the line don't copy the line at all
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
        if(CFunctionLibrary != nullptr) {
            CombinedPtr pp = CFunctionLibrary;
            CombinedPtrI ppi = pp;
            while(*ppi != 0xffffffff) {
//                CFunHexLibAddr[++k] = (unsigned int *)m;            // save the address for later adjustment
                j = *(ppi + 1) + 8;                // calculate the total size of the CFunction
                while(j--) *m++ = *pp++;                            // copy it into RAM
                ppi = pp;
            }
        }
      
        // then, copy any CFunctions in program memory to RAM
        
        i = 0;                                                      // this is used to index CFunHexAddr[] for later adjustment of a CFun's address
       // while(*(unsigned int *)p != 0xffffffff && (int)p < Option.PROG_FLASH_SIZE) {
        CombinedPtrI ppi = p;
        while(*ppi != 0xffffffff) {    
            CFunHexAddr[++i] = (unsigned int *)m;                   // save the address for later adjustment
            j = *(ppi + 1) + 8;                // calculate the total size of the CFunction
            while(j--) *m++ = *p++;                                 // copy it into RAM
            ppi = p;
        }
        // we have now copied all the CFunctions into RAM

        // calculate the size of the library code  to  end on a word boundary
        j=(((m - MemBuff) + (0x4 - 1)) & (~(0x4 - 1)));
       
         //We only have reserved MAX_PROG_SIZE of flash for library code .
        //Error if we try to use too much
        if (j > MAX_PROG_SIZE) error("Library too big");
              
        TempPtr = LibMemory;
     
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
        sd_range_erase(realflashpointer, MAX_PROG_SIZE);
        i=MAX_PROG_SIZE/4;
       
        CombinedPtrI ppp= CombinedPtr(sd_progmemory - MAX_PROG_SIZE);
        while(i--)if(*ppp++ != 0xFFFFFFFF){
            /** enable_interrupts_pico(); */
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
        cmdline = (unsigned char *)""; CurrentLinePtr = nullptr;    // keep the NEW command happy
        cmd_new();                              //  delete the program,add the library code and return to the command prompt
    }
     /********************************************************************************************************************
     ******* LIBRARY DELETE **********************************************************************************************/

     if(checkstring(cmdline, (unsigned char *)"DELETE")) {
        int i;
        if(CurrentLinePtr) error("Invalid in a program");
        if(Option.LIBRARY_FLASH_SIZE != MAX_PROG_SIZE) return;
        
        FlashWriteInit(LIBRARY_FLASH);
        sd_range_erase(realflashpointer, MAX_PROG_SIZE);
        i=MAX_PROG_SIZE/4;
       
        CombinedPtrI ppp= CombinedPtr(sd_progmemory - MAX_PROG_SIZE);
        while(i--)if(*ppp++ != 0xFFFFFFFF){
            /** enable_interrupts_pico(); */
            error("Flash erase problem");
        }
        /** enable_interrupts_pico(); */

        Option.LIBRARY_FLASH_SIZE= 0;
        SaveOptions();
        return;
        // Clear Program Memory and also the Library at the end.
//        cmdline = ""; CurrentLinePtr = nullptr;    // keep the NEW command happy
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
        if (strchr((char *)pp, '.') == nullptr)
            strcat((char *)pp, ".lib");
        if (!BasicFileOpen((char *)pp, fnbr, FA_WRITE | FA_CREATE_ALWAYS)) return;
        int i = 0;
        // first count the normal program code residing in the Library
        CombinedPtr p = LibMemory;
        while(!(p[0] == 0 && p[1] == 0)) {
            p++; i++;
        }
        while(*p == 0){ // the end of the program can have multiple zeros -count them
            p++;i++;
        }
        p++; i++;    //get 0xFF that ends the program and count it
        while(p & 0b11) { //count to the next word boundary
            p++;i++;
        }
            
        //Now add the binary used for CSUB and Fonts
        if(CFunctionLibrary != nullptr) {
            int j=0;
            CombinedPtr pint = CFunctionLibrary;
            while(*pint != 0xffffffff) {
                pint++;                                      //step over the address or Font No.
                j += *pint + 8;                              //Read the size
                pint += (*pint + 4) / sizeof(unsigned int);  //set pointer to start of next CSUB/Font
            }
            i=i+j;
        }
        CombinedPtr s = LibMemory;
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
        if (strchr((char *)pp, '.') == nullptr)
            strcat((char *)pp, ".lib");
        if (!BasicFileOpen((char *)pp, fnbr, FA_READ)) return;
		if(filesource[fnbr]!=FLASHFILE)  fsize = f_size(FileTable[fnbr].fptr);
		else fsize = lfs_file_size(&lfs,FileTable[fnbr].lfsptr);
        if(fsize>MAX_PROG_SIZE)error("File size % should be % or less",fsize,MAX_PROG_SIZE);
        FlashWriteInit(LIBRARY_FLASH);
        sd_range_erase(realflashpointer, MAX_PROG_SIZE);
        int i=MAX_PROG_SIZE/4;
        CombinedPtrI ppp= CombinedPtr(sd_progmemory - MAX_PROG_SIZE);
        while(i--)if(*ppp++ != 0xFFFFFFFF){
            /** enable_interrupts_pico(); */
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
    char *s=(char*)GetTempMemory(STRINGSIZE);
    getargs(&cmdline, 5, (unsigned char *)",");
    strcpy(s, argv[0]);
    if(!(argc == 3 || argc == 5)) error("Argument count");
    if(argc == 5) irq = getint(argv[4], 1, NBRSETTICKS) - 1;
    if(strcasecmp(argv[0], "PAUSE")==0){
        TickActive[irq]=0;
        return;
    } else if(strcasecmp(argv[0], "RESUME")==0){
        TickActive[irq]=1;
        return;
    } else period = getint(argv[0], -1, INT_MAX);
    if(period == 0) {
        TickInt[irq] = nullptr;                                        // turn off the interrupt
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
#ifndef PICOMITEVGA
    int i=Option.DISPLAY_ORIENTATION;
#endif     

    if(Option.SerialConsole){
        MMPrintString("OPTION SERIAL CONSOLE COM");
        MMputchar((Option.SerialConsole & 3)+48,1);
        MMputchar(',',1);
        MMPrintString((char *)PinDef[Option.SerialTX].pinname);MMputchar(',',1);
        MMPrintString((char *)PinDef[Option.SerialRX].pinname);
        if(Option.SerialConsole & 4)MMPrintString((char *)",BOTH");
        PRet();
    }
#ifndef PICOMITEVGA
    if(Option.SYSTEM_CLK){
        PO("SYSTEM SPI");
        MMPrintString((char *)PinDef[Option.SYSTEM_CLK].pinname);MMputchar(',',1);;
        MMPrintString((char *)PinDef[Option.SYSTEM_MOSI].pinname);MMputchar(',',1);;
        MMPrintString((char *)PinDef[Option.SYSTEM_MISO].pinname);MMPrintString("\r\n");
    }
#endif
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
    //if(Option.FlashSize !=2048*1024)
    PO2Int("SWAP (FLASH) SIZE", Option.FlashSize);
    if(psram_size()) PO2Int("PSRAM (GP18-21) SIZE", psram_size());
    if(MAX_PROG_SIZE == Option.LIBRARY_FLASH_SIZE) PO2IntH("LIBRARY_FLASH_SIZE ", Option.LIBRARY_FLASH_SIZE);
    if(Option.Invert == true) PO2Str("CONSOLE", "INVERT");
    if(Option.Invert == 2) PO2Str("CONSOLE", "AUTO");
    if(Option.ColourCode == true) PO2Str("COLOURCODE", "ON");
    if(Option.continuation) PO2Str("CONTINUATION LINES", "ON");
    if(Option.PWM == true) PO2Str("POWER PWM", "ON");
    if(Option.Listcase != CONFIG_TITLE) PO2Str("CASE", CaseList[(int)Option.Listcase]);
    if(Option.Tab != 2) PO2Int("TAB", Option.Tab);
    if(Option.DefaultFC !=WHITE || Option.DefaultBC !=BLACK){
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
#ifdef USBKEYBOARD
    if(!(Option.USBKeyboard == NO_KEYBOARD)){
        PO("KEYBOARD"); MMPrintString((char *)KBrdList[(int)Option.USBKeyboard]); 
        if(Option.capslock || Option.numlock!=1 || Option.repeat!=0b00101100){
            PIntComma(Option.capslock);PIntComma(Option.numlock);PIntComma(Option.RepeatStart);
            PIntComma(Option.RepeatRate);
        }
        PRet();
    } 
#else
    if(!(Option.KeyboardConfig == NO_KEYBOARD ||Option.KeyboardConfig == CONFIG_I2C)){
        PO("KEYBOARD"); MMPrintString((char *)KBrdList[(int)Option.KeyboardConfig]); 
        if(Option.capslock || Option.numlock!=1 || Option.repeat!=0b00101100){
            PIntComma(Option.capslock);PIntComma(Option.numlock);PIntComma(Option.repeat>>5);
            PIntComma(Option.repeat & 0x1f);
        }
        PRet();
    } 
    if(!((Option.KEYBOARD_CLOCK==11 && Option.KEYBOARD_DATA==12) ||(Option.KEYBOARD_CLOCK==0 && Option.KEYBOARD_DATA==0)) && Option.KeyboardConfig != NO_KEYBOARD){
        PO("KEYBOARD PINS"); MMPrintString((char *)PinDef[Option.KEYBOARD_CLOCK].pinname);
        MMputchar(',',0);MMPrintString((char *)PinDef[Option.KEYBOARD_DATA].pinname);PRet();
    }
    if(Option.MOUSE_CLOCK){
        PO("MOUSE"); MMPrintString((char *)PinDef[Option.MOUSE_CLOCK].pinname);
        MMputchar(',',0);MMPrintString((char *)PinDef[Option.MOUSE_DATA].pinname);PRet();
    }
#endif   
    if(Option.KeyboardConfig == CONFIG_I2C)PO2Str("KEYBOARD", "I2C");
#ifdef rp2350
    if(Option.NoHeartbeat && rp2350a)PO2Str("HEARTBEAT", "OFF");
#else
    if(Option.NoHeartbeat )PO2Str("HEARTBEAT", "OFF");
#endif
    if(Option.AllPins)PO2Str("PICO", "OFF");
#ifdef PICOMITEVGA
    if(Option.CPU_Speed==Freq720P)PO2StrInt("RESOLUTION", "1280x720",Option.CPU_Speed);
    if(Option.CPU_Speed==FreqXGA)PO2StrInt("RESOLUTION", "1024x768",Option.CPU_Speed);
    if(Option.CPU_Speed==FreqSVGA)PO2StrInt("RESOLUTION", "800x600",Option.CPU_Speed);
    if(Option.CPU_Speed==Freq848)PO2StrInt("RESOLUTION", "848x480",Option.CPU_Speed);
    if(Option.CPU_Speed==Freq400)PO2StrInt("RESOLUTION", "720x400",Option.CPU_Speed);
    if(Option.CPU_Speed==Freq480P || Option.CPU_Speed==Freq252P || Option.CPU_Speed==Freq378P )PO2StrInt("RESOLUTION", "640x480",Option.CPU_Speed);
    if(Option.DISPLAY_TYPE!=SCREENMODE1)PO2Int("DEFAULT MODE", Option.DISPLAY_TYPE-SCREENMODE1+1);
    if(Option.Height != 40 || Option.Width != 80) PO3Int("DISPLAY", Option.Height, Option.Width);
#ifdef HDMI
    if(Option.HDMIclock!=2 || Option.HDMId0!=0 || Option.HDMId1!=6 ||Option.HDMId2!=4){
    PO("HDMI PINS ");PInt(Option.HDMIclock);PIntComma(Option.HDMId0);PIntComma(Option.HDMId1);PIntComma(Option.HDMId2);PRet();
}
#endif
#else
    PO2Int("CPUSPEED (KHz)", Option.CPU_Speed);
    if(Option.DISPLAY_CONSOLE == true) {
        PO("LCDPANEL CONSOLE");
        if(Option.DefaultFont != (Option.DISPLAY_TYPE==SCREENMODE2? (6<<4) | 1 : 0x01 ))PInt((Option.DefaultFont>>4) +1);
        else if(!(Option.DefaultFC==WHITE && Option.DefaultBC==BLACK && Option.DefaultBrightness == 100 && Option.NoScroll==0))MMputchar(',',1);
        if(Option.DefaultFC!=WHITE)PIntHC(Option.DefaultFC);
        else if(!(Option.DefaultBC==BLACK && Option.DefaultBrightness == 100 && Option.NoScroll==0))MMputchar(',',1);
        if(Option.DefaultBC!=BLACK)PIntHC(Option.DefaultBC);
        else if(!(Option.DefaultBrightness == 100 && Option.NoScroll==0))MMputchar(',',1);
        if(Option.DefaultBrightness != 100)PIntComma(Option.DefaultBrightness);
        else if(!(Option.DefaultBrightness == 100 && Option.NoScroll==0))MMputchar(',',1);
        if(Option.NoScroll!=0)MMPrintString(",NOSCROLL");
        PRet();
    }
    if(Option.Height != 24 || Option.Width != 80) PO3Int("DISPLAY", Option.Height, Option.Width);
    if(Option.DISPLAY_TYPE == DISP_USER) PO3Int("LCDPANEL USER", HRes, VRes);
    if(Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < DISP_USER) {
        i=Option.DISPLAY_ORIENTATION;
        if(Option.DISPLAY_TYPE==ST7789 || Option.DISPLAY_TYPE == ST7789A)i=(i+2) % 4;
        PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); MMPrintString(", "); MMPrintString((char *)OrientList[(int)i - 1]);
        MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_CD].pinname);
        MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_Reset].pinname);
        if(Option.DISPLAY_TYPE!=ST7920){
            MMputchar(',',1);;MMPrintString((char *)PinDef[Option.LCD_CS].pinname);
        }
        if(!(Option.DISPLAY_TYPE<=I2C_PANEL || Option.DISPLAY_TYPE>=BufferedPanel ) && Option.DISPLAY_BL){
            MMputchar(',',1);MMPrintString((char *)PinDef[Option.DISPLAY_BL].pinname);
        }  else if(Option.BGR)MMputchar(',',1);
        if(!(Option.DISPLAY_TYPE<=I2C_PANEL || Option.DISPLAY_TYPE>=BufferedPanel ) && Option.BGR){
            MMputchar(',',1);MMPrintString((char *)"INVERT");
        }
        if(Option.DISPLAY_TYPE==SSD1306SPI && Option.I2Coffset)PIntComma(Option.I2Coffset);
        if(Option.DISPLAY_TYPE==N5110 && Option.LCDVOP!=0xC8)PIntComma(Option.LCDVOP);
        MMPrintString("\r\n");
    }
    if(Option.DISPLAY_TYPE > 0 && Option.DISPLAY_TYPE <= I2C_PANEL) {
        PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); MMPrintString(", "); MMPrintString((char *)OrientList[(int)i - 1]);
        if(Option.DISPLAY_TYPE==SSD1306I2C && Option.I2Coffset)PIntComma(Option.I2Coffset);
        MMPrintString("\r\n");
    }
    if(Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL) {
        PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); MMPrintString(", "); 
        MMPrintString((char *)OrientList[(int)i - 1]);
		if(Option.DISPLAY_BL){
            MMputchar(',',1);MMPrintString((char *)PinDef[Option.DISPLAY_BL].pinname);
		} else if(Option.SSD_DC!=(Option.DISPLAY_TYPE> SSD_PANEL_8 ? 16: 13) || Option.SSD_RESET!=(Option.DISPLAY_TYPE > SSD_PANEL_8 ? 19: 16) || (Option.SSD_DATA!=1))MMputchar(',',1);
		if(Option.SSD_DC!=(Option.DISPLAY_TYPE> SSD_PANEL_8 ? 16: 13)){
            MMputchar(',',1);MMPrintString((char *)PinDef[PINMAP[Option.SSD_DC]].pinname);
		} else if(Option.SSD_RESET!=(Option.DISPLAY_TYPE > SSD_PANEL_8 ? 19: 16) || (Option.SSD_DATA!=1))MMputchar(',',1);
		if(Option.SSD_RESET==-1){
            MMputchar(',',1);MMPrintString("NORESET");
		} else if( (Option.SSD_DATA!=1))MMputchar(',',1);
		if(Option.SSD_DATA!=1){
            MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.SSD_DATA].pinname);
		}
        PRet();
    }
    if(Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE<NEXTGEN){
        PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); PRet();
    } 
    #ifdef GUICONTROLS
    if(Option.MaxCtrls)PO2Int("GUI CONTROLS", Option.MaxCtrls-1);
    #endif
    #ifdef PICOMITEWEB
    if(*Option.SSID){
        char password[]="****************************************************************";
        password[strlen((char *)Option.PASSWORD)]=0;
        PO("WIFI");
        MMPrintString((char *)Option.SSID);MMputchar(',',1);MMputchar(' ',1);
        MMPrintString(password);
        MMputchar(',',1);
            MMputchar(' ',1);
        MMPrintString(Option.hostname);
        if(*Option.ipaddress){
            MMputchar(',',1);
            MMputchar(' ',1);
            MMPrintString(Option.ipaddress);
            MMputchar(',',1);
            MMputchar(' ',1);
            MMPrintString(Option.mask);
            MMputchar(',',1);
            MMputchar(' ',1);
            MMPrintString(Option.gateway);
        }
        PRet();
    }
    if(Option.TCP_PORT && Option.ServerResponceTime!=5000)PO3Int("TCP SERVER PORT", Option.TCP_PORT, Option.ServerResponceTime);
    if(Option.TCP_PORT && Option.ServerResponceTime==5000)PO2Int("TCP SERVER PORT", Option.TCP_PORT);
    if(Option.UDP_PORT && Option.UDPServerResponceTime!=5000)PO3Int("UDP SERVER PORT", Option.UDP_PORT, Option.UDPServerResponceTime);
    if(Option.UDP_PORT && Option.UDPServerResponceTime==5000)PO2Int("UDP SERVER PORT", Option.UDP_PORT);
    if(Option.Telnet==1)PO2Str("TELNET", "CONSOLE ON");
    if(Option.Telnet==-1)PO2Str("TELNET", "CONSOLE ONLY");
    if(Option.disabletftp==1)PO2Str("TFTP", "OFF");
    #endif
    if(Option.TOUCH_CS) {
        PO("TOUCH"); 
        if(Option.TOUCH_CAP==1)(MMPrintString("FT6336 "));
        MMPrintString((char *)PinDef[Option.TOUCH_CAP==1 ? Option.TOUCH_IRQ : Option.TOUCH_CS].pinname);MMputchar(',',1);
        MMPrintString((char *)PinDef[Option.TOUCH_CAP==1 ? Option.TOUCH_CS : Option.TOUCH_IRQ].pinname);
        if(Option.TOUCH_Click) {
            MMputchar(',',1);MMPrintString((char *)PinDef[Option.TOUCH_Click].pinname);
        } else if(Option.TOUCH_CAP)MMputchar(',',1);
        if(Option.TOUCH_CAP){
            MMputchar(',',1);PInt(Option.THRESHOLD_CAP);
        }
        MMPrintString("\r\n");
        if(Option.TOUCH_XZERO != 0 || Option.TOUCH_YZERO != 0) {
            MMPrintString("GUI CALIBRATE "); PInt(Option.TOUCH_SWAPXY); PIntComma(Option.TOUCH_XZERO); PIntComma(Option.TOUCH_YZERO);
            PIntComma(Option.TOUCH_XSCALE * 10000); PIntComma(Option.TOUCH_YSCALE * 10000); MMPrintString("\r\n");
        }
    }
#endif
#ifndef PICOMITEVGA
    if(Option.SD_CS){
        PO("SDCARD");
        MMPrintString((char *)PinDef[Option.SD_CS].pinname);
        if(Option.SD_CLK_PIN){
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SD_CLK_PIN].pinname);
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SD_MOSI_PIN].pinname);
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SD_MISO_PIN].pinname);
        }
        MMPrintString("\r\n");
    }
#else
    if(Option.SD_CS){
        PO("SDCARD");
        MMPrintString((char *)PinDef[Option.SD_CS].pinname);
        if(Option.SD_CLK_PIN){
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SD_CLK_PIN].pinname);
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SD_MOSI_PIN].pinname);
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SD_MISO_PIN].pinname);
        } else {
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SYSTEM_CLK].pinname);
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SYSTEM_MOSI].pinname);
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SYSTEM_MISO].pinname);
        }
        MMPrintString("\r\n");
    }
#ifndef HDMI
    PO("VGA PINS");
    MMPrintString((char *)PinDef[Option.VGA_BLUE].pinname);
    MMPrintString("-GP13"); // TODO: detect as +8
    PRet();
#endif
#endif
    if(Option.CombinedCS)PO2Str("SDCARD", "COMBINED CS");
#ifdef USBKEYBOARD
    if(!(Option.RepeatStart==600 && Option.RepeatRate==150)){
    	char buff[40]={0};
    	sprintf(buff,"OPTION KEYBOARD REPEAT %d,%d\r\n",Option.RepeatStart, Option.RepeatRate);
    	MMPrintString(buff);
    }
#endif
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
#ifdef rp2350
    if(Option.PSRAM_CS_PIN!=0)PO2Str("PSRAM PIN", PinDef[Option.PSRAM_CS_PIN].pinname);
#endif
    if((Option.heartbeatpin!=43 && !Option.NoHeartbeat)
#ifdef rp2350
     || (Option.heartbeatpin==43 && !rp2350a && !Option.NoHeartbeat)
#endif
    )PO2Str("HEARTBEAT PIN", PinDef[Option.heartbeatpin].pinname);
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
#ifdef USBKEYBOARD
void fun_keydown(void) {
	int i,n=getint(ep,0,8);
	iret=0;
	while(getConsole() != -1); // clear anything in the input buffer
	if(n==8){
		iret=(caps_lock ? 1: 0) |
				(num_lock ? 2: 0) |
				(scroll_lock ? 4: 0);
	} else if(n){
		iret = KeyDown[n-1];											        // this is the character
	} else {
		for(i=0;i<6;i++){
			if(KeyDown[i])iret++;
		}
	}
	targ=T_INT;
}
#else
void MIPS16 cmd_update(void){
    uint gpio_mask = 0u;
    reset_usb_boot(gpio_mask, PICO_STDIO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK);
}
#endif
void MIPS16 disable_systemspi(void){
    if(!IsInvalidPin(Option.SYSTEM_MOSI))ExtCurrentConfig[Option.SYSTEM_MOSI] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_MISO))ExtCurrentConfig[Option.SYSTEM_MISO] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_CLK))ExtCurrentConfig[Option.SYSTEM_CLK] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_MOSI))ExtCfg(Option.SYSTEM_MOSI, EXT_NOT_CONFIG, 0);
    if(!IsInvalidPin(Option.SYSTEM_MISO))ExtCfg(Option.SYSTEM_MISO, EXT_NOT_CONFIG, 0);
    if(!IsInvalidPin(Option.SYSTEM_CLK))ExtCfg(Option.SYSTEM_CLK, EXT_NOT_CONFIG, 0);
    Option.SYSTEM_MOSI=0;
    Option.SYSTEM_MISO=0;
    Option.SYSTEM_CLK=0;
}
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
#ifdef PICOMITEVGA
    if(!IsInvalidPin(Option.SYSTEM_CLK))ExtCurrentConfig[Option.SYSTEM_CLK] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_CLK))ExtCfg(Option.SYSTEM_CLK, EXT_NOT_CONFIG, 0);
    Option.SYSTEM_CLK=0;
    if(!IsInvalidPin(Option.SYSTEM_MISO))ExtCurrentConfig[Option.SYSTEM_MISO] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_MISO))ExtCfg(Option.SYSTEM_MISO, EXT_NOT_CONFIG, 0);
    Option.SYSTEM_MISO=0;
    if(!IsInvalidPin(Option.SYSTEM_MOSI))ExtCurrentConfig[Option.SYSTEM_MOSI] = EXT_DIG_IN ;   
    if(!IsInvalidPin(Option.SYSTEM_MOSI))ExtCfg(Option.SYSTEM_MOSI, EXT_NOT_CONFIG, 0);
    Option.SYSTEM_MOSI=0;
#endif
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
#ifndef PICOMITEVGA
void MIPS16 ConfigDisplayUser(unsigned char *tp){
    getargs(&tp, 13, (unsigned char *)",");
    if(str_equal2(argv[0], (unsigned char *)"USER")) {
        if(Option.DISPLAY_TYPE) error("Display already configured");
        if(argc != 5) error("Argument count");
        HRes = DisplayHRes = getint(argv[2], 1, 10000);
        VRes = DisplayVRes = getint(argv[4], 1, 10000);
        Option.DISPLAY_TYPE = DISP_USER;
        // setup the pointers to the drawing primitives
        DrawRectangle = DrawRectangleUser;
        DrawBitmap = DrawBitmapUser;
        return;
    }  

}
void MIPS16 clear320(void){
    if(SPI480){
        if(Option.DISPLAY_ORIENTATION & 1) {
            HRes=DisplayHRes;
            VRes=DisplayVRes;
        } else {
            HRes=DisplayVRes;
            VRes=DisplayHRes;
        }
        return;
    }
    screen320=0;
    DrawRectangle = DrawRectangleSSD1963;
    DrawBitmap = DrawBitmapSSD1963;
    DrawBuffer = DrawBufferSSD1963;
    ReadBuffer = ReadBufferSSD1963;
    if(SSD16TYPE || Option.DISPLAY_TYPE==IPS_4_16){
        DrawBLITBuffer= DrawBLITBufferSSD1963;
        ReadBLITBuffer = ReadBLITBufferSSD1963;
    } else {
        DrawBLITBuffer= DrawBufferSSD1963;
        ReadBLITBuffer = ReadBufferSSD1963;
    }
    if(Option.DISPLAY_TYPE!=SSD1963_4_16){
    if(Option.DISPLAY_ORIENTATION & 1) {
        HRes=800;
        VRes=480;
    } else {
        HRes=480;
        VRes=800;
    }
    } else {
    if(Option.DISPLAY_ORIENTATION & 1) {
        HRes=480;
        VRes=272;
    } else {
        HRes=272;
        VRes=480;
    }
    }
    FreeMemorySafe((void **)&buff320);
    return;
}
//void MIPS16 clearSPI320(void){
//    HRes=480;
//    VRes=320;
//    return;
//}

#endif

void MIPS16 configure(CombinedPtr p){
    if(!*p){
        ResetOptions(false);
        _excep_code = RESET_COMMAND;
        SoftReset();
    } else {
        if(checkstring(p,(unsigned char *) "LIST")){
#ifdef PICOMITEVGA
#ifndef HDMI
#ifdef USBKEYBOARD
            MMPrintString("CMM1.5\r\n");
#else
            MMPrintString("Unsupported feature\r\n");
#endif
#else
#ifdef USBKEYBOARD
            MMPrintString("HDMIUSB\r\n");
            MMPrintString("OLIMEX USB\r\n");
            MMPrintString("PICO COMPUTER\r\n");
            MMPrintString("HDMIUSBI2S\r\n");
#else
            MMPrintString("OLIMEX\r\n");
            MMPrintString("HDMIBasic\r\n");
#endif
#endif
#endif
#if defined(PICOMITE) || defined(PICOMITEWEB)
#ifndef USBKEYBOARD
            MMPrintString("Game*Mite\r\n");
            MMPrintString("Pico-ResTouch-LCD-3.5\r\n");
            MMPrintString("Pico-ResTouch-LCD-2.8\r\n");
            MMPrintString("PICO BACKPACK\r\n");
#ifndef PICOMITEWEB
            MMPrintString("RP2040-LCD-1.28\r\n");
            MMPrintString("RP2040LCD-0.96\r\n");
            MMPrintString("RP2040-GEEK\r\n");
#endif
#else
            MMPrintString("USB Edition V1.0\r\n");
#endif
#endif
            return;
        }       
#if 0
#ifdef PICOMITEVGA
#ifndef HDMI
#ifdef USBKEYBOARD
        if(checkstring(p,(unsigned char *) "CMM1.5"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.AllPins = 1; 
            Option.ColourCode = 1;
            Option.SYSTEM_I2C_SDA=PINMAP[14];
            Option.SYSTEM_I2C_SCL=PINMAP[15];
            Option.RTC = true;
            Option.SD_CS=PINMAP[13];
            Option.SYSTEM_CLK=PINMAP[10];
            Option.SYSTEM_MOSI=PINMAP[11];
            Option.SYSTEM_MISO=PINMAP[12];
            Option.VGA_HSYNC=PINMAP[23];
            Option.VGA_BLUE=PINMAP[18];
            Option.AUDIO_L=PINMAP[16];
            Option.AUDIO_R=PINMAP[17];
            Option.modbuffsize=512;
            Option.modbuff = true; 
            Option.AUDIO_SLICE=checkslice(PINMAP[16],PINMAP[17], 0);
            strcpy((char *)Option.platform,"CMM1.5");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
#else
        if(checkstring(p,(unsigned char *) "PICOMITEVGA V1.1"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.AllPins = 1; 
            Option.ColourCode = 1;
            Option.SYSTEM_I2C_SDA=PINMAP[14];
            Option.SYSTEM_I2C_SCL=PINMAP[15];
            Option.RTC = true;
            Option.SD_CS=PINMAP[13];
            Option.SYSTEM_CLK=PINMAP[10];
            Option.SYSTEM_MOSI=PINMAP[11];
            Option.SYSTEM_MISO=PINMAP[12];
            Option.VGA_BLUE=PINMAP[18];
            Option.AUDIO_CS_PIN=PINMAP[24];
            Option.AUDIO_CLK_PIN=PINMAP[22];
            Option.AUDIO_MOSI_PIN=PINMAP[23];
            Option.AUDIO_SLICE=checkslice(PINMAP[22],PINMAP[22], 1);
            Option.modbuffsize=512;
            Option.modbuff = true; 
            strcpy((char *)Option.platform,"PICOMITEVGA V1.1");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
        if(checkstring(p,(unsigned char *) "PICOMITEVGA V1.0"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.AllPins = 1; 
            Option.ColourCode = 1;
            Option.SYSTEM_I2C_SDA=PINMAP[14];
            Option.SYSTEM_I2C_SCL=PINMAP[15];
            Option.RTC = true;
            Option.SD_CS=PINMAP[13];
            Option.SYSTEM_CLK=PINMAP[10];
            Option.SYSTEM_MOSI=PINMAP[11];
            Option.SYSTEM_MISO=PINMAP[12];
            Option.VGA_BLUE=PINMAP[18];
            Option.AUDIO_L=PINMAP[22];
            Option.AUDIO_R=PINMAP[23];
            Option.modbuffsize=512;
            Option.modbuff = true; 
            Option.AUDIO_SLICE=checkslice(PINMAP[22],PINMAP[23], 0);
            strcpy((char *)Option.platform,"PICOMITEVGA V1.0");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
        if(checkstring(p,(unsigned char *) "VGA DESIGN 1"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.ColourCode = 1;
            Option.SYSTEM_CLK=PINMAP[10];
            Option.SYSTEM_MOSI=PINMAP[11];
            Option.SYSTEM_MISO=PINMAP[12];
            Option.SD_CS=PINMAP[13];
            Option.VGA_BLUE=PINMAP[18];
            strcpy((char *)Option.platform,"VGA Design 1");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
        if(checkstring(p,(unsigned char *) "VGA DESIGN 2"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.ColourCode = 1;
            Option.SYSTEM_I2C_SDA=PINMAP[14];
            Option.SYSTEM_I2C_SCL=PINMAP[15];
            Option.RTC = true;
            Option.SYSTEM_CLK=PINMAP[10];
            Option.SYSTEM_MOSI=PINMAP[11];
            Option.SYSTEM_MISO=PINMAP[12];
            Option.SD_CS=PINMAP[13];
            Option.VGA_BLUE=PINMAP[18];
            Option.AUDIO_L=PINMAP[6];
            Option.AUDIO_R=PINMAP[7];
            Option.modbuffsize=192;
            Option.modbuff = true; 
            Option.AUDIO_SLICE=checkslice(PINMAP[6],PINMAP[7], 0);
            strcpy((char *)Option.platform,"VGA Design 2");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
/*OPTION PLATFORM "SWEETIEPI"
OPTION CPUSPEED 252000
OPTION PICO OFF
OPTION SYSTEM I2C GP0, GP1
OPTION SDCARD GP29, GP3, GP4, GP2
OPTION AUDIO SPI GP5, GP6, GP7
OPTION VGA PINS GP14, GP10*/
        if(checkstring(p,(unsigned char *) "SWEETIEPI"))  {
            Option.AllPins = 1; 
            Option.ColourCode = 1;
            Option.SYSTEM_I2C_SDA=PINMAP[0];
            Option.SYSTEM_I2C_SCL=PINMAP[1];
            Option.SD_CS=PINMAP[29];
            Option.SD_CLK_PIN=PINMAP[3];
            Option.SD_MOSI_PIN=PINMAP[4];
            Option.SD_MISO_PIN=PINMAP[2];
            Option.VGA_BLUE=PINMAP[10];
            Option.AUDIO_CS_PIN=PINMAP[5];
            Option.AUDIO_CLK_PIN=PINMAP[6];
            Option.AUDIO_MOSI_PIN=PINMAP[7];
            Option.AUDIO_SLICE=checkslice(PINMAP[6],PINMAP[6], 1);
            Option.modbuffsize=192;
            Option.modbuff = true; 
            strcpy((char *)Option.platform,"SWEETIEPI");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
/*OPTION PLATFORM VGA BASIC

option reset
option vga pins GP16, GP18
option sdcard GP14,  GP13, GP15, GP12
option audio GP6, GP7
option system i2c GP26, GP27*/
        if(checkstring(p,(unsigned char *) "VGA BASIC"))  {
            Option.ColourCode = 1;
            Option.SYSTEM_I2C_SDA=PINMAP[0];
            Option.SYSTEM_I2C_SCL=PINMAP[1];
            Option.SD_CS=PINMAP[14];
            Option.SD_CLK_PIN=PINMAP[13];
            Option.SD_MOSI_PIN=PINMAP[15];
            Option.SD_MISO_PIN=PINMAP[12];
            Option.VGA_BLUE=PINMAP[18];
            Option.AUDIO_L=PINMAP[6];
            Option.AUDIO_R=PINMAP[7];
            Option.modbuffsize=192;
            Option.modbuff = true; 
            Option.AUDIO_SLICE=checkslice(PINMAP[6],PINMAP[7], 0);
            strcpy((char *)Option.platform,"VGA BASIC");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
#endif
#endif
#else
#ifdef USBKEYBOARD
/*
OPTION SERIAL CONSOLE COM2,GP8,GP9
OPTION SYSTEM I2C GP20,GP21
OPTION FLASH SIZE 4194304
OPTION COLOURCODE ON
OPTION KEYBOARD US
OPTION CPUSPEED (KHz) 315000
OPTION SDCARD GP22, GP26, GP27, GP28
OPTION AUDIO GP10,GP11', ON PWM CHANNEL 5
OPTION RTC AUTO ENABLE
OPTION MODBUFF ENABLE  192
OPTION PLATFORM HDMIUSB
*/
        if(checkstring(p,(unsigned char *) "HDMIUSB") || checkstring(p,(unsigned char *) "PICO COMPUTER") )  {
            ResetOptions(false);
            if(checkstring(p,(unsigned char *) "HDMIUSB") )strcpy((char *)Option.platform,"HDMIUSB");
            else strcpy((char *)Option.platform,"PICO COMPUTER");
            Option.ColourCode = 1;
            Option.CPU_Speed =Freq480P;
            Option.SD_CS=PINMAP[22];
            Option.SD_CLK_PIN=PINMAP[26];
            Option.SD_MOSI_PIN=PINMAP[27];
            Option.SD_MISO_PIN=PINMAP[28];
            Option.AUDIO_L=PINMAP[10];
            Option.AUDIO_R=PINMAP[11];
            Option.modbuffsize=192;
            Option.modbuff = true; 
            Option.AUDIO_SLICE=checkslice(PINMAP[10],PINMAP[11], 0);
            Option.SYSTEM_I2C_SDA=PINMAP[20];
            Option.SYSTEM_I2C_SCL=PINMAP[21];
            Option.RTC = true;
            Option.SerialTX=PINMAP[8];
            Option.SerialRX=PINMAP[9];
            Option.SerialConsole=2;
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
        if(checkstring(p,(unsigned char *) "HDMIUSBI2S"))  {
            if(rp2350a)error("RP350B chips only");
            ResetOptions(false);
            strcpy((char *)Option.platform,"HDMIUSBI2S");
            Option.heartbeatpin=PINMAP[25];
            Option.NoHeartbeat=false;
            Option.ColourCode = 1;
            Option.modbuffsize=512;
            Option.modbuff = true; 
            Option.audio_i2s_bclk=PINMAP[10];
            Option.audio_i2s_data=PINMAP[22];
            Option.AUDIO_SLICE=11;
            Option.SD_CS=PINMAP[29];
            Option.SD_CLK_PIN=PINMAP[30];
            Option.SD_MOSI_PIN=PINMAP[31];
            Option.SD_MISO_PIN=PINMAP[32];
            Option.SYSTEM_I2C_SDA=PINMAP[20];
            Option.SYSTEM_I2C_SCL=PINMAP[21];
            Option.RTC = true;
            Option.HDMIclock=1;
            Option.HDMId0=3;
            Option.HDMId1=5;
            Option.HDMId2=7;
            Option.SerialTX=PINMAP[8];
            Option.SerialRX=PINMAP[9];
            Option.SerialConsole=2;
//            Option.PSRAM_CS_PIN=PINMAP[47];
            Option.INT1pin = PINMAP[0];
            Option.INT2pin = PINMAP[1];
            Option.INT3pin = PINMAP[2];
            Option.INT4pin = PINMAP[3];
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
        if(checkstring(p,(unsigned char *) "OLIMEXUSB"))  {
            ResetOptions(false);
            strcpy((char *)Option.platform,"OLIMEX USB");
            Option.ColourCode = 1;
            Option.AUDIO_L=PINMAP[26];
            Option.AUDIO_R=PINMAP[27];
            Option.modbuffsize=192;
            Option.modbuff = true; 
            Option.AUDIO_SLICE=checkslice(PINMAP[26],PINMAP[27], 0);
            Option.SD_CS=PINMAP[22];
            Option.SD_CLK_PIN=PINMAP[6];
            Option.SD_MOSI_PIN=PINMAP[7];
            Option.SD_MISO_PIN=PINMAP[4];
            Option.HDMIclock=1;
            Option.HDMId0=3;
            Option.HDMId1=7;
            Option.HDMId2=5;
            Option.SerialTX=PINMAP[0];
            Option.SerialRX=PINMAP[1];
            Option.SerialConsole=1;
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
        
#else
        if(checkstring(p,(unsigned char *) "HDMIBASIC"))  {
            ResetOptions(false);
            strcpy((char *)Option.platform,"HDMIbasic");
            Option.ColourCode = 1;
            Option.SD_CS=7;
            Option.SD_CLK_PIN=4;
            Option.SD_MOSI_PIN=5;
            Option.SD_MISO_PIN=6;
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
        if(checkstring(p,(unsigned char *) "OLIMEX"))  {
            ResetOptions(false);
            strcpy((char *)Option.platform,"OLIMEX");
            Option.ColourCode = 1;
            Option.AUDIO_L=PINMAP[26];
            Option.AUDIO_R=PINMAP[27];
            Option.modbuffsize=192;
            Option.modbuff = true; 
            Option.AUDIO_SLICE=checkslice(PINMAP[26],PINMAP[27], 0);
            Option.SD_CS=PINMAP[22];
            Option.SD_CLK_PIN=PINMAP[6];
            Option.SD_MOSI_PIN=PINMAP[7];
            Option.SD_MISO_PIN=PINMAP[4];
            Option.HDMIclock=1;
            Option.HDMId0=3;
            Option.HDMId1=7;
            Option.HDMId2=5;
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
#endif
#endif
#if defined(PICOMITE) || defined(PICOMITEWEB)
#ifndef USBKEYBOARD
/*OPTION PLATFORM "Game*Mite"
OPTION SYSTEM SPI GP6,GP3,GP4
OPTION CPUSPEED 252000
OPTION LCDPANEL ILI9341,RLANDSCAPE,GP2,GP1,GP0
OPTION TOUCH GP5,GP7
OPTION SDCARD GP22
OPTION AUDIO GP20,GP21
OPTION MODBUFF ENABLE 192 */
       if(checkstring(p,(unsigned char *) "GAMEMITE"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.ColourCode = 1;
            Option.SYSTEM_CLK=PINMAP[6];
            Option.SYSTEM_MOSI=PINMAP[3];
            Option.SYSTEM_MISO=PINMAP[4];
            Option.AUDIO_L=PINMAP[20];
            Option.AUDIO_R=PINMAP[21];
            Option.modbuffsize=192;
            Option.DISPLAY_TYPE=ILI9341;
            Option.LCD_CD=PINMAP[2];
            Option.LCD_Reset=PINMAP[1];
            Option.LCD_CS=PINMAP[0];
            Option.TOUCH_CS=PINMAP[5];
            Option.TOUCH_IRQ=PINMAP[7];
            Option.SD_CS=PINMAP[22];
            Option.modbuff = true; 
            Option.DISPLAY_ORIENTATION=3;
            Option.AUDIO_SLICE=checkslice(PINMAP[20],PINMAP[21], 0);
            Option.TOUCH_SWAPXY = 0;
            Option.TOUCH_XZERO = 407;
            Option.TOUCH_YZERO = 267;
            Option.TOUCH_XSCALE = 0.0897;
            Option.TOUCH_YSCALE = 0.0677;
            strcpy((char *)Option.platform,"Game*Mite");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
       }
       if(checkstring(p,(unsigned char *) "PICORESTOUCHLCD3.5"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.ColourCode = 1;
            Option.SYSTEM_CLK=PINMAP[10];
            Option.SYSTEM_MOSI=PINMAP[11];
            Option.SYSTEM_MISO=PINMAP[12];
            Option.modbuffsize=192;
            Option.DISPLAY_TYPE=ILI9488W;
            Option.LCD_CD=PINMAP[8];
            Option.LCD_Reset=PINMAP[15];
            Option.LCD_CS=PINMAP[9];
            Option.TOUCH_CS=PINMAP[16];
            Option.TOUCH_IRQ=PINMAP[17];
            Option.SD_CS=PINMAP[22];
            Option.DISPLAY_BL=PINMAP[13];
            Option.modbuff = true; 
            Option.DISPLAY_ORIENTATION=1;
            Option.TOUCH_SWAPXY = 0;
            Option.TOUCH_XZERO = 3963;
            Option.TOUCH_YZERO = 216;
            Option.TOUCH_XSCALE = -0.1285;
            Option.TOUCH_YSCALE = 0.0859;
            strcpy((char *)Option.platform,"Pico-ResTouch-LCD-3.5");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
       }
       if(checkstring(p,(unsigned char *) "PICO BACKPACK"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.ColourCode = 1;
            Option.SYSTEM_CLK=PINMAP[18];
            Option.SYSTEM_MOSI=PINMAP[19];
            Option.SYSTEM_MISO=PINMAP[16];
            Option.DISPLAY_TYPE=ILI9341;
            Option.LCD_CD=PINMAP[20];
            Option.LCD_Reset=PINMAP[21];
            Option.LCD_CS=PINMAP[17];
            Option.TOUCH_CS=PINMAP[14];
            Option.TOUCH_IRQ=PINMAP[15];
            Option.SD_CS=PINMAP[22];
            Option.DISPLAY_ORIENTATION=1;
            Option.TOUCH_SWAPXY = 0;
            Option.TOUCH_XZERO = 3963;
            Option.TOUCH_YZERO = 216;
            Option.TOUCH_XSCALE = -0.1285;
            Option.TOUCH_YSCALE = 0.0859;
            strcpy((char *)Option.platform,"Pico Backpack");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
       }
       if(checkstring(p,(unsigned char *) "PICORESTOUCHLCD2.8"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.ColourCode = 1;
            Option.SYSTEM_CLK=PINMAP[10];
            Option.SYSTEM_MOSI=PINMAP[11];
            Option.SYSTEM_MISO=PINMAP[12];
            Option.modbuffsize=192;
            Option.DISPLAY_TYPE=ST7789B;
            Option.LCD_CD=PINMAP[8];
            Option.LCD_Reset=PINMAP[15];
            Option.LCD_CS=PINMAP[9];
            Option.TOUCH_CS=PINMAP[16];
            Option.TOUCH_IRQ=PINMAP[17];
            Option.SD_CS=PINMAP[22];
            Option.DISPLAY_BL=PINMAP[13];
            Option.modbuff = true; 
            Option.DISPLAY_ORIENTATION=1;
            Option.TOUCH_SWAPXY = 0;
            Option.TOUCH_XZERO = 373;
            Option.TOUCH_YZERO = 3859;
            Option.TOUCH_XSCALE = 0.0894;
            Option.TOUCH_YSCALE = -0.0657;
            strcpy((char *)Option.platform,"Pico-ResTouch-LCD-2.8");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
       }
#ifndef PICOMITEWEB
       if(checkstring(p,(unsigned char *) "RP2040LCD1.28"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.AllPins = 1; 
            Option.ColourCode = 1;
            Option.NoHeartbeat = 1;
            Option.SYSTEM_CLK=PINMAP[10];
            Option.SYSTEM_MOSI=PINMAP[11];
            Option.SYSTEM_MISO=PINMAP[28];
            Option.DISPLAY_TYPE=GC9A01;
            Option.LCD_CD=PINMAP[8];
            Option.LCD_Reset=PINMAP[12];
            Option.LCD_CS=PINMAP[9];
            Option.DISPLAY_BL=PINMAP[25];
            Option.DISPLAY_ORIENTATION=1;
            Option.SYSTEM_I2C_SDA=PINMAP[6];
            Option.SYSTEM_I2C_SCL=PINMAP[7];
            strcpy((char *)Option.platform,"RP2040-LCD-1.28");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
       }
       if(checkstring(p,(unsigned char *) "RP2040LCD0.96"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.ColourCode = 1;
            Option.NoHeartbeat = 1;
            Option.SYSTEM_CLK=PINMAP[10];
            Option.SYSTEM_MOSI=PINMAP[11];
            Option.SYSTEM_MISO=PINMAP[28];
            Option.DISPLAY_TYPE=ST7735S;
            Option.LCD_CD=PINMAP[8];
            Option.LCD_Reset=PINMAP[12];
            Option.LCD_CS=PINMAP[9];
            Option.DISPLAY_BL=PINMAP[25];
            Option.DISPLAY_ORIENTATION=1;
            strcpy((char *)Option.platform,"RP2040-LCD-0.96");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
       }
       if(checkstring(p,(unsigned char *) "RP2040GEEK"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.ColourCode = 1;
            Option.NoHeartbeat = 1;
            Option.AllPins = 1; 
            Option.SYSTEM_CLK=PINMAP[10];
            Option.SYSTEM_MOSI=PINMAP[11];
            Option.SYSTEM_MISO=PINMAP[24];
            Option.DISPLAY_TYPE=ST7789A;
            Option.LCD_CD=PINMAP[8];
            Option.LCD_Reset=PINMAP[12];
            Option.LCD_CS=PINMAP[9];
            Option.SD_CS=PINMAP[23];
            Option.SD_CLK_PIN=PINMAP[18];
            Option.SD_MOSI_PIN=PINMAP[19];
            Option.SD_MISO_PIN=PINMAP[20];
            Option.DISPLAY_BL=PINMAP[25];
            Option.DISPLAY_ORIENTATION=1;
            strcpy((char *)Option.platform,"RP2040-GEEK");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
       }
#endif
#else
       if(checkstring(p,(unsigned char *) "USB Edition V1.0"))  {
            ResetOptions(false);
            Option.CPU_Speed=252000;
            Option.ColourCode = 1;
            Option.NoHeartbeat = 1;
            Option.AllPins = 1; 
            Option.SYSTEM_I2C_SDA=PINMAP[24];
            Option.SYSTEM_I2C_SCL=PINMAP[25];
            Option.RTC = true;
            Option.TOUCH_CS=PINMAP[21];
            Option.TOUCH_IRQ=PINMAP[19];
            Option.SYSTEM_CLK=PINMAP[22];
            Option.SYSTEM_MOSI=PINMAP[23];
            Option.SYSTEM_MISO=PINMAP[20];
            Option.AUDIO_L=PINMAP[26];
            Option.AUDIO_R=PINMAP[27];
            Option.SerialTX=PINMAP[28];
            Option.SerialRX=PINMAP[29];
            Option.SerialConsole=1;
            Option.CombinedCS=1;
            Option.SD_CS=0;
            Option.modbuffsize=512;
            Option.modbuff = true; 
            Option.AUDIO_SLICE=checkslice(PINMAP[26],PINMAP[27], 0);
            strcpy((char *)Option.platform,"USB Edition V1.0");
            SaveOptions();
            printoptions();uSec(100000);
            _excep_code = RESET_COMMAND;
            SoftReset();
        }
#endif
#endif
#endif
        error("Invalid board for this firmware");
    }
}
void cmd_configure(void){
    configure(cmdline);
}
void MIPS16 cmd_option(void) {
    CombinedPtr tp;
 
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

#ifndef PICOMITEVGA
	tp = checkstring(cmdline, (unsigned char *)"LCD320");
	if(tp) {
        if(!( SSD16TYPE  && (Option.DISPLAY_ORIENTATION==LANDSCAPE || Option.DISPLAY_ORIENTATION==RLANDSCAPE))) error("Only available on 16-bit SSD1963 and IPS_4_16 displays in Landscape");
        if(( SSD16TYPE || Option.DISPLAY_TYPE==IPS_4_16)){
            if(checkstring(tp, (unsigned char *)"OFF"))	{ 
                clear320();
            }
            else if(checkstring(tp, (unsigned char *)"ON"))	{ 
                screen320=1; 
                DrawRectangle = DrawRectangle320;
                DrawBitmap = DrawBitmap320;
                DrawBuffer = DrawBuffer320;
                ReadBuffer = ReadBuffer320;
                DrawBLITBuffer= DrawBLITBuffer320;
                ReadBLITBuffer = ReadBLITBuffer320;
                HRes=320;
                VRes=240;
                buff320=GetMemory(320*6);
                return; 
            } else error("Syntax");
        } else error("Invalid display type");
	}
#endif 
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
#ifdef rp2350
#ifdef HDMI
    tp = checkstring(cmdline, (unsigned char *)"HDMI PINS");
    if(tp) {
        getargs(&tp,7,(unsigned char *)",");
    	if(CurrentLinePtr) error("Invalid in a program");
        if(argc!=7)error("Syntax");
        uint8_t clock=getint(argv[0],0,7);
        uint8_t d0=getint(argv[2],0,7);
        uint8_t d1=getint(argv[4],0,7);
        uint8_t d2=getint(argv[6],0,7);
        if((clock & 0x6)==(d0 & 0x6) || (clock & 0x6)==(d1 & 0x6) || (clock & 0x6)==(d2 & 0x6) || (d0 & 0x6)==(d1 & 0x6) || (d0 & 0x6)==(d2 & 0x6) || (d1 & 0x6)==(d2 & 0x6))error("Channels not unique");
        Option.HDMIclock=clock;
        Option.HDMId0=d0;
        Option.HDMId1=d1;
        Option.HDMId2=d2;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
#endif
    tp = checkstring(cmdline, (unsigned char *)"PSRAM PIN");
    if(tp) {
		if(checkstring(tp, (unsigned char *)"DISABLE")){
            Option.PSRAM_CS_PIN=0;
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;
        }
        int pin1;
        unsigned char code;
        getargs(&tp,1,(unsigned char *)",");
    	if(CurrentLinePtr) error("Invalid in a program");
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        pin1 = getinteger(argv[0]);
        if(!code)pin1=codemap(pin1);
        if(IsInvalidPin(pin1)) error("Invalid pin");
        if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin | is in use",pin1);
        if(!(pin1==1 || pin1==11 || pin1==25 || pin1==62))error("Invalid pin for PSRAM chip select (GP0,GP8,GP19,GP47)");
        Option.PSRAM_CS_PIN=pin1;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
#endif
#ifdef USBKEYBOARD
    tp = checkstring(cmdline, (unsigned char *)"KEYBOARD REPEAT");
	if(tp) {
		getargs(&tp,3,(unsigned char *)",");
		Option.RepeatStart=getint(argv[0],100,2000);
		Option.RepeatRate=getint(argv[2],25,2000);
		SaveOptions();
		return;
	}

#else
    tp = checkstring(cmdline, (unsigned char *)"PS2 PINS");
    if(tp== nullptr)tp = checkstring(cmdline, (unsigned char *)"KEYBOARD PINS");
	if(tp) {
        int pin1,pin2;
        unsigned char code;
		getargs(&tp,3,(unsigned char *)",");
    	if(CurrentLinePtr) error("Invalid in a program");
        if(Option.KEYBOARD_CLOCK)error("Keyboard must be disabled to change pins");
        if(argc!=3)error("Syntax");
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
        Option.KEYBOARD_CLOCK=pin1;
        Option.KEYBOARD_DATA=pin2;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
	}
    tp = checkstring(cmdline, (unsigned char *)"MOUSE");
	if(tp) {
    	if(CurrentLinePtr) error("Invalid in a program");
        if(checkstring(tp,(unsigned char *)"DISABLE")){
            Option.MOUSE_CLOCK=0;
            Option.MOUSE_DATA=0;
        } else {
            int pin1,pin2;
            unsigned char code;
            getargs(&tp,3,(unsigned char *)",");
            if(Option.MOUSE_CLOCK)error("Mouse must be disabled to change pins");
            if(argc!=3)error("Syntax");
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
            Option.MOUSE_CLOCK=pin1;
            Option.MOUSE_DATA=pin2;
        }
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
	}

#endif
    tp = checkstring(cmdline, (unsigned char *)"KEYBOARD");
	if(tp) {
    	if(CurrentLinePtr) error("Invalid in a program");
#ifndef USBKEYBOARD
		if(checkstring(tp, (unsigned char *)"DISABLE")){
			Option.KeyboardConfig = NO_KEYBOARD;
            Option.capslock=0;
            Option.numlock=0;
            Option.KEYBOARD_CLOCK=0;
            Option.KEYBOARD_DATA=0;
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
		} else {
#endif
        getargs(&tp,9,(unsigned char *)",");
#ifndef USBKEYBOARD
        if(!Option.KEYBOARD_CLOCK){
            Option.KEYBOARD_CLOCK=KEYBOARDCLOCK;
            Option.KEYBOARD_DATA=KEYBOARDDATA;
        }
        if(ExtCurrentConfig[Option.KEYBOARD_CLOCK] != EXT_NOT_CONFIG && Option.KeyboardConfig == NO_KEYBOARD)  error("Pin %/| is in use",Option.KEYBOARD_CLOCK,Option.KEYBOARD_CLOCK);
        if(ExtCurrentConfig[Option.KEYBOARD_DATA] != EXT_NOT_CONFIG && Option.KeyboardConfig == NO_KEYBOARD)  error("Pin %/| is in use",Option.KEYBOARD_DATA,Option.KEYBOARD_DATA);

        if(checkstring(argv[0], (unsigned char *)"US"))	Option.KeyboardConfig = CONFIG_US;
		else if(checkstring(argv[0], (unsigned char *)"FR"))	Option.KeyboardConfig = CONFIG_FR;
		else if(checkstring(argv[0], (unsigned char *)"GR"))	Option.KeyboardConfig = CONFIG_GR;
		else if(checkstring(argv[0], (unsigned char *)"IT"))	Option.KeyboardConfig = CONFIG_IT;
		else if(checkstring(argv[0], (unsigned char *)"UK"))	Option.KeyboardConfig = CONFIG_UK;
		else if(checkstring(argv[0], (unsigned char *)"ES"))	Option.KeyboardConfig = CONFIG_ES;
		else if(checkstring(argv[0], (unsigned char *)"BE"))	Option.KeyboardConfig = CONFIG_BE;
		else if(checkstring(argv[0], (unsigned char *)"BR"))	Option.KeyboardConfig = CONFIG_BR;
		else if(checkstring(argv[0], (unsigned char *)"I2C"))   Option.KeyboardConfig = CONFIG_I2C;
#else
        if(checkstring(argv[0], (unsigned char *)"US"))	Option.USBKeyboard = CONFIG_US;
		else if(checkstring(argv[0], (unsigned char *)"FR"))	Option.USBKeyboard = CONFIG_FR;
		else if(checkstring(argv[0], (unsigned char *)"GR"))	Option.USBKeyboard = CONFIG_GR;
		else if(checkstring(argv[0], (unsigned char *)"IT"))	Option.USBKeyboard = CONFIG_IT;
		else if(checkstring(argv[0], (unsigned char *)"UK"))	Option.USBKeyboard = CONFIG_UK;
		else if(checkstring(argv[0], (unsigned char *)"ES"))	Option.USBKeyboard = CONFIG_ES;
#endif
        else error("Syntax");
        Option.capslock=0;
        Option.numlock=1;
#ifndef USBKEYBOARD
        int rs=0b00100000;
        int rr=0b00001100;
        if(Option.KeyboardConfig!=CONFIG_I2C){
            if(argc>=3 && *argv[2])Option.capslock=getint(argv[2],0,1);
            if(argc>=5 && *argv[4])Option.numlock=getint(argv[4],0,1);
            if(argc>=7 && *argv[6])rs=getint(argv[6],0,3)<<5;
            if(argc==9 && *argv[8])rr=getint(argv[8],0,31);
            Option.repeat = rs | rr;
        } else {
            if(!Option.SYSTEM_I2C_SCL)error("Option System I2C not set");
        }
#else
        if(argc>=3 && *argv[2])Option.capslock=getint(argv[2],0,1);
        if(argc>=5 && *argv[4])Option.numlock=getint(argv[4],0,1);
		if(argc>=7 && *argv[6])Option.RepeatStart=getint(argv[6],100,2000);
		if(argc>=9 && *argv[8])Option.RepeatRate=getint(argv[8],25,2000);
#endif        
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
		}
#ifndef USBKEYBOARD
	}
#endif

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
//        unsigned char *p= nullptr;
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
#ifndef PICOMITEWEB
    tp = checkstring(cmdline, (unsigned char *)"PICO");
    if(tp) {
#ifdef rp2350
        if(!rp2350a)error("Invalid for RP2350B");
#endif
        if(checkstring(tp, (unsigned char *)"OFF") || checkstring(tp, (unsigned char *)"DISABLE"))      Option.AllPins = 1; 
        else if(checkstring(tp, (unsigned char *)"ON") || checkstring(tp, (unsigned char *)"ENABLE"))      Option.AllPins = 0; 
        else error("Syntax");
        SaveOptions();
        if(Option.AllPins==0){
            if(CheckPin(41, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))ExtCfg(41,EXT_DIG_OUT,Option.PWM);
            if(CheckPin(42, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))ExtCfg(42,EXT_DIG_IN,0);
            if(CheckPin(44, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))ExtCfg(44,EXT_ANA_IN,0);
        } else {
            if(CheckPin(41, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))ExtCfg(41, EXT_NOT_CONFIG, 0); 
            if(CheckPin(42, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))ExtCfg(42, EXT_NOT_CONFIG, 0); 
            if(CheckPin(44, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED))ExtCfg(44, EXT_NOT_CONFIG, 0); 
        }
        return;
    }
#endif
tp = checkstring(cmdline, (unsigned char *)"DEFAULT COLOURS");
if(tp== nullptr)tp = checkstring(cmdline, (unsigned char *)"DEFAULT COLORS");
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
tp = checkstring(cmdline, (unsigned char *)"HEARTBEAT");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"OFF") || checkstring(tp, (unsigned char *)"DISABLE"))      Option.NoHeartbeat = 1; 
        else {
#ifdef PICOMITEWEB
            if(checkstring(tp, (unsigned char *)"ON") || checkstring(tp, (unsigned char *)"ENABLE"))      Option.NoHeartbeat = 0; 
            else error("Syntax");
            SaveOptions();
            return;
        }
#else
            CombinedPtr p=checkstring(tp, (unsigned char *)"ON");
            if(p== nullptr)p=checkstring(tp, (unsigned char *)"ENABLE");
                if(p){
                    getargs(&p,1,(unsigned char *)",");
                    if(argc){
                        unsigned char code,pin1;
                        if(!(code=codecheck(p)))p+=2;
                        pin1 = getinteger(p);
                        if(!code)pin1=codemap(pin1);
                        if(IsInvalidPin(pin1)) error("Invalid pin");
                        if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin1,pin1);
                        Option.NoHeartbeat = 0;
                        Option.heartbeatpin=pin1;
                        SaveOptions();
                        _excep_code = RESET_COMMAND;
                        SoftReset();
                    } else Option.NoHeartbeat = 0; 
                } else error("Syntax");
            }
        SaveOptions();
        if(CheckPin(HEARTBEATpin, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)){
            if(Option.NoHeartbeat==0){
                gpio_init(PinDef[HEARTBEATpin].GPno);
                gpio_set_dir(PinDef[HEARTBEATpin].GPno, GPIO_OUT);
                ExtCurrentConfig[PinDef[HEARTBEATpin].pin]=EXT_HEARTBEAT;
            } else ExtCfg(HEARTBEATpin, EXT_NOT_CONFIG, 0); 
        } else error("Pin %/| is reserved", HEARTBEATpin, HEARTBEATpin);
#endif
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"LCDPANEL NOCONSOLE");
    if(tp){
   	    if(CurrentLinePtr) error("Invalid in a program");
        Option.Height = SCREENHEIGHT; Option.Width = SCREENWIDTH;
        Option.DISPLAY_CONSOLE = 0;
        Option.DefaultFC = WHITE;
        Option.DefaultBC = BLACK;
        SetFont((Option.DefaultFont = (Option.DISPLAY_TYPE==SCREENMODE2? (6<<4) | 1 : 0x01 )));
        Option.DefaultBrightness = 100;
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
        Option.DefaultBrightness = 100;
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
                Option.DefaultBrightness = getint(argv[6], 0, 100);
            }
            if(argc==9){
                if(checkstring(argv[8],(unsigned char *)"NOSCROLL")){
                    if(!FASTSCROLL)Option.NoScroll=1;
                    else error("Invalid for this display");
                } else error("Syntax");
            }
        }
        if(Option.DISPLAY_BL){
			MMFLOAT frequency=1000.0,duty=Option.DefaultBrightness;
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
#ifdef PICOMITEVGA
#ifdef HDMI
        int fcolour=(FullColour ? RGB555(Option.DefaultFC) : RGB332(Option.DefaultFC));
        int bcolour=(FullColour ? RGB555(Option.DefaultBC) : RGB332(Option.DefaultBC));
#else
        int  fcolour = RGB121pack(Option.DefaultFC);
        int bcolour = RGB121pack(Option.DefaultBC);
#endif
        for(int xp=0;xp<X_TILE;xp++){
            for(int yp=0;yp<Y_TILE;yp++){
#ifdef HDMI
                if(FullColour){
#endif
                    if(fcolour!=0xFFFFFFFF) tilefcols[yp*X_TILE+xp]=(uint16_t)fcolour;
                    if(bcolour!=0xFFFFFFFF) tilebcols[yp*X_TILE+xp]=(uint16_t)bcolour;
#ifdef HDMI
                } else {
                    if(fcolour!=0xFFFFFFFF) tilefcols_w[yp*X_TILE+xp]=(uint8_t)fcolour;
                    if(bcolour!=0xFFFFFFFF) tilebcols_w[yp*X_TILE+xp]=(uint8_t)bcolour;
                }
#endif
            }
        }
#endif
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
#ifdef PICOMITEWEB
	tp = checkstring(cmdline, (unsigned char *)"WEB MESSAGES");
	if(tp) {
		if(checkstring(tp, (unsigned char *)"OFF"))	{ optionsuppressstatus=1; return; }
		if(checkstring(tp, (unsigned char *)"ON"))	{ optionsuppressstatus=0; return; }
	}
    tp = checkstring(cmdline, (unsigned char *)"WIFI");
    if(tp) {
        setwifi(tp);
         _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"TCP SERVER PORT");
    if(tp) {
        getargs(&tp,3,(unsigned char *)",");
   	    if(CurrentLinePtr) error("Invalid in a program");
        Option.TCP_PORT=getint(argv[0],0,65535);
        Option.ServerResponceTime=5000;
        if(argc==3)Option.ServerResponceTime=getint(argv[2],1000,20000);
        SaveOptions();
         _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"UDP SERVER PORT");
    if(tp) {
        getargs(&tp,3,(unsigned char *)",");
   	    if(CurrentLinePtr) error("Invalid in a program");
        Option.UDP_PORT=getint(argv[0],0,65535);
        Option.UDPServerResponceTime=5000;
        if(argc==3)Option.UDPServerResponceTime=getint(argv[2],1000,20000);
        SaveOptions();
         _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"TELNET CONSOLE");
    if(tp) {
   	    if(CurrentLinePtr) error("Invalid in a program");
        if(checkstring(tp, (unsigned char *)"OFF"))Option.Telnet=0;
        else if(checkstring(tp, (unsigned char *)"ON"))Option.Telnet=1;
        else if(checkstring(tp, (unsigned char *)"ONLY")) Option.Telnet=-1;
        else error("Syntax");
        SaveOptions();
         _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"TFTP");
    if(tp) {
   	    if(CurrentLinePtr) error("Invalid in a program");
        if(checkstring(tp, (unsigned char *)"OFF"))Option.disabletftp=1;
        else if(checkstring(tp, (unsigned char *)"ON"))Option.disabletftp=0;
        else if(checkstring(tp, (unsigned char *)"ENABLE"))Option.disabletftp=0;
        else if(checkstring(tp, (unsigned char *)"DISABLE"))Option.disabletftp=1;
        else error("Syntax");
        SaveOptions();
         _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }

#endif

#ifdef PICOMITEVGA
    tp = checkstring(cmdline, (unsigned char *)"RESOLUTION");
    if(tp) {
        getargs(&tp,3,(unsigned char *)",");
    	if(CurrentLinePtr) error("Invalid in a program");
        if((checkstring(argv[0], (unsigned char *)"640")) || (checkstring(argv[0], (unsigned char *)"640x480"))){
            if(argc==3){
#ifdef HDMI
                int i=getint(argv[2],Freq252P,Freq378P);
                if(!(i==Freq252P || i==Freq480P || i==Freq378P))error("Invalid speed");
#else
                int i=getint(argv[2],Freq252P,Freq378P);
                if(!(i==Freq252P || i==Freq480P || i==Freq378P))error("Invalid speed");
#endif
                Option.CPU_Speed = i;
            } else Option.CPU_Speed = Freq252P; 
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont = 1 ;
        }     
#ifdef HDMI
        else if(checkstring(argv[0], (unsigned char *)"1280") || checkstring(argv[0], (unsigned char *)"1280x720")){
            Option.CPU_Speed = Freq720P; 
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont=(2<<4) | 1 ;
        }      
        else if(checkstring(argv[0], (unsigned char *)"1024") || checkstring(argv[0], (unsigned char *)"1024x768")){
            Option.CPU_Speed = FreqXGA; 
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont=(2<<4) | 1 ;
        }
#endif      
#ifdef rp2350
        else if(checkstring(argv[0], (unsigned char *)"800") || checkstring(argv[0], (unsigned char *)"800x600")){
            Option.CPU_Speed = FreqSVGA; 
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont= 1 ;
        }

        else if(checkstring(argv[0], (unsigned char *)"848") || checkstring(argv[0], (unsigned char *)"848x480")){
            Option.CPU_Speed = Freq848; 
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont= 1 ;
        }      
#endif
        else if(checkstring(argv[0], (unsigned char *)"720") || checkstring(argv[0], (unsigned char *)"720x400")){
            Option.CPU_Speed = Freq400; 
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont= 1 ;
        }      
        else error("Syntax");
#ifndef HDMI
    Option.X_TILE=(Option.CPU_Speed==Freq848 ? 106 : Option.CPU_Speed==Freq400 ? 90 : Option.CPU_Speed==FreqSVGA? 100: 80);
    Option.Y_TILE=(Option.CPU_Speed==Freq400 ? 33 : Option.CPU_Speed==FreqSVGA? 50: 40);
#endif
    SaveOptions();
    _excep_code = RESET_COMMAND;
    SoftReset();
    return;
}
#ifndef HDMI
    tp = checkstring(cmdline, (unsigned char *)"VGA PIN");
    if(tp) {
        int pin1,testpin;
        getargs(&tp,1,(unsigned char *)",");
   	    if(CurrentLinePtr) error("Invalid in a program");
        char code;
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        pin1 = getinteger(argv[0]);
        if(!code)pin1=codemap(pin1);
        // TODO: separate place
        // PSRAM_PIN_CS 18
        ExtCurrentConfig[24]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[24].GPno+1]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[24].GPno+2]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[24].GPno+3]]=EXT_BOOT_RESERVED;
        // PICO_DEFAULT_LED_PIN GP25
        ExtCurrentConfig[43]=EXT_BOOT_RESERVED;
        // now de-allocate the existing VGA pins temporarily 
        ExtCurrentConfig[Option.VGA_BLUE]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+1]]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+2]]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+3]]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+4]]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+5]]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+6]]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+7]]=EXT_NOT_CONFIG;
        if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)VGArecovery(pin1);
        testpin=PINMAP[PinDef[pin1].GPno+1];
        if(ExtCurrentConfig[testpin] != EXT_NOT_CONFIG)VGArecovery(testpin);
#ifdef rp2350
        uint64_t map=0;
        if(Option.audio_i2s_bclk){
            map|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.audio_i2s_data].GPno);
            map|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.audio_i2s_bclk].GPno);
            map|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.audio_i2s_bclk].GPno+1));
        }
        map|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[pin1].GPno);
        map|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[pin1].GPno+1));
        map|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[pin1].GPno+2));
        map|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[pin1].GPno+3));
        map|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[pin1].GPno+4));
        map|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[pin1].GPno+5));
        map|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[pin1].GPno+6));
        map|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[pin1].GPno+7));
        if((map & (uint64_t)0xFFFF) && (map & (uint64_t)0xFFFF00000000))error("Attempt to define incompatible PIO pins");
#endif
        Option.VGA_BLUE=pin1;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }   
#endif

    tp = checkstring(cmdline, (unsigned char *)"DEFAULT MODE");
    if(tp) {
#ifdef rp2350
        int mode=getint(tp,1,MAXMODES);
        if(mode==3){
            Option.DISPLAY_TYPE=SCREENMODE3; 
            Option.DefaultFont = 1 ;
#ifdef HDMI
        } else if(mode==4){
            if(!(FullColour))error("Mode not available in this resolution");
            Option.DISPLAY_TYPE=SCREENMODE4; 
            Option.DefaultFont=(6<<4) | 1 ;
        } else if(mode==5){
            Option.DISPLAY_TYPE=SCREENMODE5; 
            Option.DefaultFont=(6<<4) | 1 ;
#endif
        } else if(mode==2){
            Option.DISPLAY_TYPE=SCREENMODE2; 
            Option.DefaultFont=(6<<4) | 1 ;
        } else {
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont= 1 ;
        }
#else
        int mode=getint(tp,1,MAXMODES);
        if(mode==2){
            Option.DISPLAY_TYPE=SCREENMODE2; 
            Option.DefaultFont=(6<<4) | 1 ;
        } else {
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont= 1 ;
        }
#endif
        SaveOptions();
        DISPLAY_TYPE= Option.DISPLAY_TYPE;
	    memset((void *)WriteBuf, 0, ScreenSize);
        ResetDisplay();
        CurrentX = CurrentY =0;
        if(Option.DISPLAY_TYPE!=SCREENMODE1)ClearScreen(Option.DefaultBC);
        SetFont(Option.DefaultFont);
        return;
    }
#else
    tp = checkstring(cmdline, (unsigned char *)"CPUSPEED");
    if(tp) {
        uint32_t speed=0;    
   	    if(CurrentLinePtr) error("Invalid in a program");
        speed=getint(tp,MIN_CPU,MAX_CPU);
        uint vco, postdiv1, postdiv2;
        if (!check_sys_clock_khz(speed, &vco, &postdiv1, &postdiv2))error("Invalid clock speed");
        Option.CPU_Speed=speed;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }

    tp = checkstring(cmdline, (unsigned char *)"AUTOREFRESH");
	if(tp) {
	    if((Option.DISPLAY_TYPE==ILI9341 || Option.DISPLAY_TYPE == ILI9163 || Option.DISPLAY_TYPE == ST7735 || Option.DISPLAY_TYPE == ST7789 || Option.DISPLAY_TYPE == ST7789A)) error("Not valid for this display");
		if(checkstring(tp, (unsigned char *)"ON"))		{
			Option.Refresh = 1;
			Display_Refresh();
			return;
		}
		if(checkstring(tp, (unsigned char *)"OFF"))		{ Option.Refresh = 0; return; }
	}
    
    tp = checkstring(cmdline, (unsigned char *)"LCDPANEL");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"DISABLE")) {
    	if(CurrentLinePtr) error("Invalid in a program");
            Option.LCD_CD = Option.LCD_CS = Option.LCD_Reset = Option.DISPLAY_TYPE = Option.SSD_DATA= HRes = VRes = 0;
            Option.SSD_DC = Option.SSD_WR = Option.SSD_RD=SSD1963data=0;
    		Option.TOUCH_XZERO = Option.TOUCH_YZERO = 0;                    // record the touch feature as not calibrated
            Option.SSD_RESET = -1;
            if(Option.DISPLAY_CONSOLE){
                Option.Height = SCREENHEIGHT;
                Option.Width = SCREENWIDTH;
                setterminal(Option.Height,Option.Width);  
            }
            DrawRectangle = (void (*)(int , int , int , int , int ))DisplayNotSet;
            DrawBitmap =  (void (*)(int , int , int , int , int , int , int , unsigned char *))DisplayNotSet;
            ScrollLCD = (void (*)(int ))DisplayNotSet;
            DrawBuffer = (void (*)(int , int , int , int , unsigned char * ))DisplayNotSet;
            ReadBuffer = (void (*)(int , int , int , int , unsigned char * ))DisplayNotSet;
			Option.DISPLAY_CONSOLE = false;
		} else {
            if(Option.DISPLAY_TYPE && !CurrentLinePtr) error("Display already configured");
            ConfigDisplayUser(tp);
            if(Option.DISPLAY_TYPE)return;
    	    if(CurrentLinePtr) error("Invalid in a program");
            if(!Option.DISPLAY_TYPE)ConfigDisplaySPI(tp);
            if(!Option.DISPLAY_TYPE)ConfigDisplayVirtual(tp);
            if(!Option.DISPLAY_TYPE)ConfigDisplaySSD(tp);
            if(!Option.DISPLAY_TYPE)ConfigDisplayI2C(tp);
        }
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"TOUCH");
    if(tp) {
      if(CurrentLinePtr) error("Invalid in a program");
      if(checkstring(tp, (unsigned char *)"DISABLE")) {
            if(Option.CombinedCS)error("Touch CS in use for SDcard");
            Option.TOUCH_Click = Option.TOUCH_CS = Option.TOUCH_IRQ = false;
        } else  {
            if(Option.TOUCH_CS) error("Touch already configured");
            ConfigTouch(tp);
        }
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
  }
#endif
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
#ifdef rp2350
        if(!rp2350a)error("Invalid for RP2350B");
#endif
        if(Option.AllPins)error("OPTION PICO set");
        if(checkstring(tp, (unsigned char *)"PWM"))  Option.PWM = true;
        if(checkstring(tp, (unsigned char *)"PFM"))  Option.PWM = false;
        SaveOptions();
        if(Option.PWM){
            gpio_init(23);
            gpio_put(23,GPIO_PIN_SET);
            gpio_set_dir(23, GPIO_OUT);
        } else {
            gpio_init(23);
            gpio_put(23,GPIO_PIN_RESET);
            gpio_set_dir(23, GPIO_OUT);
    	}
        return;
    }

    tp = checkstring(cmdline, (unsigned char *)"COLOURCODE");
    if(tp == nullptr) tp = checkstring(cmdline, (unsigned char *)"COLORCODE");
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
        CombinedPtr p;
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
                modbuff = (RoundUpK4(TOP_OF_SYSTEM_FLASH));
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
                modbuff = nullptr;
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
        CombinedPtr p;
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
#ifdef rp2350
            if(rp2350a)slice=11;
            else
#endif
            slice=checkslice(pin2,pin2, 1);
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

//
#ifdef rp2350
    #if defined(PICOMITEVGA) && !defined(HMDI)
            int pio=QVGA_PIO_NUM;
    #else
            int pio=2;
    #endif
    uint64_t map=piomap[pio]; 
            map|=(uint64_t)((uint64_t)1<< (uint64_t)PinDef[pin2].GPno);
            map|=((uint64_t)1<< (uint64_t)PinDef[pin1].GPno);
            map|=((uint64_t)1<<(uint64_t)(PinDef[pin1].GPno+1));
            if((map & (uint64_t)0xFFFF) && (map & (uint64_t)0xFFFF00000000))error("Attempt to define incompatible PIO pins");
            if(rp2350a)slice=11;
            else
#endif
            slice=checkslice(pin1,pin1, 1);
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
 #ifdef PICOMITEVGA
        if(Option.RTC_Clock || Option.RTC_Data)error("In use");
#else
        if(Option.DISPLAY_TYPE == SSD1306I2C || Option.DISPLAY_TYPE == SSD1306I2C32 || Option.RTC_Clock || Option.RTC_Data)error("In use");
#endif
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
        if(CallBackEnabled==2) gpio_set_irq_enabled_with_callback(PinDef[Option.INT1pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
        else if(CallBackEnabled & 2){
            gpio_set_irq_enabled(PinDef[Option.INT1pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
            CallBackEnabled &= (~2);
        }
        if(CallBackEnabled==4) gpio_set_irq_enabled_with_callback(PinDef[Option.INT2pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
        else if(CallBackEnabled & 4){
            gpio_set_irq_enabled(PinDef[Option.INT2pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
            CallBackEnabled &= (~4);
        }
        if(CallBackEnabled==8) gpio_set_irq_enabled_with_callback(PinDef[Option.INT3pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
        else  if(CallBackEnabled & 8){
            gpio_set_irq_enabled(PinDef[Option.INT3pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
            CallBackEnabled &= (~8);
        }
        if(CallBackEnabled==16) gpio_set_irq_enabled_with_callback(PinDef[Option.INT4pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
        else  if(CallBackEnabled & 16){
            gpio_set_irq_enabled(PinDef[Option.INT4pin].GPno, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
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
#ifndef PICOMITEVGA
    tp = checkstring(cmdline, (unsigned char *)"SYSTEM SPI");
    if(tp) {
        int pin1,pin2,pin3;
        if(checkstring(tp, (unsigned char *)"DISABLE")){
   	    if(CurrentLinePtr) error("Invalid in a program");
         if((Option.SD_CS && Option.SD_CLK_PIN==0) || Option.TOUCH_CS || Option.LCD_CS || Option.CombinedCS)error("In use");
            disable_systemspi();
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;                                // this will restart the processor ? only works when not in debug
        }
    	getargs(&tp,5,(unsigned char *)",");
   	    if(CurrentLinePtr) error("Invalid in a program");
         if(argc!=5)error("Syntax");
        if(Option.SYSTEM_CLK)error("SYSTEM SPI already configured");
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
		if(!(PinDef[pin1].mode & SPI0SCK && PinDef[pin2].mode & SPI0TX  && PinDef[pin3].mode & SPI0RX  ) &&
        !(PinDef[pin1].mode & SPI1SCK && PinDef[pin2].mode & SPI1TX  && PinDef[pin3].mode & SPI1RX  ))error("Not valid SPI pins");
        if(PinDef[pin1].mode & SPI0SCK && SPI0locked)error("SPI channel already configured for audio");
        if(PinDef[pin1].mode & SPI1SCK && SPI1locked)error("SPI channel already configured for audio");
        Option.SYSTEM_CLK=pin1;
        Option.SYSTEM_MOSI=pin2;
        Option.SYSTEM_MISO=pin3;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
#endif
    // unsupported for this build
    #if 0
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
#ifndef PICOMITEVGA
        if(checkstring(tp, (unsigned char *)"COMBINED CS")){
            if(Option.SD_CS || Option.CombinedCS)error("SDcard already configured");
            if(!Option.SYSTEM_CLK)error("System SPI not configured");
            if(!Option.TOUCH_CS)error("Touch CS pin not configured");
            Option.CombinedCS=1;
            Option.SD_CS=0;
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return;
        }
#endif
    	getargs(&tp,7,(unsigned char *)",");
#ifdef PICOMITEVGA
        if(!(argc==7))error("Syntax");
#else
        if(!(argc==1 || argc==7))error("Syntax");
#endif
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
#ifdef PICOMITEVGA
			if(PinDef[pin1].mode & SPI0SCK && PinDef[pin2].mode & SPI0TX  && PinDef[pin3].mode & SPI0RX && !Option.SYSTEM_CLK){
                Option.SYSTEM_CLK=pin1;
                Option.SYSTEM_MOSI=pin2;
                Option.SYSTEM_MISO=pin3;
                MMPrintString("SPI channel 0 in use for SDcard\r\n");
			} else if(PinDef[pin1].mode & SPI1SCK && PinDef[pin2].mode & SPI1TX  && PinDef[pin3].mode & SPI1RX  && !Option.SYSTEM_CLK){
                Option.SYSTEM_CLK=pin1;
                Option.SYSTEM_MOSI=pin2;
                Option.SYSTEM_MISO=pin3;
                MMPrintString("SPI channel 1 in use for SDcard\r\n");
			} else {
#endif
                Option.SD_CLK_PIN=pin1;
                Option.SD_MOSI_PIN=pin2;
                Option.SD_MISO_PIN=pin3;
#ifdef PICOMITEVGA
            }
#endif
        }
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return;
    }
	#endif
    tp = checkstring(cmdline, (unsigned char *)"DISK SAVE");
    if(tp){
        getargs(&tp,1,(unsigned char *)",");
        if(!(argc==1))error("Syntax");
        if(CurrentLinePtr) error("Invalid in a program");
        int fnbr = FindFreeFileNbr();
        if (!InitSDCard())  return;
        char *pp = (char *)getFstring(argv[0]);
        if (strchr((char *)pp, '.') == nullptr)
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
        if (strchr((char *)pp, '.') == nullptr)
            strcat((char *)pp, ".opt");
        if (!BasicFileOpen((char *)pp, fnbr, FA_READ)) return;
		if(filesource[fnbr]!=FLASHFILE)  fsize = f_size(FileTable[fnbr].fptr);
		else fsize = lfs_file_size(&lfs,FileTable[fnbr].lfsptr);
        if(!(fsize==sizeof(Option) || fsize==sizeof(Option)-128))error("File size incorrect");
        char *s=(char *)&Option.Magic;
        for(int k = 0; k < fsize; k++){        // write to the flash byte by byte
           *s++=FileGetChar(fnbr);
        }
        Option.Magic=MagicKey; //This isn't ideal but it improves the chances of a older config working in a new build
        FileClose(fnbr);
        uSec(100000);
        /** disable_interrupts_pico(); */
        sd_range_erase(FLASH_TARGET_OFFSET, FLASH_ERASE_SIZE);
        /** enable_interrupts_pico(); */
        uSec(10000);
        /** disable_interrupts_pico(); */
        sd_range_program(FLASH_TARGET_OFFSET, (const uint8_t *)&Option, 768);
        /** enable_interrupts_pico(); */
        _excep_code = RESET_COMMAND;
        SoftReset();
    }
	tp = checkstring(cmdline, (unsigned char *)"RESET");
    if(tp) {
   	    if(CurrentLinePtr) error("Invalid in a program");
        if(Option.LIBRARY_FLASH_SIZE==MAX_PROG_SIZE) {
          uint32_t j = FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE + SAVEDVARS_FLASH_SIZE + ((MAXFLASHSLOTS - 1) * MAX_PROG_SIZE);
          uSec(250000);
          /** disable_interrupts_pico(); */
          sd_range_erase(j, MAX_PROG_SIZE);
          /** enable_interrupts_pico(); */
        }
        configure(tp);
        return;
    }
    error("Invalid Option");
}

void fun_device(void){
    uint8_t* s = (uint8_t*)GetTempMemory(STRINGSIZE);                                        // this will last for the life of the command
#ifdef PICOMITEVGA
#ifdef USBKEYBOARD
#ifdef HDMI
    strcpy((char *)s, "PicoMiteHDMIUSB");
#else
    strcpy((char *)s, "PicoMiteVGAUSB");
#endif
#else
#ifdef HDMI
    strcpy((char *)s, "PicoMiteHDMI");
#else
    strcpy((char *)s, "PicoMiteVGA");
#endif
#endif
#endif
#ifdef PICOMITE
#ifdef USBKEYBOARD
    strcpy((char *)s, "PicoMiteUSB");
#else
    strcpy((char *)s, "PicoMite");
#endif
#endif
#ifdef PICOMITEWEB
    strcpy((char *)s, "WebMite");
#endif
#ifdef rp2350
    if(rp2350a)strcat((char *)s," RP2350A");
    else strcat((char *)s," RP2350B");
#endif
    CtoM(s);
    targ = T_STR;
    sret = s;
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
    CombinedPtr tp;
    sret = (uint8_t*)GetTempMemory(STRINGSIZE);                                  // this will last for the life of the command
    if(checkstring(ep, (unsigned char *)"AUTORUN")){
        if(Option.Autorun == false)strcpy((char *)sret.raw(),"Off");
        else strcpy((char *)sret.raw(),"On");
        CtoM(sret.raw());
        targ=T_STR;
        return;
#ifdef PICOMITEVGA
    } else if((tp=checkstring(ep, (unsigned char *)"TILE HEIGHT"))){
        iret=ytileheight;
        targ=T_INT;
        return;
#endif
    } else if((tp=checkstring(ep, (unsigned char *)"ADC DMA"))){
        targ=T_INT;
        iret=ADCDualBuffering | dmarunning;
        return;
    } else if((tp=checkstring(ep, (unsigned char *)"ADC"))){
        targ=T_INT;
        iret=((adcint==adcint1 && adcint) ? 1 : ((adcint==adcint2 && adcint) ? 2 : 0));
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
        FSerror=lfs_file_read(&lfs, FileTable[fnbr].lfsptr, &boot_count, sizeof(boot_count));	
        if(FSerror>0)FSerror=0;
        ErrorCheck(fnbr);
        FileClose(fnbr);
        FatFSFileSystem=FatFSFileSystemSave;
        iret=boot_count;
        targ=T_INT;
    } else if(checkstring(ep, (unsigned char *)"BOOT")){
        if(restart_reason==     0xFFFFFFFF)strcpy((char *)sret.raw(), "Restart");
        else if(restart_reason==0xFFFFFFFE)strcpy((char *)sret.raw(), "S/W Watchdog");
        else if(restart_reason==0xFFFFFFFD)strcpy((char *)sret.raw(), "H/W Watchdog");
        else if(restart_reason==0xFFFFFFFC)strcpy((char *)sret.raw(), "Firmware update");
#ifdef rp2350
        else if(restart_reason & 0x30000)strcpy((char *)sret.raw(), "Power On");
        else if(restart_reason & 0x40000)strcpy((char *)sret.raw(), "Reset Switch");
        else if(restart_reason & 0x280000)strcpy((char *)sret.raw(), "Debug");
#else
        else if(restart_reason==0x100)strcpy((char *)sret.raw(), "Power On");
        else if(restart_reason==0x10000)strcpy((char *)sret.raw(), "Reset Switch");
        else if(restart_reason==0x100000)strcpy((char *)sret.raw(), "Debug");
#endif
        else sprintf((char *)sret.raw(), "Unknown code %X",(unsigned int)restart_reason);
        CtoM(sret.raw());
        targ=T_STR;
        return;
    } else if(*ep=='c' || *ep=='C'){
        if(checkstring(ep, (unsigned char *)"CALLTABLE")){
            iret = (int64_t)(uint32_t)CallTable;
            targ = T_INT;
            return;
        } else if(checkstring(ep, (unsigned char *)"CPUSPEED")){
            IntToStr((char *)sret.raw(),Option.CPU_Speed*1000,10);
            CtoM(sret.raw());
            targ=T_STR;
            return;
        } else if(checkstring(ep, (unsigned char *)"CURRENT")){
            if(ProgMemory[0]==1 && ProgMemory[1]==39 && ProgMemory[2]==35){
                strcpy((char *)sret.raw(), ProgMemory[3]);
            } else strcpy((char *)sret.raw(),"NONE");
            CtoM(sret.raw());
            targ=T_STR;
            return;
        } else error("Syntax");
    }  else if(*ep=='d' || *ep=='D'){
        if(checkstring(ep, (unsigned char *)"DEVICE")){
            fun_device();
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"DRIVE"))){
            strcpy((char *)sret.raw(),FatFSFileSystem ? "B:":"A:");
            CtoM(sret.raw());
            targ=T_STR;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"DEBUG"))){
            iret=time_us_64()-mSecTimer*1000;
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
            strcpy((char *)sret.raw(), MMErrMsg);
            CtoM(sret.raw());
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
            iret=(int64_t)(unsigned int)(sd_target_contents + (getint(tp,1,MAXFLASHSLOTS) - 1) * MAX_PROG_SIZE);
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
        strcpy((char *)sret.raw(),id_out);
        CtoM(sret.raw());
        targ=T_STR;
        return;
#ifdef PICOMITEWEB
    } else if((tp=checkstring(ep, (unsigned char *)"TCP REQUEST"))){
        int i=getint(tp,1,MaxPcb)-1;
        iret=TCPstate->inttrig[i];
        targ=T_INT;
        return;
    } else if((tp=checkstring(ep, (unsigned char *)"TCP PORT"))){
        iret=Option.TCP_PORT;
        targ=T_INT;
        return;
    } else if((tp=checkstring(ep, (unsigned char *)"UDP PORT"))){
        iret=Option.UDP_PORT;
        targ=T_INT;
        return;
     } else if(checkstring(ep,(unsigned char *)"IP ADDRESS")){  
        strcpy((char *)sret,ip4addr_ntoa(netif_ip4_addr(netif_list)));
        CtoM(sret);
        targ=T_STR;
        return;
    } else if(checkstring(ep,(unsigned char *)"MAX CONNECTIONS")){  
        iret=MaxPcb;
        targ=T_INT;
        return;
    } else if(checkstring(ep,(unsigned char *)"WIFI STATUS")){  
        iret=cyw43_wifi_link_status(&cyw43_state,CYW43_ITF_STA);
        targ=T_INT;
        return;
    } else if(checkstring(ep,(unsigned char *)"TCPIP STATUS")){  
        iret=cyw43_tcpip_link_status(&cyw43_state,CYW43_ITF_STA);
        targ=T_INT;
        return;
#endif
    } 
#ifndef rp2350
    else if(checkstring(ep, (unsigned char *)"INTERRUPTS")){
    iret=(int64_t)(uint32_t)*((io_rw_32 *) (PPB_BASE + M0PLUS_NVIC_ISER_OFFSET));
    targ=T_INT;
    return;
    }
#endif
#ifndef PICOMITEVGA
    else if(checkstring(ep, (unsigned char *)"LCDPANEL")){
        strcpy((char *)sret,display_details[Option.DISPLAY_TYPE].name);
        CtoM(sret);
        targ=T_STR;
        return;
    } 
    else if(checkstring(ep, (unsigned char *)"LCD320")){
        iret=(SSD16TYPE || Option.DISPLAY_TYPE==IPS_4_16);
        targ=T_INT;
        return;
    } 
#endif
#ifdef USBKEYBOARD
    else if((tp=checkstring(ep, (unsigned char *)"USB VID"))){
        int n=getint((unsigned char *)tp,1,4);
        iret=HID[n-1].vid;
        targ=T_INT;
        return;
    }
    else if((tp=checkstring(ep, (unsigned char *)"USB PID"))){
        int n=getint((unsigned char *)tp,1,4);
        iret=HID[n-1].pid;
        targ=T_INT;
        return;
    }
    else if((tp=checkstring(ep, (unsigned char *)"USB"))){
        int n=getint((unsigned char *)tp,0,4);
        if(n==0)iret=Current_USB_devices;
        else iret=HID[n-1].Device_type;
        targ=T_INT;
        return;
    }
#endif
    else if (checkstring(ep, (unsigned char *)"LINE")) {
        if (!CurrentLinePtr) {
            strcpy((char *)sret.raw(), "UNKNOWN");
        } else if (CurrentLinePtr >= ProgMemory + MAX_PROG_SIZE) {
            strcpy((char *)sret.raw(), "LIBRARY");
        } else {
            sprintf((char *)sret.raw(), "%d", CountLines(CurrentLinePtr));
        }
        CtoM(sret.raw());
        targ=T_STR;
        return;
    }	
    else if((tp=checkstring(ep,(unsigned char *)"MODBUFF ADDRESS"))){
        iret=(int64_t)((uint32_t)(char *)(RoundUpK4(TOP_OF_SYSTEM_FLASH)));
        targ=T_INT;
        return;
    }
    else if((tp=checkstring(ep, (unsigned char *)"MODIFIED"))){
//		int i,j;
	    DIR djd;
	    FILINFO fnod;
        sret = (uint8_t*)GetTempMemory(STRINGSIZE);                                    // this will last for the life of the command
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
	    IntToStr((char *)sret.raw() , ((fnod.fdate>>9)&0x7F)+1980, 10);
	    sret.raw()[4] = '-'; IntToStrPad((char *)sret.raw() + 5, (fnod.fdate>>5)&0xF, '0', 2, 10);
	    sret.raw()[7] = '-'; IntToStrPad((char *)sret.raw() + 8, fnod.fdate&0x1F, '0', 2, 10);
	    sret.raw()[10] = ' ';
	    IntToStrPad((char *)sret.raw()+11, (fnod.ftime>>11)&0x1F, '0', 2, 10);
	    sret.raw()[13] = ':'; IntToStrPad((char *)sret.raw() + 14, (fnod.ftime>>5)&0x3F, '0', 2, 10);
	    sret.raw()[16] = ':'; IntToStrPad((char *)sret.raw() + 17, (fnod.ftime&0x1F)*2, '0', 2, 10);
        FatFSFileSystem=FatFSFileSystemSave;
		CtoM(sret.raw());
	    targ=T_STR;
		return;
	} else if((tp=checkstring(ep, (unsigned char *)"ONEWIRE"))){
        fun_mmOW();
        return;
	} else if((tp=checkstring(ep, (unsigned char *)"OPTION"))){
        if(checkstring(tp, (unsigned char *)"AUTORUN")){
			if(Option.Autorun == false)strcpy((char *)sret.raw(),"Off");
            else if(Option.Autorun==MAXFLASHSLOTS+1)strcpy((char *)sret.raw(),"On");
			else {
                char b[10];
                IntToStr(b,Option.Autorun,10);
                strcpy((char *)sret.raw(),b);
            }
            CtoM(sret.raw());
            targ=T_STR;
            return;
		} else if(checkstring(tp, (unsigned char *)"BASE")){
			if(g_OptionBase==1)iret=1;
			else iret=0;
			targ=T_INT;
			return;
		} else if(checkstring(tp, (unsigned char *)"AUDIO")){
            if(Option.AUDIO_L)strcpy((char *)sret.raw(),"PWM");
            else if(Option.AUDIO_MISO_PIN)strcpy((char *)sret.raw(),"VS1053");
            else if(Option.AUDIO_CLK_PIN)strcpy((char *)sret.raw(),"SPI");
            else strcpy((char *)sret.raw(),"NONE");
            CtoM(sret.raw());
            targ=T_STR;
			return;
		} else if(checkstring(tp, (unsigned char *)"BREAK")){
			iret=BreakKey;
			targ=T_INT;
			return;
		} else if(checkstring(tp, (unsigned char *)"ANGLE")){
			if(optionangle==1.0)strcpy((char *)sret.raw(),"RADIANS");
			else strcpy((char *)sret.raw(),"DEGREES");
            CtoM(sret.raw());
            targ=T_STR;
            return;
 		} else if(checkstring(tp, (unsigned char *)"DEFAULT")){
			if(DefaultType == T_INT)strcpy((char *)sret.raw(),"Integer");
			else if(DefaultType == T_NBR)strcpy((char *)sret.raw(),"Float");
			else if(DefaultType == T_STR)strcpy((char *)sret.raw(),"String");
			else strcpy((char *)sret.raw(),"None");
            CtoM(sret.raw());
            targ=T_STR;
            return;
 		} else if(checkstring(tp, (unsigned char *)"KEYBOARD")){
#ifdef USBKEYBOARD
            strcpy((char *)sret.raw(),(char *)KBrdList[(int)Option.USBKeyboard]);
#else
            strcpy((char *)sret.raw(),(char *)KBrdList[(int)Option.KeyboardConfig]);
#endif
            CtoM(sret.raw());
            targ=T_STR;
            return;
 		} else if(checkstring(tp, (unsigned char *)"EXPLICIT")){
			if(OptionExplicit == false)strcpy((char *)sret.raw(),"Off");
			else strcpy((char *)sret.raw(),"On");
            CtoM(sret.raw());
            targ=T_STR;
            return;
		} else if(checkstring(tp, (unsigned char *)"FLASH SIZE")){
            uint8_t txbuf[4] = {0x9f};
            uint8_t rxbuf[4] = {0};
            /** disable_interrupts_pico(); */
            flash_do_cmd(txbuf, rxbuf, 4);
            /** enable_interrupts_pico(); */
            iret= 1 << rxbuf[3];
			targ=T_INT;
			return;
        } else if(checkstring(tp, (unsigned char *)"HEIGHT")){
            iret = Option.Height;
            targ = T_INT;
            return;
        } else if(checkstring(tp, (unsigned char *)"CONSOLE")){
			if(Option.DISPLAY_CONSOLE)strcpy((char *)sret.raw(),"Both");
			else strcpy((char *)sret.raw(),"Serial");
            CtoM(sret.raw());
            targ=T_STR;
            return;
        } else if(checkstring(tp, (unsigned char *)"WIDTH")){
            iret = Option.Width;
            targ = T_INT;
            return;
#ifdef PICOMITEWEB
		} else if(checkstring(tp, (unsigned char *)"SSID")){
			strcpy((char *)sret,(char *)Option.SSID);
            CtoM(sret);
            targ=T_STR;
            return;
#endif
		} else error("Syntax");
    } else if(*ep=='p' || *ep=='P'){
        if((tp=checkstring(ep, (unsigned char *)"PINNO"))){
            int pin;
            MMFLOAT f;
            long long int i64;
            CombinedPtr ss;
            int t=0;
            char code, *ptr;
            char *string=(char*)GetTempMemory(STRINGSIZE);
            skipspace(tp);
        #ifdef PICOMITEWEB
            iret=0;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='2' && tp[3]=='3')iret=41;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='2' && tp[3]=='4')iret=42;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='2' && tp[3]=='5')iret=43;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='2' && tp[3]=='9')iret=44;
            if(iret){
                targ=T_INT;
                return;
            }
        #endif
        #ifdef HDMI
            iret=0;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='1' && tp[3]=='2')iret=16;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='1' && tp[3]=='3')iret=17;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='1' && tp[3]=='4')iret=19;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='1' && tp[3]=='5')iret=20;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='1' && tp[3]=='6')iret=21;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='1' && tp[3]=='7')iret=22;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='1' && tp[3]=='8')iret=24;
	        if((tp[0]=='G' || tp[0]=='g') && (tp[1]=='P' || tp[1]=='p') && tp[2]=='1' && tp[3]=='9')iret=25;
             if(iret){
                targ=T_INT;
                return;
            }
        #endif
            if(codecheck(tp))evaluate(tp, &f, &i64, &ss, &t, false);
            if(t & T_STR ){
                ptr=(char *)getCstring(tp);
                strcpy(string,ptr);
            } else {
                strcpy(string,tp);
            }
        #ifdef PICOMITEWEB
            iret=0;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='2' && string[3]=='3')iret=41;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='2' && string[3]=='4')iret=42;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='2' && string[3]=='5')iret=43;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='2' && string[3]=='9')iret=44;
            if(iret){
                targ=T_INT;
                return;
            }
        #endif
        #ifdef HDMI
            iret=0;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='1' && string[3]=='2')iret=16;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='1' && string[3]=='3')iret=17;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='1' && string[3]=='4')iret=19;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='1' && string[3]=='5')iret=20;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='1' && string[3]=='6')iret=21;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='1' && string[3]=='7')iret=22;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='1' && string[3]=='8')iret=24;
	        if((string[0]=='G' || string[0]=='g') && (string[1]=='P' || string[1]=='p') && string[2]=='1' && string[3]=='9')iret=25;
             if(iret){
                targ=T_INT;
                return;
            }
        #endif
            if(!(code=codecheck( (unsigned char *)string)))string+=2;  
            else error("Syntax");
            pin = getinteger((unsigned char *)string);
            if(!code)pin=codemap(pin);
        #ifdef PICOMITEWEB
            if(pin>=41 && pin<=44){
                iret=pin;
                targ=T_INT;
                return;
            }
        #endif
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
#ifdef rp2350
            int channel=getint(tp,0,rp2350a? 7 : 11);
#else
            int channel=getint(tp,0,7);
#endif
            iret=pwm_hw->slice[channel].top;
            targ=T_INT;
            return;
        } else if((tp=checkstring(ep, (unsigned char *)"PWM DUTY"))){
            getargs(&tp,3,(unsigned char *)",");
            if(argc!=3)error("Syntax");
#ifdef rp2350
            int channel=getint(argv[0],0,rp2350a? 7 : 11);
#else
            int channel=getint(argv[0],0,7);
#endif
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
        #ifdef HDMI
            if(pin>=16 && pin<=25){
                strcpy((char *)sret,"Boot Reserved : HDMI");
                CtoM(sret);
                targ=T_STR;
                return;
            }
        #endif
        #ifdef PICOMITEWEB
            if(pin>=41 && pin<=44){
                strcpy((char *)sret,"Boot Reserved : CYW43");
                CtoM(sret);
                targ=T_STR;
                return;
            }
        #endif
            if(IsInvalidPin(pin))strcpy((char *)sret.raw(),"Invalid");
            else strcpy((char *)sret.raw(),PinFunction[ExtCurrentConfig[pin] & 0xFF]);
            if(ExtCurrentConfig[pin] & EXT_BOOT_RESERVED){
                strcpy((char *)sret.raw(), "Boot Reserved : ");
                strcat((char *)sret.raw(),pinsearch(pin));
            }
            if(ExtCurrentConfig[pin] & EXT_COM_RESERVED)strcat((char *)sret.raw(), ": Reserved for function");
            if(ExtCurrentConfig[pin] & EXT_DS18B20_RESERVED)strcat((char *)sret.raw(), ": In use for DS18B20");
            CtoM(sret.raw());
            targ=T_STR;
            return;
        } else if(checkstring(ep, (unsigned char *)"PROGRAM")){
            iret = (int64_t)(uint32_t)ProgMemory;
            targ = T_INT;
            return;
#ifndef USBKEYBOARD
        } else if(checkstring(ep, (unsigned char *)"PS2")){
            iret = (int64_t)(uint32_t)PS2code;
            targ = T_INT;
            return;
#endif            
        } else if(checkstring(ep, (unsigned char *)"PATH")){
//            strcpy((char *)sret,GetCWD());
//            if(sret[strlen((char *)sret)-1]!='/')strcat((char *)sret,"/");
            if(ProgMemory[0]==1 && ProgMemory[1]==39 && ProgMemory[2]==35){
                strcpy((char *)sret.raw(),ProgMemory+3);
                for(int i=strlen((char *)sret.raw())-1;i>0;i--){
                    if(sret[i]!='/')sret.raw()[i]=0;
                    else break;
                }
            } else strcpy((char *)sret.raw(),"NONE");
            CtoM(sret.raw());
            targ=T_STR;
            return;
        } else if(checkstring(ep, (unsigned char *)"PLATFORM")){
			strcpy((char *)sret.raw(),(char *)Option.platform);
            CtoM(sret.raw());
            targ=T_STR;
            return;
        } else error("Syntax");
    } else if(*ep=='s' || *ep=='S'){
        if(checkstring(ep, (unsigned char *)"SDCARD")){
            int i=OptionFileErrorAbort;
            OptionFileErrorAbort=0;
            FatFSFileSystemSave = FatFSFileSystem;
            FatFSFileSystem=1;
            if(!(Option.SD_CS || Option.CombinedCS))strcpy((char *)sret.raw(),"Not Configured");
            else if(!InitSDCard())strcpy((char *)sret.raw(),"Not present");
            else  strcpy((char *)sret.raw(),"Ready");
            CtoM(sret.raw());
            targ=T_STR;
            OptionFileErrorAbort=i;
            FatFSFileSystem = FatFSFileSystemSave;
            return;
        } else if(checkstring(ep, (unsigned char *)"SYSTEM I2C")){
            if(!Option.SYSTEM_I2C_SDA)strcpy((char *)sret.raw(),"Not set");
            else  if(I2C0locked) strcpy((char *)sret.raw(),"I2C");
            else strcpy((char *)sret.raw(),"I2C2");
            CtoM(sret.raw());
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
            strcpy((char *)sret.raw(),PlayingStr[CurrentlyPlaying]);
            CtoM(sret.raw());
            targ=T_STR;
            return;
        } else error("Syntax");
    }
#ifndef PICOMITEVGA
    else if(checkstring(ep, (unsigned char *)"TOUCH")){
        if(Option.TOUCH_CS == false)strcpy((char *)sret,"Disabled");
        else if(Option.TOUCH_XZERO == TOUCH_NOT_CALIBRATED)strcpy((char *)sret,"Not calibrated");
        else strcpy((char *)sret.raw(),"Ready");
        CtoM(sret.raw());
        targ=T_STR;
        return;
    } 
#endif
	else if(checkstring(ep, (unsigned char *)"TRACK")){
		if(CurrentlyPlaying == P_MP3 || CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_WAV|| CurrentlyPlaying == P_MOD || CurrentlyPlaying == P_MIDI)
            strcpy((char *)sret.raw(),WAVfilename);
		else strcpy((char *)sret.raw(),"OFF");
        CtoM(sret.raw());
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
        fret = (MMFLOAT)time_us_64()/1000000.0;
        targ = T_NBR;
        return;
    } else if((tp=checkstring(ep, (unsigned char *)"MAX GP"))){
#ifdef rp2350
        iret=(rp2350a ? 29 : 47);
#else
        iret=29;
#endif
        targ = T_INT;
        return;
    } else error("Syntax");
}


void cmd_watchdog(void) {
    int i;
    CombinedPtr p;

    if((p=checkstring(cmdline, (unsigned char *)"HW"))){
        if(checkstring(p, (unsigned char *)"OFF") != nullptr) {
            hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
            _excep_code=0;
        } else {
            i = getint(p,1,8331);
            watchdog_enable(i,1);
            _excep_code=POSSIBLE_WATCHDOG;
        }
    
    } else if(checkstring(cmdline, (unsigned char *)"OFF") != nullptr) {
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
    CombinedPtr p;
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
            sleep_us(totalseconds*1000000);

    } else error("Syntax");
}
void cmd_csubinterrupt(void){
    getargs(&cmdline,1,(unsigned char *)",");
    if(argc != 0){
        if(checkstring(argv[0],(unsigned char *)"0")){
            CSubInterrupt = nullptr;
            CSubComplete=0;  
        } else {
            CSubInterrupt = GetIntAddress(argv[0]); 
            CSubComplete=0;  
            InterruptUsed = true;
        }
    } else CSubComplete=1;  
}
void cmd_cfunction(void) {
    CombinedPtr p;
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
            nextstmt = p;
            skipelement(nextstmt);
            return;
        }
        p++;
    }
}

// utility function used by cmd_poke() to validate an address
unsigned int GetPokeAddr(CombinedPtr p) {
    unsigned int i;
    i = getinteger(p);
//    if(!POKERANGE(i)) error("Address");
    return i;
}


void cmd_poke(void) {
    CombinedPtr p, q;
    void *pp;
    if((p = checkstring(cmdline, (unsigned char *)"DISPLAY"))){
        if(!Option.DISPLAY_TYPE)error("Display not configured");
        if((q=checkstring(p,(unsigned char *)"HRES"))){ 
            HRes=getint(q,0,1920);
            return;
        } else if((q=checkstring(p,(unsigned char *)"VRES"))){
            VRes=getint(q,0,1200);
            return;
#ifndef PICOMITEVGA
        } else {
            getargs(&p,(MAX_ARG_COUNT * 2) - 3,(unsigned char *)",");
            if(!argc)return;
            if(Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL){
                WriteComand(getinteger(argv[0]));
                for(int i = 2; i < argc; i += 2) {
                    WriteData(getinteger(argv[i]));
                }
                return;
            } else if(Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<ST7920){
                spi_write_command(getinteger(argv[0]));
                for(int i = 2; i < argc; i += 2) {
                    spi_write_data(getinteger(argv[i]));
                }
                return;
            } else if(Option.DISPLAY_TYPE<=I2C_PANEL){
                if(argc>1)error("UNsupported command");
                I2C_Send_Command(getinteger(argv[0]));
                return;
            } else 
            error("Display not supported");
#endif
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
static unsigned int GetCFunAddr(CombinedPtrI ip, int i, CombinedPtr offset) {
    while(*ip != 0xffffffff) {
        //if(*ip++ == (unsigned int)(subfun[i]-ProgMemory)) {                      // if we have a match
        if(*ip++ == (unsigned int)(subfun[i]-offset)) {                      // if we have a match
            ip++;                                                   // step over the size word
            i = *ip++;                                              // get the offset
            return (unsigned int)(ip + i).raw(203);                          // return the entry point
        }
        ip += (*ip + 4) / sizeof(unsigned int);
    }
    return 0;
}


// utility function used by fun_peek() to validate an address
extern "C" unsigned int __not_in_flash_func(GetPeekAddr)(CombinedPtr p) {
    unsigned int i;
    i = getinteger(p);
//    if(!PEEKRANGE(i)) error("Address");
    return i;
}

#define SPIsend(a) {uint8_t b=a;xmit_byte_multi(&b,1);}
#define SPIsend2(a) {SPIsend(0);SPIsend(a);}

// Will return a byte within the PIC32 virtual memory space.
void fun_peek(void) {
    CombinedPtr p;
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
        j = GetCFunAddr(CFunctionFlash, i, ProgMemory);
        if(!j) j = GetCFunAddr(CFunctionLibrary, i, LibMemory);         //Check the library
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
        iret = *(ProgMemory + (int)getinteger(argv[2]));
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
void fun_format(void) {
	unsigned char *p, *fmt;
	int inspec;
	getargs(&ep, 3, (unsigned char *)",");
	if(argc%2 == 0) error("Invalid syntax");
	if(argc == 3)
		fmt = getCstring(argv[2]);
	else
		fmt = (unsigned char *)"%g";

	// check the format string for errors that might crash the CPU
	for(inspec = 0, p = fmt; *p; p++) {
		if(*p == '%') {
			inspec++;
			if(inspec > 1) error("Only one format specifier (%) allowed");
			continue;
		}

		if(inspec == 1 && (*p == 'g' || *p == 'G' || *p == 'f' || *p == 'e' || *p == 'E'|| *p == 'l'))
			inspec++;


		if(inspec == 1 && !(IsDigitinline(*p) || *p == '+' || *p == '-' || *p == '.' || *p == ' '))
			error("Illegal character in format specification");
	}
	if(inspec != 2) error("Format specification not found");
	sret = (uint8_t*)GetTempMemory(STRINGSIZE);									// this will last for the life of the command
	sprintf((char *)sret.raw(), (char *)fmt, getnumber(argv[0]));
	CtoM(sret.raw());
	targ=T_STR;
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
    CombinedPtr intaddr;
    static char rti[2];
    for(int i=1;i<=MAXPID;i++){
        if(PIDchannels[i].interrupt!= nullptr && time_us_64()>PIDchannels[i].timenext && PIDchannels[i].active){
            PIDchannels[i].timenext=time_us_64()+(PIDchannels[i].PIDparams->T * 1000000);
            intaddr = PIDchannels[i].interrupt;
            goto GotAnInterrupt;
        }
    }

    // check for an  ON KEY loc  interrupt
    if(KeyInterrupt != nullptr && Keycomplete) {
		Keycomplete=false;
		intaddr = KeyInterrupt;									    // set the next stmt to the interrupt location
		goto GotAnInterrupt;
	}

    if(OnKeyGOSUB && kbhitConsole()) {
        intaddr = OnKeyGOSUB;                                       // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
#ifndef USBKEYBOARD
    if(OnPS2GOSUB && PS2int) {
        intaddr = OnPS2GOSUB;                                       // set the next stmt to the interrupt location
        PS2int=false;
        goto GotAnInterrupt;
    }
#endif    
    if(piointerrupt){  // have any PIO interrupts been set
        for(int pio=0 ;pio<PIOMAX;pio++){
#ifdef rp2350
            PIO pioinuse = (pio==0 ? pio0: (pio==1 ? pio1: pio2));
#else
            PIO pioinuse = (pio==0 ? pio1: pio0);
#endif
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
            intaddr = DMAinterruptRX;
            DMAinterruptRX=nullptr;
            pio_sm_set_enabled(pio, dma_rx_sm, false);
            goto GotAnInterrupt;
        }
    }
    if(DMAinterruptTX){
        if(!dma_channel_is_busy(dma_tx_chan)){
            PIO pio = (dma_tx_pio ? pio1: pio0);
            if((pio->flevel>>(dma_tx_sm*8) & 0xf)==0){
                intaddr = DMAinterruptTX;
                DMAinterruptTX=nullptr;
                pio_sm_set_enabled(pio, dma_tx_sm, false);
                goto GotAnInterrupt;
            }
        }
    }

#ifdef GUICONTROLS
    if(Ctrl!= nullptr){
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

    if (COLLISIONInterrupt != nullptr && CollisionFound) {
        CollisionFound = false;
        intaddr = COLLISIONInterrupt;									    // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
#ifdef PICOMITEWEB
    if(TCPreceived && TCPreceiveInterrupt){
        intaddr = (char *)TCPreceiveInterrupt;                                   // get a pointer to the interrupt routine
        TCPreceived=0;
        goto GotAnInterrupt;
    }
    if(MQTTComplete && MQTTInterrupt != nullptr) {
        MQTTComplete = false;
        intaddr = (char *)MQTTInterrupt;                                      // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    if(UDPreceive && UDPinterrupt != nullptr) {
        UDPreceive = false;
        intaddr = (char *)UDPinterrupt;                                     // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
#endif
    for(int i=0;i<6;i++){
        if(nunInterruptc[i] != nullptr && nunfoundc[i]){
            nunfoundc[i]=false;
            intaddr = nunInterruptc[i];
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
        adc_init();
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
    if(WAVInterrupt != nullptr && WAVcomplete) {
        WAVcomplete=false;
		intaddr = WAVInterrupt;									    // set the next stmt to the interrupt location
		goto GotAnInterrupt;
	}

    // interrupt routines for the serial ports
    if(com1_interrupt != nullptr && SerialRxStatus(1) >= com1_ilevel) {// do we need to interrupt?
        intaddr = com1_interrupt;                                   // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    if(com2_interrupt != nullptr && SerialRxStatus(2) >= com2_ilevel) {// do we need to interrupt?
        intaddr = com2_interrupt;                                   // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }
    if(IrGotMsg && IrInterrupt.raw() != nullptr) {
        IrGotMsg = false;
        intaddr = IrInterrupt;                                      // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }

    if(KeypadInterrupt != nullptr && KeypadCheck()) {
        intaddr = CombinedPtr(KeypadInterrupt);                                  // set the next stmt to the interrupt location
        goto GotAnInterrupt;
    }

    if(CSubInterrupt != nullptr && CSubComplete) {
        intaddr = CSubInterrupt;                                  // set the next stmt to the interrupt location
        CSubComplete=0;
        goto GotAnInterrupt;
    }

    for(i = 0; i < NBRINTERRUPTS; i++) {                            // scan through the interrupt table
        if(inttbl[i].pin != 0) {                                    // if this entry has an interrupt pin set
            v = ExtInp(inttbl[i].pin);                              // get the current value of the pin
            // check if interrupt occured
            if((inttbl[i].lohi == T_HILO && v < inttbl[i].last) || (inttbl[i].lohi == T_LOHI && v > inttbl[i].last) || (inttbl[i].lohi == T_BOTH && v != inttbl[i].last)) {
                intaddr = CombinedPtr(inttbl[i].intp);                           // set the next stmt to the interrupt location
                inttbl[i].last = v;                                 // save the new pin value
                goto GotAnInterrupt;
            } else
                inttbl[i].last = v;                                 // no interrupt, just update the pin value
        }
    }

    // check if one of the tick interrupts is enabled and if it has occured
    for(i = 0; i < NBRSETTICKS; i++) {
        if(TickInt[i] != nullptr && TickTimer[i] > TickPeriod[i]) {
            // reset for the next tick but skip any ticks completely missed
            while(TickTimer[i] > TickPeriod[i]) TickTimer[i] -= TickPeriod[i];
            intaddr = TickInt[i];
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
    CommandToken tkn=commandtbl_decode(intaddr);
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
    nextstmt = intaddr;                                             // the next command will be in the interrupt routine
    return 1;
}
int __not_in_flash_func(check_interrupt)(void) {
#ifdef GUICONTROLS
    if(Ctrl!= nullptr){
        if(!(DelayedDrawKeyboard || DelayedDrawFmtBox || calibrate))ProcessTouch();
        if(CheckGuiFlag) CheckGui();                                    // This implements a LED flash
    }
#endif
#ifndef USBKEYBOARD
    if(Option.KeyboardConfig)CheckKeyboard();
#endif    
    if(!InterruptUsed) return 0;                                    // quick exit if there are no interrupts set
    if(InterruptReturn != nullptr || CurrentLinePtr == nullptr) return 0; // skip if we are in an interrupt or in immediate mode
    return checkdetailinterrupts();
}


// get the address for a MMBasic interrupt
// this will handle a line number, a label or a subroutine
// all areas of MMBasic that can generate an interrupt use this function
extern "C" CombinedPtr GetIntAddress(CombinedPtr p) {
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

}
