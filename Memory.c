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
#include "hardware/dma.h"
#include "hardware/pio_instructions.h"
#define ASMMAX 6400 // maximum number of bytes that can be copied or set by assembler routines
#define MAXCPY 3200 // tuned maximum number of bytes to copy using ZCOPY

extern const uint8_t *SavedVarsFlash;
extern const uint8_t *flash_progmemory;
// memory management parameters

// allocate static memory for programs, variables and the heap
// this is simple memory management because DOS has plenty of memory
// unsigned char __attribute__ ((aligned (256))) AllMemory[ALL_MEMORY_SIZE];
#ifdef rp2350
#ifdef PICOMITEVGA
unsigned char __attribute__((aligned(256))) AllMemory[HEAP_MEMORY_SIZE + 256 + 320 * 240 * 2];
unsigned char *FRAMEBUFFER = AllMemory + HEAP_MEMORY_SIZE + 256;
uint32_t framebuffersize = 320 * 240 * 2;
unsigned char *MMHeap = AllMemory;
#else
unsigned char __attribute__((aligned(256))) AllMemory[HEAP_MEMORY_SIZE + 256];
unsigned char *MMHeap = AllMemory;
uint32_t framebuffersize = 0;
unsigned char *FRAMEBUFFER = NULL;
#endif
#else
#ifdef PICOMITEVGA
// Keep heap 4KB aligned, but place it in a .bss.* subsection so it remains
// RAM-only (NOBITS) and does not inflate the flash image size.
unsigned char __attribute__((section(".bss.zheap"), aligned(4096))) Heap[HEAP_MEMORY_SIZE + 256];
unsigned char __attribute__((aligned(256))) video[640 * 480 / 8];
unsigned char *FRAMEBUFFER = video;
uint32_t framebuffersize = 640 * 480 / 8;
unsigned char *MMHeap = Heap;
#else
unsigned char __attribute__((aligned(256))) AllMemory[HEAP_MEMORY_SIZE + 256];
unsigned char *MMHeap = AllMemory;
uint32_t framebuffersize = 0;
unsigned char *FRAMEBUFFER = NULL;
#endif
#endif

uint32_t heap_memory_size = HEAP_MEMORY_SIZE;
#ifdef PICOMITEVGA
#ifdef rp2350
uint16_t *tilefcols; //=(uint16_t *)((uint32_t)FRAMEBUFFER+(MODE1SIZE_S*3));
uint16_t *tilebcols; //=(uint16_t *)((uint32_t)FRAMEBUFFER+(MODE1SIZE_S*3)+(MODE1SIZE_S>>1));
#else
uint16_t __attribute__((aligned(256))) tilefcols[80 * 40];
uint16_t __attribute__((aligned(256))) tilebcols[80 * 40];
#endif
#ifdef HDMI
uint8_t *tilefcols_w;
uint8_t *tilebcols_w;
uint16_t HDMIlines[2][848] = {0};
volatile int X_TILE = 80, Y_TILE = 40;
uint32_t core1stack[128];
volatile int ytileheight = 480 / 12;
#else
uint16_t M_Foreground[16] = {
    0x0000, 0x000F, 0x00f0, 0x00ff, 0x0f00, 0x0f0F, 0x0ff0, 0x0fff, 0xf000, 0xf00F, 0xf0f0, 0xf0ff, 0xff00, 0xff0F, 0xfff0, 0xffff};
uint16_t M_Background[16] = {
    0xffff, 0xfff0, 0xff0f, 0xff00, 0xf0ff, 0xf0f0, 0xf00f, 0xf000, 0x0fff, 0x0ff0, 0x0f0f, 0x0f00, 0x00ff, 0x00f0, 0x000f, 0x0000};
volatile int ytileheight = 16;
#endif
#ifdef rp2350
unsigned char *WriteBuf = AllMemory + HEAP_MEMORY_SIZE + 256;
unsigned char *DisplayBuf = AllMemory + HEAP_MEMORY_SIZE + 256;
unsigned char *LayerBuf = AllMemory + HEAP_MEMORY_SIZE + 256;
unsigned char *FrameBuf = AllMemory + HEAP_MEMORY_SIZE + 256;
unsigned char *SecondLayer = AllMemory + HEAP_MEMORY_SIZE + 256;
unsigned char *SecondFrame = AllMemory + HEAP_MEMORY_SIZE + 256;
#else
unsigned char *WriteBuf = video;
unsigned char *DisplayBuf = video;
unsigned char *LayerBuf = video;
unsigned char *FrameBuf = video;
unsigned char *SecondLayer = video;
unsigned char *SecondFrame = video;
#endif
#endif
#ifdef PICOMITE
unsigned char *WriteBuf = NULL;
unsigned char *LayerBuf = NULL;
unsigned char *FrameBuf = NULL;
#endif
#ifdef GUICONTROLS
struct s_ctrl *Ctrl = NULL; // Allocated from heap top when Option.MaxCtrls > 0
#endif
#ifdef PICOMITEWEB
unsigned char *WriteBuf = NULL;
unsigned char *LayerBuf = NULL;
unsigned char *FrameBuf = NULL;
#endif

unsigned int mmap[HEAP_MEMORY_SIZE / PAGESIZE / PAGESPERWORD] = {0};
#ifdef rp2350
unsigned int psmap[6 * 1024 * 1024 / PAGESIZE / PAGESPERWORD] = {0};
unsigned int SBitsGet(unsigned char *addr);
void SBitsSet(unsigned char *addr, int bits);
#endif
static inline unsigned int MBitsGet(unsigned char *addr);
static inline void MBitsSet(unsigned char *addr, int bits);
char *g_StrTmp[MAXTEMPSTRINGS];          // used to track temporary string space on the heap
char g_StrTmpLocalIndex[MAXTEMPSTRINGS]; // used to track the g_LocalIndex for each temporary string space on the heap

void *getheap(int size);
unsigned int UsedHeap(void);
bool g_TempMemoryIsChanged = false; // used to prevent unnecessary scanning of strtmp[]
int g_StrTmpIndex = 0;              // index to the next unallocated slot in strtmp[]

/***********************************************************************************************************************
 MMBasic commands
************************************************************************************************************************/
/*  @endcond */

/* ============================================================================
 * MEMORY SHARE - Background DMA-to-PIO shared memory between two PicoMites
 * ============================================================================ */
typedef struct
{
    bool active;
    bool is_host;
    PIO pio;
    uint sm;
    uint data_base_gpio;
    uint clk_gpio;
    int data_pins[8]; // physical pin numbers for data (for ExtCfg cleanup)
    int clk_pin_phys; // physical pin number for clock
    uint32_t address;
    uint32_t count;
    uint16_t program_offset;
    uint16_t program_len;
    int bus_width;    // 4 or 8 data pins
    uint dma_data_ch; // DMA data channel
    uint dma_ctrl_ch; // DMA control/retrigger channel
} share_state_t;

static share_state_t share_host_state = {0};
static share_state_t share_client_state = {0};

// Pass address through unchanged — no translation needed.
static inline uint32_t share_dma_address(uint32_t address)
{
    return address;
}

// Startup sync is done in the command handlers using plain GPIO and can wait indefinitely
// (abortable by user). PIO programs are kept stream-only for deterministic timing.

static inline void share_wait_loop(void)
{
    CheckAbort();
    uSec(250);
}

#define SHARE_SYNC_POLL_US 50u
#define SHARE_CLK_DETECT_WINDOW_US 2000u
/* 8-bit sync: host drives 0101 on pins 0-3, client drives 1010 on pins 4-7.
 * 4-bit sync: host drives 01 on pins 0-1, client drives 10 on pins 2-3. */
#define SHARE_HOST_POST_SYNC_MS 5 /* ms host waits after sync before streaming */

// Detect a host that is already streaming (clock toggling).
static bool MIPS16 share_clock_is_toggling(uint clk_gpio)
{
    gpio_set_input_enabled(clk_gpio, true); // RP2350 A9 errata
    uint32_t waited = 0;
    bool last = gpio_get(clk_gpio) != 0;
    int transitions = 0;

    while (waited < SHARE_CLK_DETECT_WINDOW_US)
    {
        bool now = gpio_get(clk_gpio) != 0;
        if (now != last)
        {
            transitions++;
            last = now;
            if (transitions >= 2)
                return true;
        }
        CheckAbort();
        uSec(SHARE_SYNC_POLL_US);
        waited += SHARE_SYNC_POLL_US;
    }
    return false;
}

static void MIPS16 share_prepare_start(PIO pio, uint sm, uint data_base_gpio, uint clk_gpio, int bus_width, uint dma_data_ch, uint dma_ctrl_ch)
{
    // Force SM, DMA and bus pins to a known idle state before every start.
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);

    dma_hw->abort = (1u << dma_data_ch) | (1u << dma_ctrl_ch);
    while (dma_hw->abort)
        tight_loop_contents();
    dma_channel_set_irq0_enabled(dma_data_ch, false);

    for (int i = 0; i < bus_width; i++)
    {
        uint gpio = data_base_gpio + i;
        gpio_init(gpio);
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, false);
        gpio_disable_pulls(gpio);
        gpio_set_input_enabled(gpio, true); // RP2350 A9 errata
    }
    gpio_init(clk_gpio);
    gpio_set_function(clk_gpio, GPIO_FUNC_SIO);
    gpio_set_dir(clk_gpio, false);
    gpio_disable_pulls(clk_gpio);
    gpio_set_input_enabled(clk_gpio, true); // RP2350 A9 errata
}

// Host sync: drive pattern on lower half of data pins, wait for client on upper half.
// 8-bit: host drives 0101 on pins 0-3, reads 1010 on pins 4-7.
// 4-bit: host drives 01 on pins 0-1, reads 10 on pins 2-3.
static void MIPS16 share_host_sync(uint data_base_gpio, uint clk_gpio, int bus_width)
{
    int half = bus_width / 2;
    uint host_pattern = (bus_width == 8) ? 0x05u : 0x01u;   // 0101 or 01
    uint client_pattern = (bus_width == 8) ? 0x0Au : 0x02u; // 1010 or 10
    uint mask = (1u << half) - 1;

    // Clock = output LOW
    gpio_set_dir(clk_gpio, true);
    gpio_put(clk_gpio, 0);

    // Lower half = output, drive host pattern
    for (int i = 0; i < half; i++)
    {
        uint pin = data_base_gpio + i;
        gpio_set_dir(pin, true);
        gpio_put(pin, (host_pattern >> i) & 1);
    }

    // Upper half = input
    for (int i = half; i < bus_width; i++)
    {
        gpio_set_dir(data_base_gpio + i, false);
        gpio_set_input_enabled(data_base_gpio + i, true); // RP2350 A9 errata
    }

    // Block until upper half reads client pattern.
    while (((gpio_get_all() >> (data_base_gpio + half)) & mask) != client_pattern)
        share_wait_loop();

    // Hold sync pattern for 1ms so the client also registers.
    uSec(1000);
}

// Client sync: error if host already streaming; drive pattern on upper half,
// wait for host pattern on lower half.
static void MIPS16 share_client_sync(uint data_base_gpio, uint clk_gpio, int bus_width)
{
    int half = bus_width / 2;
    uint host_pattern = (bus_width == 8) ? 0x05u : 0x01u;   // 0101 or 01
    uint client_pattern = (bus_width == 8) ? 0x0Au : 0x02u; // 1010 or 10
    uint mask = (1u << half) - 1;

    // If clock is already toggling the host is streaming — can't synchronise.
    gpio_set_dir(clk_gpio, false);
    gpio_set_input_enabled(clk_gpio, true); // RP2350 A9 errata
    if (share_clock_is_toggling(clk_gpio))
        error("Host already running - cannot synchronise");

    // Upper half = output, drive client pattern
    for (int i = half; i < bus_width; i++)
    {
        uint pin = data_base_gpio + i;
        gpio_set_dir(pin, true);
        gpio_put(pin, (client_pattern >> (i - half)) & 1);
    }

    // Lower half = input
    for (int i = 0; i < half; i++)
    {
        gpio_set_dir(data_base_gpio + i, false);
        gpio_set_input_enabled(data_base_gpio + i, true); // RP2350 A9 errata
    }

    // Block until lower half reads host pattern.
    while (((gpio_get_all() >> data_base_gpio) & mask) != host_pattern)
        share_wait_loop();

    // Hold sync pattern for 1ms so the host also registers.
    uSec(1000);
}

static void MIPS16 share_stop_internal(share_state_t *st)
{
    if (!st || !st->active)
        return;

    // Always abort DMA channels regardless of active state.
    dma_hw->abort = (1u << st->dma_data_ch) | (1u << st->dma_ctrl_ch);
    while (dma_hw->abort)
        tight_loop_contents();
    dma_channel_set_irq0_enabled(st->dma_data_ch, false);

    // Stop PIO SM and remove program if one was loaded.
    if (st->program_len > 0)
    {
        pio_sm_set_enabled(st->pio, st->sm, false);
        pio_sm_clear_fifos(st->pio, st->sm);
        pio_sm_restart(st->pio, st->sm);

        pio_program_t prog = {
            .instructions = NULL,
            .length = st->program_len,
            .origin = st->program_offset,
        };
        pio_remove_program(st->pio, &prog, st->program_offset);
    }

    // Reset GPIO and release pin reservations if pins were configured.
    if (st->clk_pin_phys > 0)
    {
        int width = st->bus_width ? st->bus_width : 8;
        for (int i = 0; i < width; i++)
        {
            gpio_init(st->data_base_gpio + i);
            gpio_set_function(st->data_base_gpio + i, GPIO_FUNC_SIO);
            gpio_set_dir(st->data_base_gpio + i, false);
            gpio_disable_pulls(st->data_base_gpio + i);
            if (st->data_pins[i])
                ExtCfg(st->data_pins[i], EXT_NOT_CONFIG, 0);
        }
        gpio_init(st->clk_gpio);
        gpio_set_function(st->clk_gpio, GPIO_FUNC_SIO);
        gpio_set_dir(st->clk_gpio, false);
        gpio_disable_pulls(st->clk_gpio);
        ExtCfg(st->clk_pin_phys, EXT_NOT_CONFIG, 0);
    }

    // Zero all state.
    st->active = false;
    st->is_host = false;
    st->pio = 0;
    st->sm = 0;
    st->data_base_gpio = 0;
    st->clk_gpio = 0;
    st->clk_pin_phys = 0;
    for (uint i = 0; i < 8; i++)
        st->data_pins[i] = 0;
    st->address = 0;
    st->count = 0;
    st->program_offset = 0;
    st->program_len = 0;
    st->bus_width = 0;
    st->dma_data_ch = 0;
    st->dma_ctrl_ch = 0;
}

void MemoryShareStop(void)
{
    share_stop_internal(&share_host_state);
    share_stop_internal(&share_client_state);
}

static void MIPS16 cmd_share_host(unsigned char *tp)
{
    getcsargs(&tp, 15);
    if (argc < 11)
        SyntaxError();

    int pior = getint(argv[0], 0, PIOMAX - 1);
    if (PIO0 == false && pior == 0)
        StandardError(3);
    if (PIO1 == false && pior == 1)
        StandardError(4);
#ifdef rp2350
    if (PIO2 == false && pior == 2)
        StandardError(5);
    PIO pio = (pior == 0 ? pio0 : (pior == 1 ? pio1 : pio2));
#else
    PIO pio = (pior == 0 ? pio0 : pio1);
#endif
    int sm = getint(argv[2], 0, 3);
    int data_pin_phys = getpinarg(argv[4]); // physical pin for first data pin
    int clk_pin_phys = getpinarg(argv[6]);  // physical pin for clock
    uint32_t address = getinteger(argv[8]);
    uint32_t dma_address = share_dma_address(address);
    int count = getinteger(argv[10]);
    MMFLOAT clk_div = 10.0; // default clock divider
    if (argc >= 13 && *argv[12])
        clk_div = getnumber(argv[12]);
    int bus_width = 8; // default 8-bit bus
    if (argc >= 15 && *argv[14])
        bus_width = getint(argv[14], 4, 8);

    if (count <= 0 || (count & 3))
        error("Count must be > 0 and multiple of 4");
    if (clk_div < 3.0)
        error("Clock divider must be >= 3");
    if (bus_width != 4 && bus_width != 8)
        error("Bus width must be 4 or 8");

    // Get GPIO numbers from physical pins
    int data_base_gpio = PinDef[data_pin_phys].GPno;
    int clk_gpio = PinDef[clk_pin_phys].GPno;

    if (data_base_gpio + bus_width - 1 > 29)
        error("Need % consecutive GPIOs from data pin", bus_width);
    if (clk_gpio >= data_base_gpio && clk_gpio <= data_base_gpio + bus_width - 1)
        error("Clock pin overlaps data pins");

    // Validate and reserve all data + clock pins
    CheckPin(clk_pin_phys, CP_IGNORE_INUSE);
    int data_pins_phys[8] = {0};
    for (int i = 0; i < bus_width; i++)
    {
        data_pins_phys[i] = codemap(data_base_gpio + i);
        CheckPin(data_pins_phys[i], CP_IGNORE_INUSE);
    }

    // Always stop previous host share (cleans up stale PIO programs, DMA, GPIO).
    share_stop_internal(&share_host_state);

    // Begin from a clean SM/DMA/GPIO state.
    share_prepare_start(pio, (uint)sm, (uint)data_base_gpio, (uint)clk_gpio, bus_width, SHARE_DMA_DATA, SHARE_DMA_CTRL);

    // Build host TX stream program (clock push-pull via side-set value, no startup logic).
    static uint16_t host_program[2];
    host_program[0] = pio_encode_out(pio_pins, bus_width) | pio_encode_sideset(1, 0); // clock LOW
    host_program[1] = pio_encode_nop() | pio_encode_sideset(1, 1);                    // clock HIGH

    pio_program_t prog = {
        .instructions = host_program,
        .length = 2,
        .origin = -1,
    };
    if (!pio_can_add_program(pio, &prog))
        error("No PIO instruction space available");
    uint offset = pio_add_program(pio, &prog);

    // Configure state machine
    pio_sm_config cfg = pio_get_default_sm_config();
    sm_config_set_wrap(&cfg, offset + 0, offset + 1);
    sm_config_set_out_pins(&cfg, data_base_gpio, bus_width);
    sm_config_set_sideset_pins(&cfg, clk_gpio);
    sm_config_set_sideset(&cfg, 1, false, false);    // 1 side-set bit drives clock value
    sm_config_set_out_shift(&cfg, true, true, 32);   // shift right, autopull, 32-bit threshold
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX); // join FIFOs for TX
    sm_config_set_clkdiv(&cfg, clk_div);

    pio_sm_init(pio, sm, offset, &cfg);

    // Configure DMA: data channel transfers memory -> PIO TX FIFO
    dma_channel_config c = dma_channel_get_default_config(SHARE_DMA_DATA);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&c, SHARE_DMA_CTRL);
    dma_channel_configure(SHARE_DMA_DATA, &c,
                          &pio->txf[sm],       // write to PIO TX FIFO
                          (void *)dma_address, // read from shared memory
                          count / 4,           // transfer count (32-bit words)
                          false);              // don't start yet

    // Configure DMA: control channel resets read address and retriggers data channel
    static uint32_t share_read_addr;
    share_read_addr = dma_address;
    dma_channel_config c2 = dma_channel_get_default_config(SHARE_DMA_CTRL);
    channel_config_set_transfer_data_size(&c2, DMA_SIZE_32);
    channel_config_set_read_increment(&c2, false);
    channel_config_set_write_increment(&c2, false);
    channel_config_set_dreq(&c2, 0x3F); // unpaced
    dma_channel_configure(SHARE_DMA_CTRL, &c2,
                          &dma_hw->ch[SHARE_DMA_DATA].al3_read_addr_trig,
                          &share_read_addr, // pointer to the start address constant
                          1,
                          false);

    // Save state for STOP
    share_host_state.active = true;
    share_host_state.is_host = true;
    share_host_state.pio = pio;
    share_host_state.sm = sm;
    share_host_state.data_base_gpio = data_base_gpio;
    share_host_state.clk_gpio = clk_gpio;
    share_host_state.clk_pin_phys = clk_pin_phys;
    for (int i = 0; i < 8; i++)
        share_host_state.data_pins[i] = (i < bus_width) ? data_pins_phys[i] : 0;
    share_host_state.address = address;
    share_host_state.count = count;
    share_host_state.program_offset = offset;
    share_host_state.program_len = 2;
    share_host_state.bus_width = bus_width;
    share_host_state.dma_data_ch = SHARE_DMA_DATA;
    share_host_state.dma_ctrl_ch = SHARE_DMA_CTRL;

    // Reserve all pins
    for (int i = 0; i < bus_width; i++)
        ExtCfg(data_pins_phys[i], EXT_COM_RESERVED, 0);
    ExtCfg(clk_pin_phys, EXT_COM_RESERVED, 0);

    // Sync: block until client responds.
    // Returns after 1ms hold so client has also registered.
    share_host_sync((uint)data_base_gpio, (uint)clk_gpio, bus_width);

    // Wait 5ms for client to switch to PIO receive mode.
    // Clock is LOW (SIO default) — client PIO starts at "wait 1 gpio CLK"
    // and blocks until host PIO drives clock HIGH with first "nop side 1".
    uSec(SHARE_HOST_POST_SYNC_MS * 1000);

    // Switch data pins to PIO outputs.
    for (int i = 0; i < bus_width; i++)
    {
        gpio_disable_pulls(data_base_gpio + i);
        gpio_set_function(data_base_gpio + i,
                          pio == pio0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
    }
    pio_sm_set_consecutive_pindirs(pio, sm, data_base_gpio, bus_width, true);

    // Now hand clock pin to PIO and set as output.
    gpio_set_function(clk_gpio,
                      pio == pio0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
    pio_sm_set_consecutive_pindirs(pio, sm, clk_gpio, 1, true);

    // Start PIO — set clock HIGH first so client "wait 0 gpio CLK" stays
    // blocked until host PIO's first "out pins,8 side 0" pulls it LOW.
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_set_enabled(pio, sm, true);

    // Start DMA — fills TX FIFO, PIO begins clocking data out.
    dma_start_channel_mask(1u << SHARE_DMA_CTRL);
}

static void MIPS16 cmd_share_client(unsigned char *tp)
{
    getcsargs(&tp, 13);
    if (argc < 11)
        SyntaxError();

    int pior = getint(argv[0], 0, PIOMAX - 1);
    if (PIO0 == false && pior == 0)
        StandardError(3);
    if (PIO1 == false && pior == 1)
        StandardError(4);
#ifdef rp2350
    if (PIO2 == false && pior == 2)
        StandardError(5);
    PIO pio = (pior == 0 ? pio0 : (pior == 1 ? pio1 : pio2));
#else
    PIO pio = (pior == 0 ? pio0 : pio1);
#endif
    int sm = getint(argv[2], 0, 3);
    int data_pin_phys = getpinarg(argv[4]); // physical pin for first data pin
    int clk_pin_phys = getpinarg(argv[6]);  // physical pin for clock (input)
    uint32_t address = getinteger(argv[8]);
    uint32_t dma_address = share_dma_address(address);
    int count = getinteger(argv[10]);
    int bus_width = 8; // default 8-bit bus
    if (argc >= 13 && *argv[12])
        bus_width = getint(argv[12], 4, 8);

    if (count <= 0 || (count & 3))
        error("Count must be > 0 and multiple of 4");
    if (bus_width != 4 && bus_width != 8)
        error("Bus width must be 4 or 8");

    // Get GPIO numbers from physical pins
    int data_base_gpio = PinDef[data_pin_phys].GPno;
    int clk_gpio = PinDef[clk_pin_phys].GPno;

    if (data_base_gpio + bus_width - 1 > 29)
        error("Need % consecutive GPIOs from data pin", bus_width);
    if (clk_gpio >= data_base_gpio && clk_gpio <= data_base_gpio + bus_width - 1)
        error("Clock pin overlaps data pins");

    // Validate and reserve all data + clock pins
    CheckPin(clk_pin_phys, CP_IGNORE_INUSE);
    int data_pins_phys[8] = {0};
    for (int i = 0; i < bus_width; i++)
    {
        data_pins_phys[i] = codemap(data_base_gpio + i);
        CheckPin(data_pins_phys[i], CP_IGNORE_INUSE);
    }

    // Always stop previous client share (cleans up stale PIO programs, DMA, GPIO).
    share_stop_internal(&share_client_state);

    // Begin from a clean SM/DMA/GPIO state.
    share_prepare_start(pio, (uint)sm, (uint)data_base_gpio, (uint)clk_gpio, bus_width, PIO_RX_DMA, PIO_RX_DMA2);

    // Build client RX stream program: autopush after 32 bits.
    // .wrap_target
    //     wait 1 gpio CLK     ; [0] Wait for clock HIGH (data valid)
    //     in pins, N           ; [1] Sample nibble/byte, autopush after 32 bits
    //     wait 0 gpio CLK     ; [2] Wait for clock LOW (host loading new data)
    // .wrap
    static uint16_t client_program[3];
    client_program[0] = pio_encode_wait_gpio(true, clk_gpio);
    client_program[1] = pio_encode_in(pio_pins, bus_width);
    client_program[2] = pio_encode_wait_gpio(false, clk_gpio);

    pio_program_t prog = {
        .instructions = client_program,
        .length = 3,
        .origin = -1,
    };
    if (!pio_can_add_program(pio, &prog))
        error("No PIO instruction space available");
    uint offset = pio_add_program(pio, &prog);

    // Configure state machine
    pio_sm_config cfg = pio_get_default_sm_config();
    sm_config_set_wrap(&cfg, offset + 0, offset + 2);
    sm_config_set_in_pins(&cfg, data_base_gpio);
    sm_config_set_in_shift(&cfg, true, true, 32);    // shift right, autopush at 32 bits
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_RX); // join FIFOs for RX
    sm_config_set_clkdiv(&cfg, 1.0);                 // run at full speed to catch clock edges

    pio_sm_init(pio, sm, offset, &cfg);

    // Configure DMA: data channel transfers PIO RX FIFO -> memory
    dma_channel_config c = dma_channel_get_default_config(PIO_RX_DMA);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false)); // RX FIFO not empty
    channel_config_set_chain_to(&c, PIO_RX_DMA2);
    dma_channel_configure(PIO_RX_DMA, &c,
                          (void *)dma_address, // write to shared memory
                          &pio->rxf[sm],       // read from PIO RX FIFO
                          count / 4,           // transfer count (32-bit words)
                          false);              // don't start yet

    // Configure DMA: control channel resets write address and retriggers data channel
    static uint32_t share_write_addr;
    share_write_addr = dma_address;
    dma_channel_config c2 = dma_channel_get_default_config(PIO_RX_DMA2);
    channel_config_set_transfer_data_size(&c2, DMA_SIZE_32);
    channel_config_set_read_increment(&c2, false);
    channel_config_set_write_increment(&c2, false);
    channel_config_set_dreq(&c2, 0x3F); // unpaced
    dma_channel_configure(PIO_RX_DMA2, &c2,
                          &dma_hw->ch[PIO_RX_DMA].al2_write_addr_trig,
                          &share_write_addr, // pointer to the start address constant
                          1,
                          false);

    // Save state for STOP
    share_client_state.active = true;
    share_client_state.is_host = false;
    share_client_state.pio = pio;
    share_client_state.sm = sm;
    share_client_state.data_base_gpio = data_base_gpio;
    share_client_state.clk_gpio = clk_gpio;
    share_client_state.clk_pin_phys = clk_pin_phys;
    for (int i = 0; i < 8; i++)
        share_client_state.data_pins[i] = (i < bus_width) ? data_pins_phys[i] : 0;
    share_client_state.address = address;
    share_client_state.count = count;
    share_client_state.program_offset = offset;
    share_client_state.program_len = 3;
    share_client_state.bus_width = bus_width;
    share_client_state.dma_data_ch = SHARE_DMA_DATA;
    share_client_state.dma_ctrl_ch = SHARE_DMA_CTRL;

    // Reserve all pins
    for (int i = 0; i < bus_width; i++)
        ExtCfg(data_pins_phys[i], EXT_COM_RESERVED, 0);
    ExtCfg(clk_pin_phys, EXT_COM_RESERVED, 0);

    // Sync: block until host responds.
    // Returns after 1ms hold so host has also registered.
    share_client_sync((uint)data_base_gpio, (uint)clk_gpio, bus_width);

    // Switch ALL data pins and clock to PIO as inputs.
    for (int i = 0; i < bus_width; i++)
    {
        gpio_disable_pulls(data_base_gpio + i);
        gpio_set_function(data_base_gpio + i,
                          pio == pio0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
        gpio_set_input_enabled(data_base_gpio + i, true); // RP2350 A9 errata
    }
    gpio_disable_pulls(clk_gpio);
    gpio_set_function(clk_gpio,
                      pio == pio0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1);
    gpio_set_input_enabled(clk_gpio, true); // RP2350 A9 errata

    // Start PIO (blocks at wait 0 gpio CLK — no clock yet, host hasn't started).
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_set_enabled(pio, sm, true);

    // Start DMA (blocks on RX FIFO empty — no data until host starts clocking).
    dma_start_channel_mask(1u << PIO_RX_DMA2);
}

void MIPS16 cmd_memory(void)
{
    unsigned char *p, *tp;
    tp = checkstring(cmdline, (unsigned char *)"SHARE HOST");
    if (tp)
    {
        cmd_share_host(tp);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"SHARE CLIENT");
    if (tp)
    {
        cmd_share_client(tp);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"SHARE STOP HOST");
    if (tp)
    {
        share_stop_internal(&share_host_state);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"SHARE STOP CLIENT");
    if (tp)
    {
        share_stop_internal(&share_client_state);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"SHARE STOP");
    if (tp)
    {
        MemoryShareStop();
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"PACK");
    if (tp)
    {
        getcsargs(&tp, 7);
        if (argc != 7)
            SyntaxError();
        ;
        int i, n = getinteger(argv[4]);
        if (n <= 0)
            return;
        int size = getint(argv[6], 1, 32);
        if (!(size == 1 || size == 4 || size == 8 || size == 16 || size == 32))
            error((char *)"Invalid size");
        int sourcesize, destinationsize;
        void *top = NULL;
        uint64_t *from = NULL;
        if (CheckEmpty((char *)argv[0]))
        {
            sourcesize = parseintegerarray(argv[0], (int64_t **)&from, 1, 1, NULL, false, NULL);
            if (sourcesize < n)
                error("Source array too small");
        }
        else
            from = (uint64_t *)GetPokeAddr(argv[0]);
        if (CheckEmpty((char *)argv[2]))
        {
            destinationsize = parseintegerarray(argv[2], (int64_t **)&top, 2, 1, NULL, true, NULL);
            if (destinationsize * 64 / size < n)
                StandardError(23);
        }
        else
            top = (void *)GetPokeAddr(argv[2]);
        if ((uint32_t)from % 8)
            error("Address not divisible by 8");
        if (size == 1)
        {
            uint8_t *to = (uint8_t *)top;
            for (i = 0; i < n; i++)
            {
                int s = i % 8;
                if (s == 0)
                    *to = 0;
                *to |= ((*from++) & 0x1) << s;
                if (s == 7)
                    to++;
            }
        }
        else if (size == 4)
        {
            uint8_t *to = (uint8_t *)top;
            for (i = 0; i < n; i++)
            {
                if ((i & 1) == 0)
                {
                    *to = (*from++) & 0xF;
                }
                else
                {
                    *to |= ((*from++) & 0xF) << 4;
                    to++;
                }
            }
        }
        else if (size == 8)
        {
            uint8_t *to = (uint8_t *)top;
            while (n--)
            {
                *to++ = (uint8_t)*from++;
            }
        }
        else if (size == 16)
        {
            uint16_t *to = (uint16_t *)top;
            if ((uint32_t)to % 2)
                error("Address not divisible by 2");
            while (n--)
            {
                *to++ = (uint16_t)*from++;
            }
        }
        else if (size == 32)
        {
            uint32_t *to = (uint32_t *)top;
            if ((uint32_t)to % 4)
                error("Address not divisible by 4");
            while (n--)
            {
                *to++ = (uint32_t)*from++;
            }
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"PRINT");
    if (tp)
    {
        char *fromp = NULL;
        int sourcesize;
        int64_t *aint;
        getcsargs(&tp, 5);
        if (!(argc == 5))
            SyntaxError();
        ;
        if (*argv[0] == '#')
            argv[0]++;
        int fnbr = getint(argv[0], 1, MAXOPENFILES); // get the number
        int n = getinteger(argv[2]);
        if (CheckEmpty((char *)argv[4]))
        {
            sourcesize = parseintegerarray(argv[4], &aint, 3, 1, NULL, false, NULL);
            if (sourcesize * 8 < n)
                error("Source array too small");
            fromp = (char *)aint;
        }
        else
        {
            fromp = (char *)GetPeekAddr(argv[4]);
        }
        if (FileTable[fnbr].com > MAXCOMPORTS)
        {
            FilePutData(fromp, fnbr, n);
        }
        else
            error("File % not open", fnbr);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"INPUT");
    if (tp)
    {
        char *fromp = NULL;
        int sourcesize;
        int64_t *aint;
        getcsargs(&tp, 5);
        if (!(argc == 5))
            SyntaxError();
        ;
        if (*argv[0] == '#')
            argv[0]++;
        int fnbr = getint(argv[0], 1, MAXOPENFILES); // get the number
        int n = getinteger(argv[2]);
        if (CheckEmpty((char *)argv[4]))
        {
            sourcesize = parseintegerarray(argv[4], &aint, 3, 1, NULL, false, NULL);
            if (sourcesize * 8 < n)
                error("Source array too small");
            fromp = (char *)aint;
        }
        else
        {
            fromp = (char *)GetPokeAddr(argv[4]);
        }
        if (FileTable[fnbr].com > MAXCOMPORTS)
        {
            unsigned int bytesRead = 0;
            FileGetData(fnbr, fromp, n, &bytesRead);
            if ((int)bytesRead != n)
                error("End of file");
        }
        else
            error("File % not open", fnbr);
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"UNPACK");
    if (tp)
    {
        getcsargs(&tp, 7);
        if (argc != 7)
            SyntaxError();
        ;
        int i, n = getinteger(argv[4]);
        if (n <= 0)
            return;
        int size = getint(argv[6], 1, 32);
        if (!(size == 1 || size == 4 || size == 8 || size == 16 || size == 32))
            error((char *)"Invalid size");
        int sourcesize, destinationsize;
        uint64_t *to = NULL;
        void *fromp = NULL;
        if (CheckEmpty((char *)argv[0]))
        {
            sourcesize = parseintegerarray(argv[0], (int64_t **)&fromp, 1, 1, NULL, false, NULL);
            if (sourcesize * 64 / size < n)
                error("Source array too small");
        }
        else
        {
            fromp = (void *)GetPokeAddr(argv[0]);
        }
        if (CheckEmpty((char *)argv[2]))
        {
            destinationsize = parseintegerarray(argv[2], (int64_t **)&to, 2, 1, NULL, true, NULL);
            if (n > destinationsize)
                StandardError(23);
        }
        else
            to = (uint64_t *)GetPokeAddr(argv[2]);
        if ((uint32_t)to % 8)
            error("Address not divisible by 8");
        if (size == 1)
        {
            uint8_t *from = (uint8_t *)fromp;
            for (i = 0; i < n; i++)
            {
                int s = i % 8;
                *to++ = ((*from & (1 << s)) ? 1 : 0);
                if (s == 7)
                    from++;
            }
        }
        else if (size == 4)
        {
            uint8_t *from = (uint8_t *)fromp;
            for (i = 0; i < n; i++)
            {
                if ((i & 1) == 0)
                {
                    *to++ = (*from) & 0xF;
                }
                else
                {
                    *to++ = (*from) >> 4;
                    from++;
                }
            }
        }
        else if (size == 8)
        {
            uint8_t *from = (uint8_t *)fromp;
            while (n--)
            {
                *to++ = (uint64_t)*from++;
            }
        }
        else if (size == 16)
        {
            uint16_t *from = (uint16_t *)fromp;
            if ((uint32_t)from % 2)
                error("Address not divisible by 2");
            while (n--)
            {
                *to++ = (uint64_t)*from++;
            }
        }
        else if (size == 32)
        {
            uint32_t *from = (uint32_t *)fromp;
            if ((uint32_t)from % 4)
                error("Address not divisible by 4");
            while (n--)
            {
                *to++ = (uint64_t)*from++;
            }
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"COPY");
    if (tp)
    {
        if ((p = checkstring(tp, (unsigned char *)"INTEGER")))
        {
            int stepin = 1, stepout = 1;
            getcsargs(&p, 9);
            if (argc < 5)
                SyntaxError();
            ;
            int n = getinteger(argv[4]);
            if (n <= 0)
                return;
            uint64_t *from = (uint64_t *)GetPokeAddr(argv[0]);
            uint64_t *to = (uint64_t *)GetPokeAddr(argv[2]);
            if ((uint32_t)from % 8)
                error("Address not divisible by 8");
            if ((uint32_t)to % 8)
                error("Address not divisible by 8");
            if (argc >= 7 && *argv[6])
                stepin = getint(argv[6], 0, 0xFFFF);
            if (argc == 9)
                stepout = getint(argv[8], 0, 0xFFFF);
            if (stepin == 1 && stepout == 1)
                memmove(to, from, n * 8);
            else
            {
                if (from < to)
                {
                    from += (n - 1) * stepin;
                    to += (n - 1) * stepout;
                    while (n--)
                    {
                        *to = *from;
                        to -= stepout;
                        from -= stepin;
                    }
                }
                else
                {
                    while (n--)
                    {
                        *to = *from;
                        to += stepout;
                        from += stepin;
                    }
                }
            }
            return;
        }
        if ((p = checkstring(tp, (unsigned char *)"FLOAT")))
        {
            int stepin = 1, stepout = 1;
            getcsargs(&p, 9); // assume byte
            if (argc < 5)
                SyntaxError();
            ;
            int n = getinteger(argv[4]);
            if (n <= 0)
                return;
            MMFLOAT *from = (MMFLOAT *)GetPokeAddr(argv[0]);
            MMFLOAT *to = (MMFLOAT *)GetPokeAddr(argv[2]);
            if ((uint32_t)from % 8)
                error("Address not divisible by 8");
            if ((uint32_t)to % 8)
                error("Address not divisible by 8");
            if (argc >= 7 && *argv[6])
                stepin = getint(argv[6], 0, 0xFFFF);
            if (argc == 9)
                stepout = getint(argv[8], 0, 0xFFFF);
            if (n <= 0)
                return;
            if (stepin == 1 && stepout == 1)
                memmove(to, from, n * 8);
            else
            {
                if (from < to)
                {
                    from += (n - 1) * stepin;
                    to += (n - 1) * stepout;
                    while (n--)
                    {
                        *to = *from;
                        to -= stepout;
                        from -= stepin;
                    }
                }
                else
                {
                    while (n--)
                    {
                        *to = *from;
                        to += stepout;
                        from += stepin;
                    }
                }
            }
            return;
        }
        getcsargs(&tp, 9); // assume byte
        if (argc < 5)
            SyntaxError();
        ;
        int stepin = 1, stepout = 1;
        char *from = (char *)GetPeekAddr(argv[0]);
        char *to = (char *)GetPokeAddr(argv[2]);
        int n = getinteger(argv[4]);
        if (argc >= 7 && *argv[6])
            stepin = getint(argv[6], 0, 0xFFFF);
        if (argc == 9)
            stepout = getint(argv[8], 0, 0xFFFF);
        if (n <= 0)
            return;
        if (stepin == 1 && stepout == 1)
            memmove(to, from, n);
        else
        {
            if (from < to)
            {
                from += (n - 1) * stepin;
                to += (n - 1) * stepout;
                while (n--)
                {
                    *to = *from;
                    to -= stepout;
                    from -= stepin;
                }
            }
            else
            {
                while (n--)
                {
                    *to = *from;
                    to += stepout;
                    from += stepin;
                }
            }
        }
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"SET");
    if (tp)
    {
        unsigned char *p;
        if ((p = checkstring(tp, (unsigned char *)"BYTE")))
        {
            getcsargs(&p, 5); // assume byte
            if (argc != 5)
                SyntaxError();
            ;
            char *to = (char *)GetPokeAddr(argv[0]);
            int val = getint(argv[2], 0, 255);
            int n = getinteger(argv[4]);
            if (n <= 0)
                return;
            memset(to, val, n);
            return;
        }
        if ((p = checkstring(tp, (unsigned char *)"SHORT")))
        {
            getcsargs(&p, 5); // assume byte
            if (argc != 5)
                SyntaxError();
            ;
            short *to = (short *)GetPokeAddr(argv[0]);
            if ((uint32_t)to % 2)
                error("Address not divisible by 2");
            short *q = to;
            short data = getint(argv[2], 0, 65535);
            int n = getinteger(argv[4]);
            if (n <= 0)
                return;
            while (n > 0)
            {
                *q++ = data;
                n--;
            }
            return;
        }
        if ((p = checkstring(tp, (unsigned char *)"WORD")))
        {
            getcsargs(&p, 5); // assume byte
            if (argc != 5)
                SyntaxError();
            ;
            unsigned int *to = (unsigned int *)GetPokeAddr(argv[0]);
            if ((uint32_t)to % 4)
                error("Address not divisible by 4");
            unsigned int *q = to;
            unsigned int data = getint(argv[2], 0, 0xFFFFFFFF);
            int n = getinteger(argv[4]);
            if (n <= 0)
                return;
            while (n > 0)
            {
                *q++ = data;
                n--;
            }
            return;
        }
        if ((p = checkstring(tp, (unsigned char *)"INTEGER")))
        {
            int stepin = 1;
            getcsargs(&p, 7);
            if (argc < 5)
                SyntaxError();
            ;
            uint64_t *to = (uint64_t *)GetPokeAddr(argv[0]);
            if ((uint32_t)to % 8)
                error("Address not divisible by 8");
            int64_t data;
            data = getinteger(argv[2]);
            int n = getinteger(argv[4]);
            if (argc == 7)
                stepin = getint(argv[6], 0, 0xFFFF);
            if (n <= 0)
                return;
            if (stepin == 1)
                while (n--)
                    *to++ = data;
            else
            {
                while (n--)
                {
                    *to = data;
                    to += stepin;
                }
            }
            return;
        }
        if ((p = checkstring(tp, (unsigned char *)"FLOAT")))
        {
            int stepin = 1;
            getcsargs(&p, 7); // assume byte
            if (argc < 5)
                SyntaxError();
            ;
            MMFLOAT *to = (MMFLOAT *)GetPokeAddr(argv[0]);
            if ((uint32_t)to % 8)
                error("Address not divisible by 8");
            MMFLOAT data;
            data = getnumber(argv[2]);
            int n = getinteger(argv[4]);
            if (argc == 7)
                stepin = getint(argv[6], 0, 0xFFFF);
            if (n <= 0)
                return;
            if (stepin == 1)
                while (n--)
                    *to++ = data;
            else
            {
                while (n--)
                {
                    *to = data;
                    to += stepin;
                }
            }
            return;
        }
        getcsargs(&tp, 5); // assume byte
        if (argc != 5)
            SyntaxError();
        ;
        char *to = (char *)GetPokeAddr(argv[0]);
        int val = getint(argv[2], 0, 255);
        int n = getinteger(argv[4]);
        if (n <= 0)
            return;
        memset(to, val, n);
        return;
    }
    // MEMORY Usage
    int i, j, var, nbr, vsize, VarCnt;
    int ProgramSize, ProgramPercent, VarSize, VarPercent, GeneralSize, GeneralPercent, SavedVarSize, SavedVarSizeK, SavedVarPercent, SavedVarCnt;
    int CFunctSize, CFunctSizeK, CFunctNbr, CFunctPercent, FontSize, FontSizeK, FontNbr, FontPercent, LibrarySizeK, LibraryPercent, LibraryMaxK;
    unsigned int CurrentRAM, *pint;

    CurrentRAM = heap_memory_size + MAXVARS * sizeof(struct s_vartbl);
#ifdef rp2350
    CurrentRAM += PSRAMsize;
#endif
    // calculate the space allocated to variables on the heap
    for (i = VarCnt = vsize = var = 0; var < MAXVARS; var++)
    {
        if (g_vartbl[var].type == T_NOTYPE)
            continue;
        VarCnt++;
        vsize += sizeof(struct s_vartbl);
        if (g_vartbl[var].val.s == NULL)
            continue;
        if (g_vartbl[var].type & T_PTR)
            continue;
        nbr = g_vartbl[var].dims[0] + 1 - g_OptionBase;
        if (g_vartbl[var].dims[0])
        {
            for (j = 1; j < MAXDIM && g_vartbl[var].dims[j]; j++)
                nbr *= (g_vartbl[var].dims[j] + 1 - g_OptionBase);
            if (g_vartbl[var].type & T_NBR)
                i += MRoundUp(nbr * sizeof(MMFLOAT));
            else if (g_vartbl[var].type & T_INT)
                i += MRoundUp(nbr * sizeof(long long int));
            else
                i += MRoundUp(nbr * (g_vartbl[var].size + 1));
        }
        else if (g_vartbl[var].type & T_STR)
        {
            if (g_vartbl[var].val.s != (void *)&g_vartbl[var].dims[1])
                i += STRINGSIZE;
        }
    }
    VarSize = (vsize + i + 512) / 1024; // this is the memory allocated to variables
    VarPercent = ((vsize + i) * 100) / CurrentRAM;
    if (VarCnt && VarSize == 0)
        VarPercent = VarSize = 1; // adjust if it is zero and we have some variables
    i = UsedHeap() - i;
    if (i < 0)
        i = 0;
    GeneralSize = (i + 512) / 1024;
    GeneralPercent = (i * 100) / CurrentRAM;

    // count the space used by saved variables (in flash)
    p = (unsigned char *)SavedVarsFlash;
    SavedVarCnt = 0;
    while (!(*p == 0 || *p == 0xff))
    {
        unsigned char type, array;
        SavedVarCnt++;
        type = *p++;
        array = type & 0x80;
        type &= 0x7f; // set array to true if it is an array
        p += strlen((char *)p) + 1;
        if (array)
            p += (p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24) + 4;
        else
        {
            if (type & T_NBR)
                p += sizeof(MMFLOAT);
            else if (type & T_INT)
                p += sizeof(long long int);
            else
                p += *p + 1;
        }
    }
    SavedVarSize = p - (SavedVarsFlash);
    SavedVarSizeK = (SavedVarSize + 512) / 1024;
    SavedVarPercent = (SavedVarSize * 100) / (/*MAX_PROG_SIZE +*/ SAVEDVARS_FLASH_SIZE);
    if (SavedVarCnt && SavedVarSizeK == 0)
        SavedVarPercent = SavedVarSizeK = 1; // adjust if it is zero and we have some variables

    // count the space used by CFunctions, CSubs and fonts
    CFunctSize = CFunctNbr = FontSize = FontNbr = 0;
    pint = (unsigned int *)CFunctionFlash;
    while (*pint != 0xffffffff)
    {
        // if(*pint < FONT_TABLE_SIZE) {
        if (*pint >> 31)
        {
            pint++;
            FontNbr++;
            FontSize += *pint + 8;
        }
        else
        {
            pint++;
            CFunctNbr++;
            CFunctSize += *pint + 8;
        }
        pint += (*pint + 4) / sizeof(unsigned int);
    }
    CFunctPercent = (CFunctSize * 100) / (MAX_PROG_SIZE + SAVEDVARS_FLASH_SIZE);
    CFunctSizeK = (CFunctSize + 512) / 1024;
    if (CFunctNbr && CFunctSizeK == 0)
        CFunctPercent = CFunctSizeK = 1; // adjust if it is zero and we have some functions
    FontPercent = (FontSize * 100) / (MAX_PROG_SIZE /*+ SAVEDVARS_FLASH_SIZE*/);
    FontSizeK = (FontSize + 512) / 1024;
    if (FontNbr && FontSizeK == 0)
        FontPercent = FontSizeK = 1; // adjust if it is zero and we have some functions

    // count the number of lines in the program
    p = ProgMemory;
    i = 0;
    while (*p != 0xff)
    { // skip if program memory is erased
        if (*p == 0)
            p++; // if it is at the end of an element skip the zero marker
        if (*p == 0)
            break; // end of the program or module
        if (*p == T_NEWLINE)
        {
            i++; // count the line
            p++; // skip over the newline token
        }
        if (*p == T_LINENBR)
            p += 3; // skip over the line number
        skipspace(p);
        if (p[0] == T_LABEL)
            p += p[1] + 2; // skip over the label
        while (*p)
            p++; // look for the zero marking the start of an element
    }
    ProgramSize = ((p - ProgMemory) + 512) / 1024;
    ProgramPercent = ((p - ProgMemory) * 100) / (MAX_PROG_SIZE /*+ SAVEDVARS_FLASH_SIZE*/);
    if (ProgramPercent > 100)
        ProgramPercent = 100;
    if (i && ProgramSize == 0)
        ProgramPercent = ProgramSize = 1; // adjust if it is zero and we have some lines

    MMPrintString("Program:\r\n");
    IntToStrPad((char *)inpbuf, ProgramSize, ' ', 4, 10);
    strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), ProgramPercent, ' ', 2, 10);
    strcat((char *)inpbuf, "%) Program (");
    IntToStr((char *)inpbuf + strlen((char *)inpbuf), i, 10);
    strcat((char *)inpbuf, " lines)\r\n");
    MMPrintString((char *)inpbuf);

    if (CFunctNbr)
    {
        IntToStrPad((char *)inpbuf, CFunctSizeK, ' ', 4, 10);
        strcat((char *)inpbuf, "K (");
        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), CFunctPercent, ' ', 2, 10);
        strcat((char *)inpbuf, "%) ");
        MMPrintString((char *)inpbuf);
        IntToStr((char *)inpbuf, CFunctNbr, 10);
        strcat((char *)inpbuf, " Embedded C Routine");
        strcat((char *)inpbuf, CFunctNbr == 1 ? "\r\n" : "s\r\n");
        MMPrintString((char *)inpbuf);
    }

    if (FontNbr)
    {
        IntToStrPad((char *)inpbuf, FontSizeK, ' ', 4, 10);
        strcat((char *)inpbuf, "K (");
        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), FontPercent, ' ', 2, 10);
        strcat((char *)inpbuf, "%) ");
        MMPrintString((char *)inpbuf);
        IntToStr((char *)inpbuf, FontNbr, 10);
        strcat((char *)inpbuf, "  Embedded Font");
        strcat((char *)inpbuf, FontNbr == 1 ? "\r\n" : "s\r\n");
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

    IntToStrPad((char *)inpbuf, ((MAX_PROG_SIZE /* + SAVEDVARS_FLASH_SIZE*/) + 512) / 1024 - ProgramSize - CFunctSizeK - FontSizeK /*- SavedVarSizeK - LibrarySizeK*/, ' ', 4, 10);
    strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), 100 - ProgramPercent - CFunctPercent - FontPercent /*- SavedVarPercent - LibraryPercent*/, ' ', 2, 10);
    strcat((char *)inpbuf, "%) Free\r\n");
    MMPrintString((char *)inpbuf);

    // Get the library size
    LibrarySizeK = LibraryPercent = 0;
    LibraryMaxK = MAX_PROG_SIZE / 1024;
    if (Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE)
    {
        i = 0;
        // first count the normal program code residing in the Library
        p = LibMemory;
        while (!(p[0] == 0 && p[1] == 0))
        {
            p++;
            i++;
        }
        while (*p == 0)
        { // the end of the program can have multiple zeros -count them
            p++;
            i++;
        }
        p++;
        i++; // get 0xFF that ends the program and count it
        while ((unsigned int)p & 0b11)
        { // count to the next word boundary
            p++;
            i++;
        }

        // Now add the binary used for CSUB and Fonts
        if (CFunctionLibrary != NULL)
        {
            j = 0;
            pint = (unsigned int *)CFunctionLibrary;
            while (*pint != 0xffffffff)
            {
                pint++;                                     // step over the address or Font No.
                j += *pint + 8;                             // Read the size
                pint += (*pint + 4) / sizeof(unsigned int); // set pointer to start of next CSUB/Font
            }
            i = i + j;
        }

        LibrarySizeK = (i + 512) / 1024;
        LibraryPercent = (LibrarySizeK * 100) / LibraryMaxK;
        if (LibrarySizeK == 0)
            LibrarySizeK = 1; // adjust if it is zero and we have any library
        if (LibraryPercent == 0)
            LibraryPercent = 1; // adjust if it is zero and we have any library

        MMPrintString("\r\nLibrary:\r\n");

        IntToStrPad((char *)inpbuf, LibrarySizeK, ' ', 4, 10);
        strcat((char *)inpbuf, "K (");
        // IntToStrPad(inpbuf, (128*1024  + 512)/1024  - LibrarySizeK, ' ', 4, 10); strcat((char *)inpbuf, "K (");
        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), LibraryPercent, ' ', 2, 10);
        strcat((char *)inpbuf, "%) ");
        strcat((char *)inpbuf, "Library\r\n");
        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), LibraryMaxK - LibrarySizeK, ' ', 4, 10);
        strcat((char *)inpbuf, "K (");
        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), 100 - LibraryPercent, ' ', 2, 10);
        strcat((char *)inpbuf, "%) Free\r\n");
        MMPrintString((char *)inpbuf);
    }

    MMPrintString("\r\nSaved Variables:\r\n");
    if (SavedVarCnt)
    {
        IntToStrPad((char *)inpbuf, SavedVarSizeK, ' ', 4, 10);
        strcat((char *)inpbuf, "K (");
        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), SavedVarPercent, ' ', 2, 10);
        strcat((char *)inpbuf, "%)");
        IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), SavedVarCnt, ' ', 2, 10);
        strcat((char *)inpbuf, " Saved Variable");
        strcat((char *)inpbuf, SavedVarCnt == 1 ? " (" : "s (");
        IntToStr((char *)inpbuf + strlen((char *)inpbuf), SavedVarSize, 10);
        strcat((char *)inpbuf, " bytes)\r\n");
        MMPrintString((char *)inpbuf);
    }
    IntToStrPad((char *)inpbuf, ((SAVEDVARS_FLASH_SIZE) + 512) / 1024 - SavedVarSizeK, ' ', 4, 10);
    strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), 100 - SavedVarPercent, ' ', 2, 10);
    strcat((char *)inpbuf, "%) Free\r\n");
    MMPrintString((char *)inpbuf);

    MMPrintString("\r\nRAM:\r\n");
    IntToStrPad((char *)inpbuf, VarSize, ' ', 4, 10);
    strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), VarPercent, ' ', 2, 10);
    strcat((char *)inpbuf, "%) ");
    IntToStr((char *)inpbuf + strlen((char *)inpbuf), VarCnt, 10);
    strcat((char *)inpbuf, " Variable");
    strcat((char *)inpbuf, VarCnt == 1 ? "\r\n" : "s\r\n");
    MMPrintString((char *)inpbuf);

    IntToStrPad((char *)inpbuf, GeneralSize, ' ', 4, 10);
    strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), GeneralPercent, ' ', 2, 10);
    strcat((char *)inpbuf, "%) General\r\n");
    MMPrintString((char *)inpbuf);

    IntToStrPad((char *)inpbuf, (CurrentRAM + 512) / 1024 - VarSize - GeneralSize, ' ', 4, 10);
    strcat((char *)inpbuf, "K (");
    IntToStrPad((char *)inpbuf + strlen((char *)inpbuf), 100 - VarPercent - GeneralPercent, ' ', 2, 10);
    strcat((char *)inpbuf, "%) Free\r\n");
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

void m_alloc(int type)
{
    switch (type)
    {

    case M_PROG:
#ifdef rp2350
        if (PSRAMsize)
            memset((uint8_t *)PSRAMbase, 0, PSRAMsize);
#endif
    case M_LIMITED: // this is called initially in InitBasic() to set the base pointer for program memory
        // everytime the program size is adjusted up or down this must be called to check for memory overflow
        ProgMemory = (uint8_t *)flash_progmemory;
        memset(MMHeap, 0, heap_memory_size);
        break;

    case M_VAR: // this must be called to initialises the variable memory pointer
        // everytime the variable table is increased this must be called to verify that enough memory is free
        memset(g_vartbl, 0, MAXVARS * sizeof(struct s_vartbl));
        break;
    }
}

// get some memory from the heap
// void *GetMemory(size_t  msize) {
//    return getheap(msize);                                          // allocate space
//}

// Get a temporary buffer of any size
// The space only lasts for the length of the command.
// A pointer to the space is saved in an array so that it can be returned at the end of the command
void __not_in_flash_func (*GetTempMemory)(int NbrBytes)
{
    if (g_StrTmpIndex >= MAXTEMPSTRINGS)
        StandardError(29);
    g_StrTmpLocalIndex[g_StrTmpIndex] = g_LocalIndex;
    g_StrTmp[g_StrTmpIndex] = GetSystemMemory(NbrBytes);
    g_TempMemoryIsChanged = true;
    return (void *)g_StrTmp[g_StrTmpIndex++];
}
#if defined(rp2350)
void __not_in_flash_func (*GetTempStrMemory)(void)
{
    if (g_StrTmpIndex >= MAXTEMPSTRINGS)
        StandardError(29);
    g_StrTmpLocalIndex[g_StrTmpIndex] = g_LocalIndex;
    g_StrTmp[g_StrTmpIndex] = GetSystemMemory(STRINGSIZE);
    g_TempMemoryIsChanged = true;
    return (void *)g_StrTmp[g_StrTmpIndex++];
}

void __not_in_flash_func (*GetTempMainMemory)(int NbrBytes)
{
    if (g_StrTmpIndex >= MAXTEMPSTRINGS)
        StandardError(29);
    g_StrTmpLocalIndex[g_StrTmpIndex] = g_LocalIndex;
    g_StrTmp[g_StrTmpIndex] = GetMemory(NbrBytes);
    g_TempMemoryIsChanged = true;
    return (void *)g_StrTmp[g_StrTmpIndex++];
}
#endif

// get a temporary string buffer
// this is used by many BASIC string functions.  The space only lasts for the length of the command.
// void *GetTempStrMemory(void) {
//    return GetTempStrMemory();
//}

// clear any temporary string spaces (these last for just the life of a command) and return the memory to the heap
// this will not clear memory allocated with a local index less than g_LocalIndex, sub/funs will increment g_LocalIndex
// and this prevents the automatic use of ClearTempMemory from clearing memory allocated before calling the sub/fun
void __not_in_flash_func(ClearTempMemory)(void)
{
    while (g_StrTmpIndex > 0)
    {
        if (g_StrTmpLocalIndex[g_StrTmpIndex - 1] >= g_LocalIndex)
        {
            g_StrTmpIndex--;
            FreeMemory((void *)g_StrTmp[g_StrTmpIndex]);
            g_StrTmp[g_StrTmpIndex] = NULL;
            g_TempMemoryIsChanged = false;
        }
        else
            break;
    }
}

void MIPS16 ClearSpecificTempMemory(void *addr)
{
    int i;
    for (i = 0; i < g_StrTmpIndex; i++)
    {
        if (g_StrTmp[i] == addr)
        {
            FreeMemory(addr);
            g_StrTmpIndex--;
            while (i < g_StrTmpIndex)
            {
                g_StrTmp[i] = g_StrTmp[i + 1];
                g_StrTmpLocalIndex[i] = g_StrTmpLocalIndex[i + 1];
                i++;
            }
            g_StrTmp[i] = NULL; // Clear the stale entry at end
            return;
        }
    }
}

void MIPS64 __not_in_flash_func(FreeMemory)(unsigned char *addr)
{
    if (addr == NULL)
        return;
#if defined(rp2350)
    int bits;
    if (PSRAMsize)
    {
        if (addr > (unsigned char *)PSRAMbase && addr < (unsigned char *)(PSRAMbase + PSRAMsize))
        {
            // Validate the address is actually allocated before freeing
            bits = SBitsGet(addr);
            if (!(bits & PUSED))
                return; // Address not allocated - nothing to free
            do
            {
                bits = SBitsGet(addr);
                SBitsSet(addr, 0);
                addr += PAGESIZE;
            } while (bits != (PUSED | PLAST));
        }
        else
        {
            // Validate the address is actually allocated before freeing
            bits = MBitsGet(addr);
            if (!(bits & PUSED))
                return; // Address not allocated - nothing to free
            do
            {
                bits = MBitsGet(addr);
                MBitsSet(addr, 0);
                addr += PAGESIZE;
            } while (bits != (PUSED | PLAST));
        }
    }
    else
    {
        // Validate the address is actually allocated before freeing
        bits = MBitsGet(addr);
        if (!(bits & PUSED))
            return; // Address not allocated - nothing to free
        do
        {
            bits = MBitsGet(addr);
            MBitsSet(addr, 0);
            addr += PAGESIZE;
        } while (bits != (PUSED | PLAST));
    }
#else
    int bits;
    // Validate the address is actually allocated before freeing
    bits = MBitsGet(addr);
    if (!(bits & PUSED))
        return; // Address not allocated - nothing to free
    do
    {
        bits = MBitsGet(addr);
        MBitsSet(addr, 0);
        addr += PAGESIZE;
    } while (bits != (PUSED | PLAST));
#endif
}

void InitHeap(bool all)
{
    int i;
    memset(mmap, 0, sizeof(mmap));
    memset(MMHeap, 0, heap_memory_size + 256);
#ifdef rp2350
    if (all)
        memset(psmap, 0, sizeof(psmap));
#endif
    for (i = 0; i < MAXTEMPSTRINGS; i++)
        g_StrTmp[i] = NULL;
#ifdef PICOMITEVGA
    WriteBuf = (unsigned char *)FRAMEBUFFER;
    DisplayBuf = (unsigned char *)FRAMEBUFFER;
    LayerBuf = (unsigned char *)FRAMEBUFFER;
    FrameBuf = (unsigned char *)FRAMEBUFFER;
#else
    FrameBuf = NULL;
    WriteBuf = NULL;
    LayerBuf = NULL;
#endif
}

/***********************************************************************************************************************
 Private memory management functions
************************************************************************************************************************/

#ifdef rp2350
unsigned int __not_in_flash_func(SBitsGet)(unsigned char *addr)
{
    unsigned int i, *p;
    addr -= (unsigned int)PSRAMbase;
    p = &psmap[((unsigned int)addr / PAGESIZE) / PAGESPERWORD];              // point to the word in the memory map
    i = ((((unsigned int)addr / PAGESIZE)) & (PAGESPERWORD - 1)) * PAGEBITS; // get the position of the bits in the word
    return (*p >> i) & ((1 << PAGEBITS) - 1);
}

void __not_in_flash_func(SBitsSet)(unsigned char *addr, int bits)
{
    unsigned int i, *p;
    addr -= (unsigned int)PSRAMbase;
    p = &psmap[((unsigned int)addr / PAGESIZE) / PAGESPERWORD];              // point to the word in the memory map
    i = ((((unsigned int)addr / PAGESIZE)) & (PAGESPERWORD - 1)) * PAGEBITS; // get the position of the bits in the word
    *p = (bits << i) | (*p & (~(((1 << PAGEBITS) - 1) << i)));
}
#endif

static inline __attribute__((always_inline)) unsigned int MBitsGet(unsigned char *addr)
{
    unsigned int page_idx = ((unsigned int)addr - (unsigned int)&MMHeap[0]) >> 8; // divide by 256 using shift
    unsigned int *p = &mmap[page_idx >> 4];                                       // divide by 16 using shift
    unsigned int bit_pos = (page_idx & 15) << 1;                                  // (mod 16) * 2, combined operation
    return (*p >> bit_pos) & 3;                                                   // extract 2 bits
}

static inline __attribute__((always_inline)) void MBitsSet(unsigned char *addr, int bits)
{
    unsigned int page_idx = ((unsigned int)addr - (unsigned int)&MMHeap[0]) >> 8; // divide by 256 using shift
    unsigned int *p = &mmap[page_idx >> 4];                                       // divide by 16 using shift
    unsigned int bit_pos = (page_idx & 15) << 1;                                  // (mod 16) * 2, combined operation
    *p = (*p & ~(3u << bit_pos)) | ((unsigned int)bits << bit_pos);
}
#ifdef rp2350
void __not_in_flash_func (*GetPSMemory)(int size)
{
    unsigned int j, n;
    unsigned char *addr;
    j = n = (size + PAGESIZE - 1) / PAGESIZE; // nbr of pages rounded up
    for (addr = (unsigned char *)(PSRAMbase + PSRAMsize - PAGESIZE); addr >= (unsigned char *)PSRAMbase; addr -= PAGESIZE)
    {
        if (!(SBitsGet(addr) & PUSED))
        {
            if (--n == 0)
            { // found a free slot
                j--;
                SBitsSet(addr + (j * PAGESIZE), PUSED | PLAST); // show that this is used and the last in the chain of pages
                while (j--)
                    SBitsSet(addr + (j * PAGESIZE), PUSED); // set the other pages to show that they are used
                memset(addr, 0, size);                      // zero the memory
                                                            //               dp("alloc = %p (%d)", addr, size);
                return (void *)addr;
            }
        }
        else
            n = j; // not enough space here so reset our count
    }
    // out of memory
    TempStringClearStart = 0;
    ClearTempMemory(); // hopefully this will give us enough to print the prompt
    error("Not enough PSRAM memory");
    return NULL; // keep the compiler happy
}
#endif
void MIPS64 __not_in_flash_func (*GetSystemMemory)(int size)
{ // get memory from the bottom up
    int n = 0, k;
    unsigned char *addr;
    k = (size + PAGESIZE - 1) / PAGESIZE; // nbr of pages rounded up
    for (addr = MMHeap; addr < MMHeap + heap_memory_size - PAGESIZE; addr += PAGESIZE)
    {
        if (!(MBitsGet(addr) & PUSED))
        {
            if (++n == k)
            { // found a free slot
                k--;
                MBitsSet(addr, PUSED | PLAST); // show that this is used and the last in the chain of pages
                while (k--)
                {
                    addr -= PAGESIZE;
                    MBitsSet(addr, PUSED);
                }
                memset(addr, 0, size); // zero the memory
                return (void *)addr;
            }
        }
        else
            n = 0; // not enough space here so reset our count
    }
    // out of memory
#ifdef rp2350
    if (PSRAMsize)
        return GetPSMemory(size);
#endif
    TempStringClearStart = 0;
    ClearTempMemory(); // hopefully this will give us enough to print the prompt
    error("Not enough System Heap memory");
    return NULL; // keep the compiler happy
}
void MIPS64 __not_in_flash_func (*GetMemory)(int size)
{
#ifdef rp2350
    if (PSRAMsize && size > heap_memory_size / 2)
        return GetPSMemory(size);
#endif
    unsigned int j, n, k;
    unsigned char *addr;
    j = n = k = (size + PAGESIZE - 1) / PAGESIZE; // nbr of pages rounded up
    for (addr = MMHeap + heap_memory_size - PAGESIZE; addr >= MMHeap; addr -= PAGESIZE)
    {
        if (!(MBitsGet(addr) & PUSED))
        {
            if (--n == 0)
            { // found a free slot
                j--;
                MBitsSet(addr + (j * PAGESIZE), PUSED | PLAST); // show that this is used and the last in the chain of pages
                while (j--)
                    MBitsSet(addr + (j * PAGESIZE), PUSED); // set the other pages to show that they are used
                memset(addr, 0, size);                      // zero the memory
                return (void *)addr;
            }
        }
        else
            n = j; // not enough space here so reset our count
    }
    // out of memory
#ifdef rp2350
    if (PSRAMsize)
        return GetPSMemory(size);
#endif
    TempStringClearStart = 0;
    ClearTempMemory(); // hopefully this will give us enough to print the prompt
    error("Not enough Heap memory");
    return NULL; // keep the compiler happy
}

void *GetAlignedMemory(int size)
{
    unsigned char *addr = MMHeap;
    while (((uint32_t)addr & (size - 1)) && (!((MBitsGet(addr) & PUSED))) && ((uint32_t)addr < (uint32_t)MMHeap + heap_memory_size))
        addr += PAGESIZE;
    if ((uint32_t)addr == (uint32_t)MMHeap + heap_memory_size)
        StandardError(29);
    unsigned char *retaddr = addr;
    for (; size > 0; addr += PAGESIZE, size -= PAGESIZE)
    {
        if (!(MBitsGet(addr) & PUSED))
        {
            MBitsSet(addr, PUSED);
        }
        else
            error("Not enough aligned memory");
    }
    addr -= PAGESIZE;
    MBitsSet(addr, PUSED | PLAST);
    return (retaddr);
}

int FreeSpaceOnHeap(void)
{
    unsigned int nbr;
    unsigned char *addr;
    nbr = 0;
    for (addr = MMHeap + heap_memory_size - PAGESIZE; addr >= MMHeap; addr -= PAGESIZE)
        if (!(MBitsGet(addr) & PUSED))
            nbr++;
#ifdef rp2350
    if (PSRAMsize)
    {
        for (addr = (unsigned char *)(PSRAMbase + PSRAMsize - PAGESIZE); addr >= (unsigned char *)PSRAMbase; addr -= PAGESIZE)
            if (!(SBitsGet(addr) & PUSED))
                nbr++;
    }
#endif
    return nbr * PAGESIZE;
}

int LargestContiguousHeap(void)
{
    unsigned int nbr;
    unsigned char *addr;
    nbr = 0;
    for (addr = MMHeap; addr < MMHeap + heap_memory_size - PAGESIZE; addr += PAGESIZE)
    {
        if (!(MBitsGet(addr) & PUSED))
            nbr++;
        else
            break;
    }
    return nbr * PAGESIZE;
}

unsigned int UsedHeap(void)
{
    unsigned int nbr;
    unsigned char *addr;
    nbr = 0;
    for (addr = MMHeap + heap_memory_size - PAGESIZE; addr >= MMHeap; addr -= PAGESIZE)
        if (MBitsGet(addr) & PUSED)
            nbr++;
#ifdef rp2350
    if (PSRAMsize)
    {
        for (addr = (unsigned char *)(PSRAMbase + PSRAMsize - PAGESIZE); addr >= (unsigned char *)PSRAMbase; addr -= PAGESIZE)
            if (SBitsGet(addr) & PUSED)
                nbr++;
    }
#endif
    return nbr * PAGESIZE;
}

int MemSize(void *addr)
{ // returns the amount of heap memory allocated to an address
    int i = 0;
    int bits;
#ifdef rp2350
    if (addr > (void *)PSRAMbase && addr < (void *)(PSRAMbase + PSRAMsize))
    {
        if (addr >= (void *)PSRAMbase && addr < (void *)(PSRAMbase + PSRAMsize))
        {
            do
            {
                bits = SBitsGet(addr);
                addr += PAGESIZE;
                i += PAGESIZE;
            } while (bits != (PUSED | PLAST));
        }
        return i;
    }
#endif
    if (addr >= (void *)MMHeap && addr < (void *)(MMHeap + heap_memory_size))
    {
        do
        {
            bits = MBitsGet(addr);
            addr += PAGESIZE;
            i += PAGESIZE;
        } while (bits != (PUSED | PLAST));
    }
    return i;
}

void *ReAllocMemory(void *addr, size_t msize)
{
    int size = MemSize(addr);
    if (msize <= size)
        return addr;
    void *newaddr = GetMemory(msize);
    if (addr != NULL && size != 0)
    {
        memcpy(newaddr, addr, MemSize(addr));
        FreeMemory(addr);
        addr = NULL;
    }
    return newaddr;
}
void __not_in_flash_func(FreeMemorySafe)(void **addr)
{
    if (*addr != NULL)
    {
        if (*addr >= (void *)MMHeap && *addr < (void *)(MMHeap + heap_memory_size))
        {
            FreeMemory(*addr);
            *addr = NULL;
        }
#ifdef rp2350
        if (*addr >= (void *)PSRAMbase && *addr < (void *)(PSRAMbase + PSRAMsize))
        {
            FreeMemory(*addr);
            *addr = NULL;
        }
#endif
    }
}
/*  @endcond */
