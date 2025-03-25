/***********************************************************************************************************************
PicoMite MMBasic

Memory.c

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
* @file Memory.c
* @author Geoff Graham, Peter Mather
* @brief Source for the MMBasic Memory command
*/
/**
 * @cond
 * The following section will be excluded from the documentation.
 */



#define INCLUDE_FUNCTION_DEFINES

#include <stdio.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#define ASMMAX 6400 // maximum number of bytes that can be copied or set by assembler routines
#define MAXCPY 3200 // tuned maximum number of bytes to copy using ZCOPY

extern const uint8_t *SavedVarsFlash;
extern const uint8_t *flash_progmemory;
// memory management parameters

// allocate static memory for programs, variables and the heap
// this is simple memory management because DOS has plenty of memory
//unsigned char __attribute__ ((aligned (256))) AllMemory[ALL_MEMORY_SIZE];
#ifdef rp2350
    #ifdef PICOMITEVGA
        unsigned char __attribute__ ((aligned (256))) AllMemory[HEAP_MEMORY_SIZE+256 +320*240*2];
        unsigned char *FRAMEBUFFER=AllMemory+HEAP_MEMORY_SIZE+256;
        uint32_t framebuffersize=320*240*2;
        #else
        unsigned char __attribute__ ((aligned (256))) AllMemory[HEAP_MEMORY_SIZE+256];
    #endif
    unsigned char *MMHeap=AllMemory;
    #else
    #ifdef PICOMITEVGA
        unsigned char __attribute__ ((aligned (4096))) Heap[HEAP_MEMORY_SIZE+256];
        unsigned char __attribute__ ((aligned (256))) Frame[640*480/8];
        unsigned char *FRAMEBUFFER=Frame;
        uint32_t framebuffersize=640*480/8;
        unsigned char *MMHeap=Heap;
        #else
        unsigned char __attribute__ ((aligned (256))) AllMemory[HEAP_MEMORY_SIZE+256];
        unsigned char *MMHeap=AllMemory;
    #endif
#endif

uint32_t heap_memory_size=HEAP_MEMORY_SIZE;
#ifdef PICOMITEVGA
#ifdef rp2350
uint16_t *tilefcols;//=(uint16_t *)((uint32_t)FRAMEBUFFER+(MODE1SIZE_S*3));
uint16_t *tilebcols;//=(uint16_t *)((uint32_t)FRAMEBUFFER+(MODE1SIZE_S*3)+(MODE1SIZE_S>>1));
#else
uint16_t __attribute__ ((aligned (256))) tilefcols[80*40];
uint16_t __attribute__ ((aligned (256))) tilebcols[80*40];
#endif
#ifdef HDMI
uint8_t *tilefcols_w; 
uint8_t *tilebcols_w;
uint16_t HDMIlines[2][848]={0};
volatile int X_TILE=80, Y_TILE=40;
uint32_t core1stack[128];
volatile int ytileheight=480/12;
#else
uint16_t M_Foreground[16] ={
0x0000,0x000F,0x00f0,0x00ff,0x0f00,0x0f0F,0x0ff0,0x0fff,0xf000,0xf00F,0xf0f0,0xf0ff,0xff00,0xff0F,0xfff0,0xffff
};
uint16_t M_Background[16] ={
0xffff,0xfff0,0xff0f,0xff00,0xf0ff,0xf0f0,0xf00f,0xf000,0x0fff,0x0ff0,0x0f0f,0x0f00,0x00ff,0x00f0,0x000f,0x0000
};
volatile int ytileheight=16;
#endif
#ifdef rp2350
unsigned char *WriteBuf=AllMemory+HEAP_MEMORY_SIZE+256;
unsigned char *DisplayBuf=AllMemory+HEAP_MEMORY_SIZE+256;
unsigned char *LayerBuf=AllMemory+HEAP_MEMORY_SIZE+256;
unsigned char *FrameBuf=AllMemory+HEAP_MEMORY_SIZE+256;
unsigned char *SecondLayer=AllMemory+HEAP_MEMORY_SIZE+256;
unsigned char *SecondFrame=AllMemory+HEAP_MEMORY_SIZE+256;
#else
unsigned char *WriteBuf=Frame;
unsigned char *DisplayBuf=Frame;
unsigned char *LayerBuf=Frame;
unsigned char *FrameBuf=Frame;
unsigned char *SecondLayer=Frame;
unsigned char *SecondFrame=Frame;
#endif
#endif
#ifdef PICOMITE
    unsigned char *WriteBuf=NULL;
    unsigned char *LayerBuf=NULL;
    unsigned char *FrameBuf=NULL;
#endif
#ifdef GUICONTROLS
    struct s_ctrl CTRLS[MAXCONTROLS];
    struct s_ctrl *Ctrl=NULL;
#endif
#ifdef PICOMITEWEB
    unsigned char *WriteBuf=NULL;
    unsigned char *LayerBuf=NULL;
    unsigned char *FrameBuf=NULL;
#endif

unsigned int mmap[HEAP_MEMORY_SIZE/ PAGESIZE / PAGESPERWORD]={0};
#ifdef rp2350
unsigned int psmap[6*1024*1024/ PAGESIZE / PAGESPERWORD]={0};
unsigned int SBitsGet(unsigned char *addr);
void SBitsSet(unsigned char *addr, int bits);
#endif
unsigned int MBitsGet(unsigned char *addr);
void MBitsSet(unsigned char *addr, int bits);
volatile char *g_StrTmp[MAXTEMPSTRINGS];                                       // used to track temporary string space on the heap
volatile char g_StrTmpLocalIndex[MAXTEMPSTRINGS];                              // used to track the g_LocalIndex for each temporary string space on the heap

void *getheap(int size);
unsigned int UsedHeap(void);
bool g_TempMemoryIsChanged = false;						            // used to prevent unnecessary scanning of strtmp[]
short g_StrTmpIndex = 0;                                                // index to the next unallocated slot in strtmp[]





/***********************************************************************************************************************
 MMBasic commands
************************************************************************************************************************/
/*  @endcond */
void MIPS16 cmd_memory(void) {
	unsigned char *p,*tp;
    tp = checkstring(cmdline, (unsigned char *)"PACK");
    if(tp){
        getargs(&tp,7,(unsigned char *)",");
        if(argc!=7)error("Syntax");
        int i,n=getinteger(argv[4]);
        if(n<=0)return;
        int size=getint(argv[6],1,32);
        if(!(size==1 || size==4 || size==8 || size==16 || size==32))error((char *)"Invalid size");
        int sourcesize,destinationsize;
        void *top=NULL;
        uint64_t *from=NULL;
        if(CheckEmpty((char *)argv[0])){
            sourcesize=parseintegerarray(argv[0],(int64_t **)&from, 1,1,NULL,false);
            if(sourcesize<n)error("Source array too small");
        } else from=(uint64_t *)GetPokeAddr(argv[0]);
        if(CheckEmpty((char *)argv[2])){
            destinationsize=parseintegerarray(argv[2],(int64_t **)&top, 2,1,NULL,true);
            if(destinationsize*64/size<n)error("Destination array too small");
        } else top=(void *)GetPokeAddr(argv[2]);
        if((uint32_t)from % 8)error("Source address not divisible by 8");
        if(size==1){
            uint8_t *to=(uint8_t *)top;
            for(i=0;i<n;i++){
                int s= i % 8;
                if(s==0)*to=0;
                *to |= ((*from++) & 0x1)<<s;
                if(s==7)to++;
           }
        } else if(size==4){
            uint8_t *to=(uint8_t *)top;
            for(i=0;i<n;i++){
                if((i & 1) == 0){
                    *to=(*from++) & 0xF;
                } else {
                    *to |= ((*from++) & 0xF)<<4;
                    to++;
                }
           }
        } else if(size==8){
            uint8_t *to=(uint8_t *)top;
            while(n--){
            *to++=(uint8_t)*from++;
            }
        } else if(size==16){
            uint16_t *to=(uint16_t *)top;
            if((uint32_t)to % 2)error("Destination address not divisible by 2");
            while(n--){
            *to++=(uint16_t)*from++;
            }
        } else if(size==32){
            uint32_t *to=(uint32_t *)top;
            if((uint32_t)to % 4)error("Destination address not divisible by 4");
            while(n--){
            *to++=(uint32_t)*from++;
            }
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"PRINT");
    if(tp){
        char *fromp=NULL;
        int sourcesize;
        int64_t *aint;
        getargs(&tp,5,(unsigned char *)",");
        if(!(argc==5))error("Syntax");
	    if(*argv[0] == '#') argv[0]++;
		int fnbr = getint(argv[0],1,MAXOPENFILES);	// get the number
        int n=getinteger(argv[2]);
        if(CheckEmpty((char *)argv[4])){
            sourcesize=parseintegerarray(argv[4],&aint,3,1,NULL,false);
            if(sourcesize*8<n)error("Source array too small");
            fromp=(char *)aint;
        } else {
            fromp=(char *)GetPeekAddr(argv[4]);
        }
        if (FileTable[fnbr].com > MAXCOMPORTS)
        {
            FilePutStr(n, fromp, fnbr);
        }
        else error("File % not open",fnbr);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"INPUT");
    if(tp){
        char *fromp=NULL;
        int sourcesize;
        int64_t *aint;
        getargs(&tp,5,(unsigned char *)",");
        if(!(argc==5))error("Syntax");
	    if(*argv[0] == '#') argv[0]++;
		int fnbr = getint(argv[0],1,MAXOPENFILES);	// get the number
        int n=getinteger(argv[2]);
        if(CheckEmpty((char *)argv[4])){
            sourcesize=parseintegerarray(argv[4],&aint,3,1,NULL,false);
            if(sourcesize*8<n)error("Source array too small");
            fromp=(char *)aint;
        } else {
            fromp=(char *)GetPokeAddr(argv[4]);
        }
        if (FileTable[fnbr].com > MAXCOMPORTS)
        {
            while(!(MMfeof(fnbr)) && n--) *fromp++=FileGetChar(fnbr);
            if(n)error("End of file");
        }
        else error("File % not open",fnbr);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"UNPACK");
    if(tp){
        getargs(&tp,7,(unsigned char *)",");
        if(argc!=7)error("Syntax");
        int i,n=getinteger(argv[4]);
        if(n<=0)return;
        int size=getint(argv[6],1,32);
        if(!(size==1 || size==4 || size==8 || size==16 || size==32))error((char *)"Invalid size");
        int sourcesize,destinationsize;
        uint64_t *to=NULL;
        void *fromp=NULL;
        if(CheckEmpty((char *)argv[0])){
            sourcesize=parseintegerarray(argv[0],(int64_t **)&fromp, 1,1,NULL,false);
            if(sourcesize*64/size<n)error("Source array too small");
        } else {
            fromp=(void*)GetPokeAddr(argv[0]);
        }
        if(CheckEmpty((char *)argv[2])){
            destinationsize=parseintegerarray(argv[2],(int64_t **)&to, 2,1,NULL,true);
            if(n>destinationsize)error("Destination array too small");
        } else to=(uint64_t *)GetPokeAddr(argv[2]);
        if((uint32_t)to % 8)error("Source address not divisible by 8");
        if(size==1){
            uint8_t *from=(uint8_t *)fromp;
            for(i=0;i<n;i++){
                int s= i % 8;
                *to++ = ((*from & (1<<s)) ? 1 : 0);
                if(s==7)from++;
           }

        } else if(size==4){
            uint8_t *from=(uint8_t *)fromp;
            for(i=0;i<n;i++){
                if((i & 1) == 0){
                    *to++=(*from) & 0xF;
                } else {
                    *to++ = (*from) >> 4;
                    from++;
                }
           }
        } else if(size==8){
            uint8_t *from=(uint8_t *)fromp;
            while(n--){
            *to++=(uint64_t)*from++;
            }
        } else if(size==16){
            uint16_t *from=(uint16_t *)fromp;
            if((uint32_t)from % 2)error("Source address not divisible by 2");
            while(n--){
            *to++=(uint64_t)*from++;
            }
        } else if(size==32){
            uint32_t *from=(uint32_t *)fromp;
            if((uint32_t)from % 4)error("Source address not divisible by 4");
            while(n--){
            *to++=(uint64_t)*from++;
            }
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"COPY");
    if(tp){
    	if((p = checkstring(tp, (unsigned char *)"INTEGER"))) {
    		int stepin=1, stepout=1;
        	getargs(&p,9,(unsigned char *)",");
        	if(argc<5)error("Syntax");
        	int n=getinteger(argv[4]);
        	if(n<=0)return;
         	uint64_t *from=(uint64_t *)GetPokeAddr(argv[0]);
         	uint64_t *to=(uint64_t *)GetPokeAddr(argv[2]);
        	if((uint32_t)from % 8)error("Address not divisible by 8");
        	if((uint32_t)to % 8)error("Address not divisible by 8");
        	if(argc>=7 && *argv[6])stepin=getint(argv[6],0,0xFFFF);
        	if(argc==9)stepout=getint(argv[8],0,0xFFFF);
        	if(stepin==1 && stepout==1)memmove(to, from, n*8);
        	else{
                if(from<to){
                    from+=(n-1)*stepin;
                    to+=(n-1)*stepout;
                    while(n--){
                        *to=*from;
                        to-=stepout;
                        from-=stepin;
                    }
                } else {
                    while(n--){
                        *to=*from;
                        to+=stepout;
                        from+=stepin;
                    }
                }
        	}
    		return;
    	}
    	if((p = checkstring(tp, (unsigned char *)"FLOAT"))) {
    		int stepin=1, stepout=1;
        	getargs(&p,9,(unsigned char *)","); //assume byte
        	if(argc<5)error("Syntax");
        	int n=getinteger(argv[4]);
        	if(n<=0)return;
        	MMFLOAT *from=(MMFLOAT *)GetPokeAddr(argv[0]);
        	MMFLOAT *to=(MMFLOAT *)GetPokeAddr(argv[2]);
        	if((uint32_t)from % 8)error("Address not divisible by 8");
        	if((uint32_t)to % 8)error("Address not divisible by 8");
        	if(argc>=7 && *argv[6])stepin=getint(argv[6],0,0xFFFF);
        	if(argc==9)stepout=getint(argv[8],0,0xFFFF);
        	if(n<=0)return;
        	if(stepin==1 && stepout==1)memmove(to, from, n*8);
        	else{
                if(from<to){
                    from+=(n-1)*stepin;
                    to+=(n-1)*stepout;
                    while(n--){
                        *to=*from;
                        to-=stepout;
                        from-=stepin;
                    }
                } else {
                    while(n--){
                        *to=*from;
                        to+=stepout;
                        from+=stepin;
                    }
                }
        	}
    		return;
    	}
        getargs(&tp,9,(unsigned char *)","); //assume byte
        if(argc<5)error("Syntax");
        int stepin=1, stepout=1;
    	char *from=(char *)GetPeekAddr(argv[0]);
    	char *to=(char *)GetPokeAddr(argv[2]);
    	int n=getinteger(argv[4]);
        if(argc>=7 && *argv[6])stepin=getint(argv[6],0,0xFFFF);
        if(argc==9)stepout=getint(argv[8],0,0xFFFF);
        if(n<=0)return;
    	if(stepin==1 && stepout==1)memmove(to, from, n);
        else {
            if(from<to){
                from+=(n-1)*stepin;
                to+=(n-1)*stepout;
                while(n--){
                    *to=*from;
                    to-=stepout;
                    from-=stepin;
                }
            } else {
                while(n--){
                    *to=*from;
                    to+=stepout;
                    from+=stepin;
                }
            }
        }
    	return;
    }
    tp = checkstring(cmdline, (unsigned char *)"SET");
    if(tp){
    	unsigned char *p;
    	if((p = checkstring(tp, (unsigned char *)"BYTE"))) {
        	getargs(&p,5,(unsigned char *)","); //assume byte
        	if(argc!=5)error("Syntax");
         	char *to=(char *)GetPokeAddr(argv[0]);
         	int val=getint(argv[2],0,255);
        	int n=getinteger(argv[4]);
        	if(n<=0)return;
        	memset(to, val, n);
    		return;
    	}
    	if((p = checkstring(tp, (unsigned char *)"SHORT"))) {
        	getargs(&p,5,(unsigned char *)","); //assume byte
        	if(argc!=5)error("Syntax");
         	short *to=(short *)GetPokeAddr(argv[0]);
        	if((uint32_t)to % 2)error("Address not divisible by 2");
        	short *q=to;
   		    short data=getint(argv[2],0,65535);
        	int n=getinteger(argv[4]);
        	if(n<=0)return;
        	while(n>0){
                *q++=data;
                n--;  
        	}
    		return;
    	}
    	if((p = checkstring(tp, (unsigned char *)"WORD"))) {
        	getargs(&p,5,(unsigned char *)","); //assume byte
        	if(argc!=5)error("Syntax");
         	unsigned int *to=(unsigned int *)GetPokeAddr(argv[0]);
        	if((uint32_t)to % 4)error("Address not divisible by 4");
        	unsigned int *q=to;
   		    unsigned int data=getint(argv[2],0,0xFFFFFFFF);
        	int n=getinteger(argv[4]);
        	if(n<=0)return;
        	while(n>0){
                *q++=data;
                n--;  
        	}
    		return;
     	}
    	if((p = checkstring(tp, (unsigned char *)"INTEGER"))) {
    		int stepin=1;
        	getargs(&p,7,(unsigned char *)",");
        	if(argc<5)error("Syntax");
         	uint64_t *to=(uint64_t *)GetPokeAddr(argv[0]);
        	if((uint32_t)to % 8)error("Address not divisible by 8");
        	int64_t data;
    		data=getinteger(argv[2]);
        	int n=getinteger(argv[4]);
        	if(argc==7)stepin=getint(argv[6],0,0xFFFF);
        	if(n<=0)return;
        	if(stepin==1)while(n--)*to++=data;
        	else{
            	while(n--){
            		*to=data;
            		to+=stepin;
            	}
        	}
    		return;
    	}
    	if((p = checkstring(tp, (unsigned char *)"FLOAT"))) {
    		int stepin=1;
        	getargs(&p,7,(unsigned char *)","); //assume byte
        	if(argc<5)error("Syntax");
        	MMFLOAT *to=(MMFLOAT *)GetPokeAddr(argv[0]);
        	if((uint32_t)to % 8)error("Address not divisible by 8");
        	MMFLOAT data;
    		data=getnumber(argv[2]);
        	int n=getinteger(argv[4]);
           	if(argc==7)stepin=getint(argv[6],0,0xFFFF);
        	if(n<=0)return;
        	if(stepin==1)while(n--)*to++=data;
        	else{
            	while(n--){
            		*to=data;
            		to+=stepin;
            	}
        	}
    		return;
    	}
    	getargs(&tp,5,(unsigned char *)","); //assume byte
    	if(argc!=5)error("Syntax");
     	char *to=(char *)GetPokeAddr(argv[0]);
     	int val=getint(argv[2],0,255);
    	int n=getinteger(argv[4]);
    	if(n<=0)return;
    	memset(to, val, n);
    	return;
    }
    //MEMORY Usage
    int i, j, var, nbr, vsize, VarCnt;
    int ProgramSize, ProgramPercent, VarSize, VarPercent, GeneralSize, GeneralPercent, SavedVarSize, SavedVarSizeK, SavedVarPercent, SavedVarCnt;
    int CFunctSize, CFunctSizeK, CFunctNbr, CFunctPercent, FontSize, FontSizeK, FontNbr, FontPercent, LibrarySizeK, LibraryPercent,LibraryMaxK;
    unsigned int CurrentRAM, *pint;

    CurrentRAM = heap_memory_size + MAXVARS * sizeof(struct s_vartbl);
#ifdef rp2350
    CurrentRAM+=PSRAMsize;
#endif
    // calculate the space allocated to variables on the heap
    for(i = VarCnt = vsize = var = 0; var < MAXVARS; var++) {
        if(g_vartbl[var].type == T_NOTYPE) continue;
        VarCnt++;  vsize += sizeof(struct s_vartbl);
        if(g_vartbl[var].val.s == NULL) continue;
        if(g_vartbl[var].type & T_PTR) continue;
        nbr = g_vartbl[var].dims[0] + 1 - g_OptionBase;
        if(g_vartbl[var].dims[0]) {
            for(j = 1; j < MAXDIM && g_vartbl[var].dims[j]; j++)
                nbr *= (g_vartbl[var].dims[j] + 1 - g_OptionBase);
            if(g_vartbl[var].type & T_NBR)
                i += MRoundUp(nbr * sizeof(MMFLOAT));
            else if(g_vartbl[var].type & T_INT)
                i += MRoundUp(nbr * sizeof(long long int));
            else
                i += MRoundUp(nbr * (g_vartbl[var].size + 1));
        } else
            if(g_vartbl[var].type & T_STR)
                i += STRINGSIZE;
    }
    VarSize = (vsize + i + 512)/1024;                               // this is the memory allocated to variables
    VarPercent = ((vsize + i) * 100)/CurrentRAM;
    if(VarCnt && VarSize == 0) VarPercent = VarSize = 1;            // adjust if it is zero and we have some variables
    i = UsedHeap() - i;
    if(i < 0) i = 0;
    GeneralSize = (i + 512)/1024; GeneralPercent = (i * 100)/CurrentRAM;

    // count the space used by saved variables (in flash)
    p = (unsigned char *)SavedVarsFlash;
    SavedVarCnt = 0;
    while(!(*p == 0 || *p == 0xff)) {
        unsigned char type, array;
        SavedVarCnt++;
        type = *p++;
        array = type & 0x80;  type &= 0x7f;                         // set array to true if it is an array
        p += strlen((char *)p) + 1;
        if(array)
            p += (p[0] | p[1] << 8 | p[2] << 16| p[3] << 24) + 4;
        else {
            if(type &  T_NBR)
                p += sizeof(MMFLOAT);
            else if(type &  T_INT)
                p += sizeof(long long int);
            else
                p += *p + 1;
        }
    }
    SavedVarSize = p - (SavedVarsFlash);
    SavedVarSizeK = (SavedVarSize + 512) / 1024;
    SavedVarPercent = (SavedVarSize * 100) / (/*MAX_PROG_SIZE +*/ SAVEDVARS_FLASH_SIZE);
    if(SavedVarCnt && SavedVarSizeK == 0) SavedVarPercent = SavedVarSizeK = 1;        // adjust if it is zero and we have some variables

    // count the space used by CFunctions, CSubs and fonts
    CFunctSize = CFunctNbr = FontSize = FontNbr = 0;
    pint = (unsigned int *)CFunctionFlash;
    while(*pint != 0xffffffff) {
        //if(*pint < FONT_TABLE_SIZE) {
        if(*pint >> 31 ){    
            pint++;
            FontNbr++;
            FontSize += *pint + 8;
        } else {
            pint++;
            CFunctNbr++;
            CFunctSize += *pint + 8;
        }
        pint += (*pint + 4) / sizeof(unsigned int);
    }
    CFunctPercent = (CFunctSize * 100) /  (MAX_PROG_SIZE + SAVEDVARS_FLASH_SIZE);
    CFunctSizeK = (CFunctSize + 512) / 1024;
    if(CFunctNbr && CFunctSizeK == 0) CFunctPercent = CFunctSizeK = 1;              // adjust if it is zero and we have some functions
    FontPercent = (FontSize * 100) /  (MAX_PROG_SIZE /*+ SAVEDVARS_FLASH_SIZE*/);
    FontSizeK = (FontSize + 512) / 1024;
    if(FontNbr && FontSizeK == 0) FontPercent = FontSizeK = 1;                      // adjust if it is zero and we have some functions

    // count the number of lines in the program
    p = ProgMemory;
    i = 0;
	while(*p != 0xff) {                                             // skip if program memory is erased
        if(*p == 0) p++;                                            // if it is at the end of an element skip the zero marker
        if(*p == 0) break;                                          // end of the program or module
        if(*p == T_NEWLINE) {
            i++;                                                    // count the line
            p++;                                                    // skip over the newline token
        }
        if(*p == T_LINENBR) p += 3;                                 // skip over the line number
		skipspace(p);
		if(p[0] == T_LABEL) p += p[1] + 2;							// skip over the label
		while(*p) p++;												// look for the zero marking the start of an element
    }
    ProgramSize = ((p - ProgMemory) + 512)/1024;
    ProgramPercent = ((p - ProgMemory) * 100)/(MAX_PROG_SIZE /*+ SAVEDVARS_FLASH_SIZE*/);
    if(ProgramPercent > 100) ProgramPercent = 100;
    if(i && ProgramSize == 0) ProgramPercent = ProgramSize = 1;                                        // adjust if it is zero and we have some lines

    MMPrintString("Program:\r\n");
    IntToStrPad((char *)inpbuf, ProgramSize, ' ', 4, 10); strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), ProgramPercent, ' ', 2, 10); strcat((char *)inpbuf, "%) Program (");
    IntToStr((char *)inpbuf + strlen((char *)inpbuf), i, 10); strcat((char *)inpbuf, " lines)\r\n");
	MMPrintString((char *)inpbuf);

    if(CFunctNbr) {
        IntToStrPad((char *)inpbuf, CFunctSizeK, ' ', 4, 10); strcat((char *)inpbuf, "K (");
        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), CFunctPercent, ' ', 2, 10); strcat((char *)inpbuf, "%) "); MMPrintString((char *)inpbuf);
        IntToStr((char *)inpbuf, CFunctNbr, 10); strcat((char *)inpbuf, " Embedded C Routine"); strcat((char *)inpbuf, CFunctNbr == 1 ? "\r\n":"s\r\n");
        MMPrintString((char *)inpbuf);
    }

    if(FontNbr) {
        IntToStrPad((char *)inpbuf, FontSizeK, ' ', 4, 10); strcat((char *)inpbuf, "K (");
        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), FontPercent, ' ', 2, 10); strcat((char *)inpbuf, "%) "); MMPrintString((char *)inpbuf);
        IntToStr((char *)inpbuf, FontNbr, 10); strcat((char *)inpbuf, " Embedded Fonts"); strcat((char *)inpbuf, FontNbr == 1 ? "\r\n":"s\r\n");
        MMPrintString((char *)inpbuf);
    }
/*
    if(SavedVarCnt) {
        IntToStrPad(inpbuf, SavedVarSizeK, ' ', 4, 10); strcat((char *)inpbuf, "K (");
        IntToStrPad(inpbuf + strlen(inpbuf), SavedVarPercent, ' ', 2, 10); strcat((char *)inpbuf, "%)");
        IntToStrPad(inpbuf + strlen(inpbuf), SavedVarCnt, ' ', 2, 10); strcat((char *)inpbuf, " Saved Variable"); strcat((char *)inpbuf, SavedVarCnt == 1 ? " (":"s (");
        IntToStr((char *)inpbuf + strlen(inpbuf), SavedVarSize, 10); strcat((char *)inpbuf, " bytes)\r\n");
        MMPrintString(inpbuf);
    }
*/



    IntToStrPad((char *)inpbuf, ((MAX_PROG_SIZE/* + SAVEDVARS_FLASH_SIZE*/) + 512)/1024 - ProgramSize - CFunctSizeK - FontSizeK /*- SavedVarSizeK - LibrarySizeK*/, ' ', 4, 10); strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), 100 - ProgramPercent - CFunctPercent - FontPercent /*- SavedVarPercent - LibraryPercent*/, ' ', 2, 10); strcat((char *)inpbuf, "%) Free\r\n");
	MMPrintString((char *)inpbuf);

     //Get the library size
    LibrarySizeK = LibraryPercent = 0;
    LibraryMaxK= MAX_PROG_SIZE/1024;
    if(Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE) {
           i = 0;
           // first count the normal program code residing in the Library
           p = LibMemory;
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
             j=0;
             pint = (unsigned int *)CFunctionLibrary;
             while(*pint != 0xffffffff) {
              pint++;                                      //step over the address or Font No.
              j += *pint + 8;                              //Read the size
              pint += (*pint + 4) / sizeof(unsigned int);  //set pointer to start of next CSUB/Font
             }
             i=i+j;
           }


           LibrarySizeK=(i+512)/1024;
           LibraryPercent = (LibrarySizeK * 100)/LibraryMaxK;
           if(LibrarySizeK == 0) LibrarySizeK = 1;              // adjust if it is zero and we have any library
           if(LibraryPercent == 0) LibraryPercent = 1;          // adjust if it is zero and we have any library
     
           MMPrintString("\r\nLibrary:\r\n");
        
           IntToStrPad((char *)inpbuf, LibrarySizeK, ' ', 4, 10); strcat((char *)inpbuf, "K (");
	       //IntToStrPad(inpbuf, (128*1024  + 512)/1024  - LibrarySizeK, ' ', 4, 10); strcat((char *)inpbuf, "K (");
	       IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), LibraryPercent, ' ', 2, 10); strcat((char *)inpbuf, "%) "); strcat((char *)inpbuf, "Library\r\n");
	       IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), LibraryMaxK-LibrarySizeK, ' ', 4, 10); strcat((char *)inpbuf, "K (");
	       IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), 100 - LibraryPercent, ' ', 2, 10); strcat((char *)inpbuf, "%) Free\r\n");
	       MMPrintString((char *)inpbuf);
       }
   

     MMPrintString("\r\nSaved Variables:\r\n");
	 if(SavedVarCnt) {
	        IntToStrPad((char *)inpbuf, SavedVarSizeK, ' ', 4, 10); strcat((char *)inpbuf, "K (");
	        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), SavedVarPercent, ' ', 2, 10); strcat((char *)inpbuf, "%)");
	        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), SavedVarCnt, ' ', 2, 10); strcat((char *)inpbuf, " Saved Variable"); strcat((char *)inpbuf, SavedVarCnt == 1 ? " (":"s (");
	        IntToStr((char *)inpbuf + strlen((char *)inpbuf), SavedVarSize, 10); strcat((char *)inpbuf, " bytes)\r\n");
	        MMPrintString((char *)inpbuf);
	 }
	 IntToStrPad((char *)inpbuf, (( SAVEDVARS_FLASH_SIZE) + 512)/1024 - SavedVarSizeK, ' ', 4, 10); strcat((char *)inpbuf, "K (");
	 IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), 100 -  SavedVarPercent, ' ', 2, 10); strcat((char *)inpbuf, "%) Free\r\n");
	 MMPrintString((char *)inpbuf);


    MMPrintString("\r\nRAM:\r\n");
    IntToStrPad((char *)inpbuf, VarSize, ' ', 4, 10); strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), VarPercent, ' ', 2, 10); strcat((char *)inpbuf, "%) ");
    IntToStr((char *)inpbuf + strlen((char *)inpbuf), VarCnt, 10); strcat((char *)inpbuf, " Variable"); strcat((char *)inpbuf, VarCnt == 1 ? "\r\n":"s\r\n");
	MMPrintString((char *)inpbuf);

    IntToStrPad((char *)inpbuf, GeneralSize, ' ', 4, 10); strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), GeneralPercent, ' ', 2, 10); strcat((char *)inpbuf, "%) General\r\n");
	MMPrintString((char *)inpbuf);

    IntToStrPad((char *)inpbuf, (CurrentRAM + 512)/1024 - VarSize - GeneralSize, ' ', 4, 10); strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), 100 - VarPercent - GeneralPercent, ' ', 2, 10); strcat((char *)inpbuf, "%) Free\r\n");
	MMPrintString((char *)inpbuf);
}

/* 
 * @cond
 * The following section will be excluded from the documentation.
 */

/***********************************************************************************************************************
 Public memory management functions
************************************************************************************************************************/

/* all memory allocation (except for the heap) is made by m_alloc() 
   memory layout is based on static allocation of RAM (very simple)
   see the Maximite version of MMBasic for a more complex dynamic memory management scheme
   
          |--------------------|
          |                    |
          |    MMBasic Heap    |
          |    (grows down)    |
          |                    |
          |--------------------|   <<<   MMHeap
          
          
          |--------------------|
          |   Variable Table   |
          |     (grows up)     |
          |--------------------|   <<<   g_vartbl and DOS_vartbl
          
          
          |--------------------|
          |                    |
          |   Program Memory   |
          |     (grows up)     |
          |                    |
          |--------------------|   <<<   ProgMemory and DOS_ProgMemory

  Calls are made to m_alloc() to assign the various pointers (ProgMemory, etc)
  These calls must be made in this sequence:
        m_alloc(M_PROG, size)       Called whenever program memory size changes
        m_alloc(M_VAR, size)        Called when the program is running and whenever the variable table needs to be expanded
        
   Separately calls are made to getmemory() and FreeHeap() to allocate or free space on the heap (which grows downward).
   
*/


void m_alloc(int type) {
    switch(type) {

        case M_PROG:
#ifdef rp2350
#ifndef PICOMITEWEB
                        if(PSRAMsize)memset((uint8_t *)PSRAMbase,0,PSRAMsize);
#endif
#endif
        case M_LIMITED:    // this is called initially in InitBasic() to set the base pointer for program memory
                        // everytime the program size is adjusted up or down this must be called to check for memory overflow
                        ProgMemory = (uint8_t *)flash_progmemory;
                        memset(MMHeap,0,heap_memory_size);
#ifdef GUICONTROLS
                        if(Option.MaxCtrls) Ctrl=(struct s_ctrl *)CTRLS;
#endif
                        break;
                        
        case M_VAR:     // this must be called to initialises the variable memory pointer
                        // everytime the variable table is increased this must be called to verify that enough memory is free
                        memset(g_vartbl,0,MAXVARS * sizeof(struct s_vartbl));
                        break;
    }
}



// get some memory from the heap
//void *GetMemory(size_t  msize) {
//    return getheap(msize);                                          // allocate space
//}


// Get a temporary buffer of any size
// The space only lasts for the length of the command.
// A pointer to the space is saved in an array so that it can be returned at the end of the command
void __not_in_flash_func(*GetTempMemory)(int NbrBytes) {
    if(g_StrTmpIndex >= MAXTEMPSTRINGS) error("Not enough memory");
    g_StrTmpLocalIndex[g_StrTmpIndex] = g_LocalIndex;
    g_StrTmp[g_StrTmpIndex] = GetMemory(NbrBytes);
    g_TempMemoryIsChanged = true;
    return (void *)g_StrTmp[g_StrTmpIndex++];
}


// get a temporary string buffer
// this is used by many BASIC string functions.  The space only lasts for the length of the command.
//void *GetTempStrMemory(void) {
//    return GetTempMemory(STRINGSIZE);
//}


// clear any temporary string spaces (these last for just the life of a command) and return the memory to the heap
// this will not clear memory allocated with a local index less than g_LocalIndex, sub/funs will increment g_LocalIndex
// and this prevents the automatic use of ClearTempMemory from clearing memory allocated before calling the sub/fun
void __not_in_flash_func(ClearTempMemory)(void) {
    while(g_StrTmpIndex > 0) {
        if(g_StrTmpLocalIndex[g_StrTmpIndex - 1] >= g_LocalIndex) {
            g_StrTmpIndex--;
            FreeMemory((void *)g_StrTmp[g_StrTmpIndex]);
            g_StrTmp[g_StrTmpIndex] = NULL;
            g_TempMemoryIsChanged = false;
        } else
            break;
    }
}



void __not_in_flash_func(ClearSpecificTempMemory)(void *addr) {
    int i;
    for(i = 0; i < g_StrTmpIndex; i++) {
        if(g_StrTmp[i] == addr) {
            FreeMemory(addr);
            g_StrTmp[i] = NULL;
            g_StrTmpIndex--;
            while(i < g_StrTmpIndex) {
                g_StrTmp[i] = g_StrTmp[i + 1];
                g_StrTmpLocalIndex[i] = g_StrTmpLocalIndex[i + 1];
                i++;
            }
            return;
        }
    }
}


// test the stack for overflow - this is a NULL function in the DOS version
void __not_in_flash_func(TestStackOverflow)(void) {
//    static uint32_t x=0xFFFFFFFF;
    uint32_t y=__get_MSP();
//    if(y<x){
//        x=y;PIntH(x);PRet();
//    }
    if(y< HEAPTOP) error("Stack overflow, expression too complex at depth %",g_LocalIndex);
}



void __not_in_flash_func(FreeMemory)(unsigned char *addr) {
    if(addr == NULL) return;
#ifdef rp2350
#ifndef PICOMITEWEB
    int bits;
    if(addr>(unsigned char *)PSRAMbase && addr<(unsigned char *)(PSRAMbase+PSRAMsize)){
        do {
            bits = SBitsGet(addr);
            SBitsSet(addr, 0);
            addr += PAGESIZE;
        } while(bits != (PUSED | PLAST));
    } else {
        do {
            bits = MBitsGet(addr);
            MBitsSet(addr, 0);
            addr += PAGESIZE;
        } while(bits != (PUSED | PLAST));
    }
#else
    int bits;
    do {
        bits = MBitsGet(addr);
        MBitsSet(addr, 0);
        addr += PAGESIZE;
    } while(bits != (PUSED | PLAST));
#endif
#else
    int bits;
    do {
        bits = MBitsGet(addr);
        MBitsSet(addr, 0);
        addr += PAGESIZE;
    } while(bits != (PUSED | PLAST));
#endif
}



void InitHeap(bool all) {
    int i;
    memset(mmap,0,sizeof(mmap));
#ifdef rp2350
    if(all)memset(psmap,0,sizeof(psmap));
#endif
    for(i = 0; i < MAXTEMPSTRINGS; i++) g_StrTmp[i] = NULL;
#ifdef PICOMITEVGA
    WriteBuf=(unsigned char *)FRAMEBUFFER;
    DisplayBuf=(unsigned char *)FRAMEBUFFER;
    LayerBuf=(unsigned char *)FRAMEBUFFER;
    FrameBuf=(unsigned char *)FRAMEBUFFER;
#else
    FrameBuf=NULL;
    WriteBuf=NULL;
    LayerBuf=NULL;
#endif
}




/***********************************************************************************************************************
 Private memory management functions
************************************************************************************************************************/

#ifdef rp2350
#ifndef PICOMITEWEB
unsigned int __not_in_flash_func(SBitsGet)(unsigned char *addr) {
    unsigned int i, *p;
    addr -= (unsigned int)PSRAMbase;
    p = &psmap[((unsigned int)addr/PAGESIZE) / PAGESPERWORD];        // point to the word in the memory map
    i = ((((unsigned int)addr/PAGESIZE)) & (PAGESPERWORD - 1)) * PAGEBITS; // get the position of the bits in the word
    return (*p >> i) & ((1 << PAGEBITS) -1);
}

void __not_in_flash_func(SBitsSet)(unsigned char *addr, int bits) {
    unsigned int i, *p;
    addr -= (unsigned int)PSRAMbase;
    p = &psmap[((unsigned int)addr/PAGESIZE) / PAGESPERWORD];        // point to the word in the memory map
    i = ((((unsigned int)addr/PAGESIZE)) & (PAGESPERWORD - 1)) * PAGEBITS; // get the position of the bits in the word
    *p = (bits << i) | (*p & (~(((1 << PAGEBITS) -1) << i)));
}
#endif
#endif

unsigned int __not_in_flash_func(MBitsGet)(unsigned char *addr) {
    unsigned int i, *p;
    addr -= (unsigned int)&MMHeap[0];
    p = &mmap[((unsigned int)addr/PAGESIZE) / PAGESPERWORD];        // point to the word in the memory map
    i = ((((unsigned int)addr/PAGESIZE)) & (PAGESPERWORD - 1)) * PAGEBITS; // get the position of the bits in the word
    return (*p >> i) & ((1 << PAGEBITS) -1);
}



void __not_in_flash_func(MBitsSet)(unsigned char *addr, int bits) {
    unsigned int i, *p;
    addr -= (unsigned int)&MMHeap[0];
    p = &mmap[((unsigned int)addr/PAGESIZE) / PAGESPERWORD];        // point to the word in the memory map
    i = ((((unsigned int)addr/PAGESIZE)) & (PAGESPERWORD - 1)) * PAGEBITS; // get the position of the bits in the word
    *p = (bits << i) | (*p & (~(((1 << PAGEBITS) -1) << i)));
}
#ifdef rp2350
#ifndef PICOMITEWEB
void __not_in_flash_func(*GetPSMemory)(int size) {
    unsigned int j, n;
    unsigned char *addr;
    j = n = (size + PAGESIZE - 1)/PAGESIZE;                         // nbr of pages rounded up
    for(addr = (unsigned char *)(PSRAMbase + PSRAMsize - PAGESIZE); addr >= (unsigned char *)PSRAMbase; addr -= PAGESIZE) {
        if(!(SBitsGet(addr) & PUSED)) {
            if(--n == 0) {                                          // found a free slot
                j--;
                SBitsSet(addr + (j * PAGESIZE), PUSED | PLAST);     // show that this is used and the last in the chain of pages
                while(j--) SBitsSet(addr + (j * PAGESIZE), PUSED);  // set the other pages to show that they are used
                memset(addr, 0, size);                              // zero the memory
 //               dp("alloc = %p (%d)", addr, size);
                return (void *)addr;
            }
        } else
            n = j;                                                  // not enough space here so reset our count
    }
    // out of memory
    TempStringClearStart = 0;
    ClearTempMemory();                                               // hopefully this will give us enough to print the prompt
    error("Not enough PSRAM memory");
    return NULL;                                                    // keep the compiler happy
}  
#endif  
#endif

void __not_in_flash_func(*GetMemory)(int size) {
    unsigned int j, n;
    unsigned char *addr;
    j = n = (size + PAGESIZE - 1)/PAGESIZE;                         // nbr of pages rounded up
    for(addr = MMHeap + heap_memory_size - PAGESIZE; addr >= MMHeap; addr -= PAGESIZE) {
        if(!(MBitsGet(addr) & PUSED)) {
            if(--n == 0) {                                          // found a free slot
                j--;
                MBitsSet(addr + (j * PAGESIZE), PUSED | PLAST);     // show that this is used and the last in the chain of pages
                while(j--) MBitsSet(addr + (j * PAGESIZE), PUSED);  // set the other pages to show that they are used
                memset(addr, 0, size);                              // zero the memory
 //               dp("alloc = %p (%d)", addr, size);
                return (void *)addr;
            }
        } else
            n = j;                                                  // not enough space here so reset our count
    }
    // out of memory
#ifdef rp2350
#ifndef PICOMITEWEB
    if(PSRAMsize)return GetPSMemory(size);
#endif
#endif
    TempStringClearStart = 0;
    ClearTempMemory();                                               // hopefully this will give us enough to print the prompt
    error("Not enough Heap memory");
    return NULL;                                                    // keep the compiler happy
}    

void *GetAlignedMemory(int size) {
   unsigned char *addr=MMHeap;
    while(((uint32_t)addr & (size-1)) && (!((MBitsGet(addr) & PUSED))) && ((uint32_t)addr<(uint32_t)MMHeap+heap_memory_size))addr+=PAGESIZE;
    if((uint32_t)addr==(uint32_t)MMHeap+heap_memory_size)error("Not enough memory");
    unsigned char *retaddr=addr;
    for(;size>0;addr+=PAGESIZE, size-=PAGESIZE){
         if(!(MBitsGet(addr) & PUSED)){
            MBitsSet(addr,PUSED);
         } else error("Not enough Aigned memory");
    }
    addr-=PAGESIZE;
    MBitsSet(addr, PUSED | PLAST); 
    return(retaddr);
}    


int FreeSpaceOnHeap(void) {
    unsigned int nbr;
    unsigned char *addr;
    nbr = 0;
    for(addr = MMHeap + heap_memory_size - PAGESIZE; addr >= MMHeap; addr -= PAGESIZE)
        if(!(MBitsGet(addr) & PUSED)) nbr++;
#ifdef rp2350
#ifndef PICOMITEWEB
    if(PSRAMsize){
        for(addr = (unsigned char*)(PSRAMbase + PSRAMsize - PAGESIZE); addr >= (unsigned char*)PSRAMbase; addr -= PAGESIZE)
            if(!(SBitsGet(addr) & PUSED)) nbr++;
    }
#endif
#endif
    return nbr * PAGESIZE;
}    
    
int LargestContiguousHeap(void) {
    unsigned int nbr;
    unsigned char *addr;
    nbr = 0;
    for(addr = MMHeap; addr < MMHeap + heap_memory_size - PAGESIZE; addr += PAGESIZE){
        if(!(MBitsGet(addr) & PUSED)) nbr++;
        else break;
    }
    return nbr * PAGESIZE;
}    


unsigned int UsedHeap(void) {
    unsigned int nbr;
    unsigned char *addr;
    nbr = 0;
    for(addr = MMHeap + heap_memory_size - PAGESIZE; addr >= MMHeap; addr -= PAGESIZE)
        if(MBitsGet(addr) & PUSED) nbr++;
#ifdef rp2350
#ifndef PICOMITEWEB
    if(PSRAMsize){
        for(addr = (unsigned char*)(PSRAMbase + PSRAMsize - PAGESIZE); addr >= (unsigned char*)PSRAMbase; addr -= PAGESIZE)
            if(SBitsGet(addr) & PUSED) nbr++;
    }
#endif
#endif
    return nbr * PAGESIZE;
}    

int MemSize(void *addr){ //returns the amount of heap memory allocated to an address
    int i=0;
    int bits;
#ifdef rp2350
#ifndef PICOMITEWEB
    if(addr>(void *)PSRAMbase && addr<(void *)(PSRAMbase+PSRAMsize)){
        if(addr >= (void *)PSRAMbase && addr < (void *)(PSRAMbase + PSRAMsize)){
            do {
                bits = SBitsGet(addr);
                addr += PAGESIZE;
                i+=PAGESIZE;
            } while(bits != (PUSED | PLAST));
        }
        return i;
    } else {
        if(addr >= (void *)MMHeap && addr < (void *)(MMHeap + heap_memory_size)){
            do {
                bits = MBitsGet(addr);
                addr += PAGESIZE;
                i+=PAGESIZE;
            } while(bits != (PUSED | PLAST));
        }
        return i;
    }
#else
    if(addr >= (void *)MMHeap && addr < (void *)(MMHeap + heap_memory_size)){
        do {
            bits = MBitsGet(addr);
            addr += PAGESIZE;
            i+=PAGESIZE;
        } while(bits != (PUSED | PLAST));
    }
    return i;
#endif
#else
    if(addr >= (void *)MMHeap && addr < (void *)(MMHeap + heap_memory_size)){
        do {
            bits = MBitsGet(addr);
            addr += PAGESIZE;
            i+=PAGESIZE;
        } while(bits != (PUSED | PLAST));
    }
    return i;
#endif
}

void *ReAllocMemory(void *addr, size_t msize){
	int size=MemSize(addr);
	if(msize<=size)return addr;
	void *newaddr=GetMemory(msize);
	if(addr!=NULL && size!=0){
		memcpy(newaddr,addr,MemSize(addr));
		FreeMemory(addr);
        addr=NULL;

	}
	return newaddr;
}
void __not_in_flash_func(FreeMemorySafe)(void **addr){
	if(*addr!=NULL){
        if(*addr >= (void *)MMHeap && *addr < (void *)(MMHeap + heap_memory_size)) {FreeMemory(*addr);*addr=NULL;}
#ifdef rp2350
#ifndef PICOMITEWEB
        if(*addr >= (void *)PSRAMbase && *addr < (void *)(PSRAMbase + PSRAMsize)) {FreeMemory(*addr);*addr=NULL;}
#endif
#endif
	}
}
/*  @endcond */

