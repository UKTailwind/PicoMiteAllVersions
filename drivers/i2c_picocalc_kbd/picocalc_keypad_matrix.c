/*
 * ports/pico_rp2350/picocalc_keypad.c — PicoCalc hardware keypad scanner.
 *
 * Only compiled for COMPILE=PICORP2350 and COMPILE=PICOUSBRP2350
 * because the PicoCalc board (RP2350 PicoMite variant) is the only
 * target that wires its native keypad into the GPIO matrix on pins
 * 31..40 / 26..30 and drives it through LocalKeyDown[].
 *
 * Declaration is in AllCommands.h (`void cmd_keyscan(void)`); PicoMite.c
 * only calls it from a `#if defined(PICOMITE) && defined(rp2350)`
 * guard, so the symbol is never referenced on other targets.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_pin.h"

/* LocalKeyDown / ODCCLR / ODCSET are defined in External.c / External.h;
 * extern brings them into this TU without pulling in all of External.h. */
extern int LocalKeyDown[7];

/* LOCALKEYSCANRATE comes from configuration.h (10 ms). */

const unsigned char localkeymap[10][5]={
    { 1, 2, 3, 4, 5},
    { 6, 7, 8, 9,10},
    {11,12,13,14,15},
    {16,17,18,19,20},
    {21,22,23,24,25},
    {26,27,28,29,30},
    {31,32,33,34,35},
    {36,37,38,39,40},
    {41,42,43,44,45},
    {46,47,48,49,50}
};
const unsigned char asciimapl[51]={
    255,
    '1','q','a','z',255,
    '2','w','s','x',255,
    '3','e','d','c',255,
    '4','r','f','v',' ',
    '5','t','g','b',',',
    '6','y','h','n','.',
    '7','u','j','m',';',
    '8','i','k',0x80,0x81,
    '9','o','l','=',0x82,
    '0','p',8,13,0x83
};
const unsigned char asciimapu[51]={
    255,
    '!','Q','A','Z',255,
    '"','W','S','X',255,
    '#','E','D','C',255,
    '$','R','F','V',' ',
    '%','T','G','B','<',
    '^','Y','H','N','>',
    '&','U','J','M',':',
    '*','I','K',0x80,0x81,
    '(','O','L','+',0x82,
    ')','P',8,13,0x83
};
const unsigned char asciimapfl[51]={
    255,
    0x91,'@','a','z',255,
    0x92,'~','s','x',9,
    0x93,'`','d','c',255,
    0x94,'|','f','v',' ',
    0x95,'{','g','b','\\',
    0x96,'}','h','n','_',
    0x97,'[','j','m','\'',
    0x98,']','k',0x88,0x89,
    0x99,0x9B,'-','/',0x86,
    0x9A,0x9C,127,27,0x87
};
const unsigned char asciimapfu[51]={
    255,
    0xB1,'@','a','z',255,
    0xB2,'~','s','x',9,
    0xB3,'`','d','c',255,
    0xB4,'|','f','v',' ',
    0xB5,'{','g','b','\\',
    0xB6,'}','h','n','_',
    0xB7,'[','j','m','\'',
    0xB8,']','k',0x88,0x89,
    0xB9,0xBB,'-','/',0x86,
    0xBA,0xBC,127,27,0x87
};

static bool checkpressedtime(int count){
    if(!count)return false;
    if(count==1)return true;
    if(count==Option.RepeatStart/LOCALKEYSCANRATE)return true;
    if(count >= (Option.RepeatStart+Option.RepeatRate)/LOCALKEYSCANRATE &&
    (count-Option.RepeatStart/LOCALKEYSCANRATE) % (Option.RepeatRate/LOCALKEYSCANRATE)==0)return true;
    return false;
}

void cmd_keyscan(void){
    static bool shift=false, function=false, s_lock=false, ctrl=false;
    int key=0;
    static unsigned short pressed[51]={0};
    for(int cols = 31; cols < 41; cols++) {
        PinSetBit(PINMAP[cols], ODCCLR);
        for(int rows = 26; rows < 31; rows++) {
            int index=localkeymap[cols-31][rows-26];
            if(PinRead((unsigned char)PINMAP[rows]) == 0) {
                pressed[index]++;
            } else pressed[index]=0;
        }
        PinSetBit(PINMAP[cols], ODCSET);
    }
    function=pressed[15] ? true : false;
    shift=pressed[5] ? true : false;
    ctrl=pressed[10] ? true : false;
    if(function && pressed[5]==1){
        hal_pin_bank_xor_mask((uint64_t)1<<24);
        s_lock^=1;
    }
    LocalKeyDown[6] = (ctrl ? 2: 0) |
                      (function ? 4: 0) |
                      (shift ? 8: 0);

    for(int i=1;i<=50;i++){
        if(checkpressedtime(pressed[i])){
            if(function)key=(s_lock ^ shift) ? asciimapfu[i]: asciimapfl[i];
            else key=(s_lock ^ shift) ? asciimapu[i]: asciimapl[i];
            if(ctrl && (key>='a' && key<='z'))key-=('a'-1);
            if(ctrl && key>='A' && key<='Z')key-=('A'-1);
            if (key == BreakKey) {
                MMAbort = true;
                ConsoleRxBufHead = ConsoleRxBufTail;
            } else {
                ConsoleRxBuf[ConsoleRxBufHead] = key;
                if (ConsoleRxBuf[ConsoleRxBufHead] == keyselect && KeyInterrupt != NULL) {
                    Keycomplete = true;
                } else {
                    ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;
                    if (ConsoleRxBufHead == ConsoleRxBufTail) {
                        ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;
                    }
                }
            }
        }
    }
}
