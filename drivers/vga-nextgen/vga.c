#include "vga.h"
#include "hardware/clocks.h"
#include "stdbool.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/systick.h"

#include "hardware/dma.h"
#include "hardware/irq.h"
#include <string.h>
#include <stdio.h>
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "stdlib.h"

uint16_t pio_program_VGA_instructions[] = {
    //     .wrap_target
    0x6008, //  0: out    pins, 8
    //     .wrap
};

const struct pio_program pio_program_VGA = {
    .instructions = pio_program_VGA_instructions,
    .length = 1,
    .origin = -1,
};


static uint32_t* lines_pattern[4];
static uint32_t* lines_pattern_data = NULL;
static int _SM_VGA = -1;


static int N_lines_total = 525;
static int N_lines_visible = 480;
static int line_VS_begin = 490;
static int line_VS_end = 491;
static int shift_picture = 0;

static int visible_line_size = 320;


static int dma_chan_ctrl;
static int dma_chan;

/// TODO:
static uint graphics_buffer_width = 0;
static uint graphics_buffer_height = 0;
static int graphics_buffer_shift_x = 0;
static int graphics_buffer_shift_y = 0;

static bool is_flash_line = false;
static bool is_flash_frame = false;

static uint32_t bg_color[2];
static uint16_t palette16_mask = 0;

//буфер 2К текстовой палитры для быстрой работы
static uint16_t* txt_palette_fast = NULL;

/// TODO:
extern volatile int QVgaScanLine;
extern unsigned char *DisplayBuf;
extern unsigned char *LayerBuf;
static int vgaloop2, vgaloop4, vgaloop8, vgaloop16;
extern int MODE1SIZE;
extern int MODE2SIZE;
extern int MODE3SIZE;
extern int MODE4SIZE;
extern int MODE5SIZE;
extern volatile int ytileheight;
extern volatile int X_TILE, Y_TILE;
extern uint16_t M_Foreground[16], M_Background[16];
extern uint16_t *tilefcols, *tilebcols;
extern short HRes, VRes;
extern volatile int DISPLAY_TYPE;
extern volatile uint8_t transparent, transparents;
extern unsigned char *SecondLayer;
#define SCREENMODE1     29
#define VGADISPLAY      SCREENMODE1  
#define SCREENMODE2     30
#define SCREENMODE3     31
extern uint16_t map16[16];


void __time_critical_func() dma_handler_VGA() {
    static int tile=0, tc=0;
    dma_hw->ints0 = 1u << dma_chan_ctrl;
    static uint32_t frame_number = 0;
    static uint32_t screen_line = 0;
    screen_line++;

    if (screen_line == N_lines_total) {
        //vsync();
        screen_line = 0;
        frame_number++;
        tile=0;
        tc=0;
    }
    QVgaScanLine = screen_line;

    if (screen_line >= N_lines_visible) {
        //заполнение цветом фона
        if (screen_line == N_lines_visible || screen_line == N_lines_visible + 3) {
            uint32_t* output_buffer_32bit = lines_pattern[2 + (screen_line & 1)];
            output_buffer_32bit += shift_picture / 4;
            uint32_t p_i = ((screen_line & is_flash_line) + (frame_number & is_flash_frame)) & 1;
            uint32_t color32 = bg_color[p_i];
            for (int i = visible_line_size / 2; i--;) {
                *output_buffer_32bit++ = color32;
            }
        }

        //синхросигналы
        if (screen_line >= line_VS_begin && screen_line <= line_VS_end)
            dma_channel_set_read_addr(dma_chan_ctrl, &lines_pattern[1], false); //VS SYNC
        else
            dma_channel_set_read_addr(dma_chan_ctrl, &lines_pattern[0], false);
        return;
    }

    int y, line_number;

    uint32_t* * output_buffer = &lines_pattern[2 + (screen_line & 1)];
    line_number = screen_line;
    y = screen_line - graphics_buffer_shift_y;

    if (y < 0) {
        dma_channel_set_read_addr(dma_chan_ctrl, &lines_pattern[0], false); // TODO: ensue it is required
        return;
    }
    if (y >= graphics_buffer_height) {
        // заполнение линии цветом фона
        if (y == graphics_buffer_height || y == graphics_buffer_height + 1 || y == graphics_buffer_height + 2) {
            uint32_t* output_buffer_32bit = *output_buffer;
            uint32_t p_i = ((line_number & is_flash_line) + (frame_number & is_flash_frame)) & 1;
            uint32_t color32 = bg_color[p_i];
            output_buffer_32bit += shift_picture / 4;
            for (int i = visible_line_size / 2; i--;) {
                *output_buffer_32bit++ = color32;
            }
        }
        dma_channel_set_read_addr(dma_chan_ctrl, output_buffer, false);
        return;
    };
    if (!DisplayBuf || !LayerBuf || !tilefcols || !tilebcols) {
        dma_channel_set_read_addr(dma_chan_ctrl, output_buffer, false);
        return;
    }

    //зона прорисовки изображения
    uint16_t* output_buffer_16bit = (uint16_t *)(*output_buffer);
    output_buffer_16bit += shift_picture / 2; //смещение началы вывода на размер синхросигнала

    graphics_buffer_shift_x &= 0xfffffff2; //2bit buf

    //для div_factor 2
    uint max_width = graphics_buffer_width;
    if (graphics_buffer_shift_x < 0) {
        max_width += graphics_buffer_shift_x;
    }
    else {
#define div_factor (2)
        output_buffer_16bit += graphics_buffer_shift_x * 2 / div_factor;
    }

    int width = MIN((visible_line_size - ((graphics_buffer_shift_x > 0) ? (graphics_buffer_shift_x) : 0)), max_width);
    if (width < 0) return; // TODO: detect a case

    static uint16_t map4_2_8[16] = {
        0b11000000,
        0b11000011,
        0b11000100,
        0b11000111,
        0b11001000,
        0b11001011,
        0b11001100,
        0b11001111,
        0b11110000,
        0b11110011,
        0b11110100,
        0b11110111,
        0b11111000,
        0b11111011,
        0b11111100,
        0b11111111
    };

    if (DISPLAY_TYPE == SCREENMODE1) {
        uint16_t *q = output_buffer_16bit;
        volatile unsigned char *p  = &DisplayBuf[screen_line * vgaloop8];
        volatile unsigned char *pp = &LayerBuf[screen_line * vgaloop8];
        if (tc == ytileheight){
            tile++;
            tc=0;
        }
        tc++;
        register int pos= tile * X_TILE;
        for(register int i = 0; i < vgaloop16; ++i) {
            register int d = *p++ | *pp++;
            register int low  = d & 0xF;
            register int high = d >> 4;
            register uint32_t u16 = (M_Foreground[low] & tilefcols[pos]) | (M_Background[low] & tilebcols[pos]); // 4x4bit
            *q++ = map4_2_8[u16 & 0xF] | (map4_2_8[(u16 >> 4) & 0xF] << 8);
            *q++ = map4_2_8[(u16 >> 8) & 0xF] | (map4_2_8[(u16 >> 12) & 0xF] << 8);
            u16 = (M_Foreground[high]& tilefcols[pos]) | (M_Background[high] & tilebcols[pos]) ;
            *q++ = map4_2_8[u16 & 0xF] | (map4_2_8[(u16 >> 4) & 0xF] << 8);
            *q++ = map4_2_8[(u16 >> 8) & 0xF] | (map4_2_8[(u16 >> 12) & 0xF] << 8);
            pos++;
            d = *p++ | *pp++;
            low = d & 0xF;
            high = d++ >>4;
            u16 = (M_Foreground[low] & tilefcols[pos]) | (M_Background[low] & tilebcols[pos]) ;
            *q++ = map4_2_8[u16 & 0xF] | (map4_2_8[(u16 >> 4) & 0xF] << 8);
            *q++ = map4_2_8[(u16 >> 8) & 0xF] | (map4_2_8[(u16 >> 12) & 0xF] << 8);
            u16 = (M_Foreground[high]& tilefcols[pos]) | (M_Background[high] & tilebcols[pos]) ;
            *q++ = map4_2_8[u16 & 0xF] | (map4_2_8[(u16 >> 4) & 0xF] << 8);
            *q++ = map4_2_8[(u16 >> 8) & 0xF] | (map4_2_8[(u16 >> 12) & 0xF] << 8);
            pos++;
        }
#ifdef rp2350
    } else if(DISPLAY_TYPE==SCREENMODE3){
        uint8_t transparent16=(uint8_t)transparent;
        register unsigned char *p=&DisplayBuf[screen_line * vgaloop2];
        register unsigned char *q=&LayerBuf[screen_line * vgaloop2];
        register int low, high, low2, high2;
        register uint8_t *r = (uint8_t *)output_buffer_16bit;
        for(register int i = 0; i < vgaloop2; ++i){
            low= map16[p[i] & 0xF];
            high=map16[(p[i] & 0xF0)>>4];
            low2= map16[q[i] & 0xF];
            high2=map16[(q[i] & 0xF0)>>4];
            if ( low2 != transparent16) low = low2;
            if ( high2!= transparent16) high = high2;
            *r++ = map4_2_8[low];
            *r++ = map4_2_8[high];
        }
#endif
    } else { //mode 2
        int line = screen_line >> 1;
        register unsigned char *dd=&DisplayBuf[line * vgaloop4];
        register unsigned char *ll=&LayerBuf[line * vgaloop4];
        uint8_t transparent16=(uint8_t)transparent;
#ifdef rp2350
        uint8_t s;
        uint8_t transparent16s=(uint8_t)transparents;
#endif
#ifdef rp2350
        register unsigned char *ss=&SecondLayer[line * vgaloop4];
        if(ss==dd){
            ss=ll;
            transparent16s=transparent16;
        }
        register int low3, high3;
#endif
        register int low, high, low2, high2;
        register uint8_t *r = (uint8_t*)output_buffer_16bit;
        register uint8_t l,d;
        for(int i=0;i<vgaloop4;i+=2){
            d=*dd++;
            l=*ll++;
            low= map16[d & 0xF];
            d>>=4;
            high=map16[d];
            low2= map16[l & 0xF];
            l>>=4;
            high2=map16[l];
#ifdef rp2350
            s=*ss++;
            low3= map16[s & 0xF];
            s>>=4;
            high3=map16[s];
#endif
            if(low2!=transparent16)low=low2;
            if(high2!=transparent16)high=high2;
#ifdef rp2350
            if(low3!=transparent16s)low=low3;
            if(high3!=transparent16s)high=high3;
#endif
            low = map4_2_8[low];
            *r++ = low;
            *r++ = low;
            high = map4_2_8[high];
            *r++ = high;
            *r++ = high;

            d=*dd++;
            l=*ll++;
            low= map16[d & 0xF];
            d>>=4;
            high=map16[d];
            low2= map16[l & 0xF];
            l>>=4;
            high2=map16[l];
#ifdef rp2350
            s=*ss++;
            low3= map16[s & 0xF];
            s>>=4;
            high3=map16[s];
#endif
            if(low2!=transparent16)low=low2;
            if(high2!=transparent16)high=high2;
#ifdef rp2350
            if(low3!=transparent16s)low=low3;
            if(high3!=transparent16s)high=high3;
#endif
            low = map4_2_8[low];
            *r++ = low;
            *r++ = low;
            high = map4_2_8[high];
            *r++ = high;
            *r++ = high;
        }
    }

/**
    for  (int x = 0; x < width; ++x) {
        register uint8_t cx = input_buffer_8bit[x ^ 2] & 0b00111111;
        uint16_t c = (cx >> 4) | (cx & 0b1100) | ((cx & 0b11) << 4); // swap R and B
        *output_buffer_16bit++ = ((c << 8 | c) & 0x3f3f) | palette16_mask;
    }
*/
    dma_channel_set_read_addr(dma_chan_ctrl, output_buffer, false);
}

void graphics_set_mode() {
    if (_SM_VGA < 0) return; // если  VGA не инициализирована -

    // Если мы уже проиницилизированы - выходим
    if (txt_palette_fast && lines_pattern_data) {
        return;
    };
    uint8_t TMPL_VHS8 = 0;
    uint8_t TMPL_VS8 = 0;
    uint8_t TMPL_HS8 = 0;
    uint8_t TMPL_LINE8 = 0;

    int line_size;
    double fdiv = 100;
    int HS_SIZE = 4;
    int HS_SHIFT = 100;

            TMPL_LINE8 = 0b11000000;
            HS_SHIFT = 328 * 2;
            HS_SIZE = 48 * 2;
            line_size = 400 * 2;
            shift_picture = line_size - HS_SHIFT;
            palette16_mask = 0xc0c0;
            visible_line_size = 320;
            N_lines_total = 525;
            N_lines_visible = 480;
            line_VS_begin = 490;
            line_VS_end = 491;
            fdiv = clock_get_hz(clk_sys) / 25175000.0; //частота пиксельклока
    int  QVGA_HACT = 640;
    int  QVGA_VACT	= 480;
    vgaloop4=QVGA_HACT/4;
    vgaloop8=QVGA_HACT/8;
    vgaloop16=QVGA_HACT/16;
    vgaloop2=QVGA_HACT/2;
    MODE1SIZE=QVGA_HACT*QVGA_VACT/8;
    MODE2SIZE=(QVGA_HACT/2)*(QVGA_VACT/2)/2;
    MODE3SIZE=QVGA_HACT*QVGA_VACT/2;
    HRes=QVGA_HACT;
    VRes=QVGA_VACT;

    //корректировка  палитры по маске бит синхры
    bg_color[0] = (bg_color[0] & 0x3f3f3f3f) | palette16_mask | palette16_mask << 16;
    bg_color[1] = (bg_color[1] & 0x3f3f3f3f) | palette16_mask | palette16_mask << 16;

    //инициализация шаблонов строк и синхросигнала
    if (!lines_pattern_data) //выделение памяти, если не выделено
    {
        const uint32_t div32 = (uint32_t)(fdiv * (1 << 16) + 0.0);
        PIO_VGA->sm[_SM_VGA].clkdiv = div32 & 0xfffff000; //делитель для конкретной sm
        dma_channel_set_trans_count(dma_chan, line_size / 4, false);

        lines_pattern_data = (uint32_t *)calloc(line_size * 4 / 4, sizeof(uint32_t));

        for (int i = 0; i < 4; i++) {
            lines_pattern[i] = &lines_pattern_data[i * (line_size / 4)];
        }
        // memset(lines_pattern_data,N_TMPLS*1200,0);
        TMPL_VHS8 = TMPL_LINE8 ^ 0b11000000;
        TMPL_VS8 = TMPL_LINE8 ^ 0b10000000;
        TMPL_HS8 = TMPL_LINE8 ^ 0b01000000;

        uint8_t* base_ptr = (uint8_t *)lines_pattern[0];
        //пустая строка
        memset(base_ptr, TMPL_LINE8, line_size);
        //memset(base_ptr+HS_SHIFT,TMPL_HS8,HS_SIZE);
        //выровненная синхра вначале
        memset(base_ptr, TMPL_HS8, HS_SIZE);

        // кадровая синхра
        base_ptr = (uint8_t *)lines_pattern[1];
        memset(base_ptr, TMPL_VS8, line_size);
        //memset(base_ptr+HS_SHIFT,TMPL_VHS8,HS_SIZE);
        //выровненная синхра вначале
        memset(base_ptr, TMPL_VHS8, HS_SIZE);

        //заготовки для строк с изображением
        base_ptr = (uint8_t *)lines_pattern[2];
        memcpy(base_ptr, lines_pattern[0], line_size);
        base_ptr = (uint8_t *)lines_pattern[3];
        memcpy(base_ptr, lines_pattern[0], line_size);
    }
}

void graphics_set_buffer(uint8_t* buffer, const uint16_t width, const uint16_t height) {
///    graphics_buffer = buffer;
    graphics_buffer_width = width;
    graphics_buffer_height = height;
}


void graphics_set_offset(const int x, const int y) {
    graphics_buffer_shift_x = x;
    graphics_buffer_shift_y = y;
}

void graphics_set_flashmode(const bool flash_line, const bool flash_frame) {
    is_flash_frame = flash_frame;
    is_flash_line = flash_line;
}

void graphics_set_bgcolor(const uint32_t color888) {
    const uint8_t conv0[] = { 0b00, 0b00, 0b01, 0b10, 0b10, 0b10, 0b11, 0b11 };
    const uint8_t conv1[] = { 0b00, 0b01, 0b01, 0b01, 0b10, 0b11, 0b11, 0b11 };

    const uint8_t b = (color888 & 0xff) / 42;

    const uint8_t r = (color888 >> 16 & 0xff) / 42;
    const uint8_t g = (color888 >> 8 & 0xff) / 42;

    const uint8_t c_hi = conv0[r] << 4 | conv0[g] << 2 | conv0[b];
    const uint8_t c_lo = conv1[r] << 4 | conv1[g] << 2 | conv1[b];
    bg_color[0] = (((c_hi << 8 | c_lo) & 0x3f3f) | palette16_mask) << 16 |
                  (((c_hi << 8 | c_lo) & 0x3f3f) | palette16_mask);
    bg_color[1] = (((c_lo << 8 | c_hi) & 0x3f3f) | palette16_mask) << 16 |
                  (((c_lo << 8 | c_hi) & 0x3f3f) | palette16_mask);
}

void graphics_init() {
    //инициализация PIO
    //загрузка программы в один из PIO
    const uint offset = pio_add_program(PIO_VGA, &pio_program_VGA);
    _SM_VGA = pio_claim_unused_sm(PIO_VGA, true);
    const uint sm = _SM_VGA;

    for (int i = 0; i < 8; i++) {
        gpio_init(VGA_BASE_PIN + i);
        gpio_set_dir(VGA_BASE_PIN + i, GPIO_OUT);
        pio_gpio_init(PIO_VGA, VGA_BASE_PIN + i);
    }; //резервируем под выход PIO

    pio_sm_set_consecutive_pindirs(PIO_VGA, sm, VGA_BASE_PIN, 8, true); //конфигурация пинов на выход

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + 0, offset + (pio_program_VGA.length - 1));

    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX); //увеличение буфера TX за счёт RX до 8-ми
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_out_pins(&c, VGA_BASE_PIN, 8);
    pio_sm_init(PIO_VGA, sm, offset, &c);

    pio_sm_set_enabled(PIO_VGA, sm, true);

    //инициализация DMA
    dma_chan_ctrl = dma_claim_unused_channel(true);
    dma_chan = dma_claim_unused_channel(true);
    //основной ДМА канал для данных
    dma_channel_config c0 = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);

    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);

    uint dreq = DREQ_PIO1_TX0 + sm;
    if (PIO_VGA == pio0) dreq = DREQ_PIO0_TX0 + sm;

    channel_config_set_dreq(&c0, dreq);
    channel_config_set_chain_to(&c0, dma_chan_ctrl); // chain to other channel
	channel_config_set_irq_quiet(&c0, true);
	c0.ctrl |= DMA_CH0_CTRL_TRIG_HIGH_PRIORITY_BITS;

    dma_channel_configure(
        dma_chan,
        &c0,
        &PIO_VGA->txf[sm], // Write address
        lines_pattern[0], // read address
        600 / 4, //
        false // Don't start yet
    );
    //канал DMA для контроля основного канала
    dma_channel_config c1 = dma_channel_get_default_config(dma_chan_ctrl);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);

    channel_config_set_read_increment(&c1, false);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_chain_to(&c1, dma_chan); // chain to other channel

    dma_channel_configure(
        dma_chan_ctrl,
        &c1,
        &dma_hw->ch[dma_chan].read_addr, // Write address
        &lines_pattern[0], // read address
        1, //
        false // Don't start yet
    );

    graphics_set_mode();
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler_VGA);
// set highest IRQ priority
	irq_set_priority(DMA_IRQ_0, 0);
    dma_channel_set_irq0_enabled(dma_chan_ctrl, true);
  //  irq_set_enabled(DMA_IRQ_0, true);
   // dma_start_channel_mask(1u << dma_chan);
}
