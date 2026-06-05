/*
 * drivers/upng_sprite/upng_sprite.c — BLIT LOADPNG sprite loader.
 *
 * Extracted from Draw.c's `#ifdef rp2350` branch of cmd_sprite.
 * Linked on every rp2350 target (the upng.c PNG decoder is only
 * available on rp2350 per CMakeLists line 299).
 *
 * Caller passes the cmdline pointer after "LOADPNG "; we parse
 * argv ourselves and populate spritebuff[bnbr].{sprite,blitstore}ptr
 * with a 4-bit packed sprite in RGB121 or mode-2 format.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void hal_port_sprite_loadpng(unsigned char * p) {
    int bnbr;
    int toggle = 0, transparent = 0, cutoff = 30;
    int w, h;
    upng_t * upng;
    // get the command line arguments
    getargs(&p, 11, (unsigned char *)","); // this MUST be the first executable line in the function
    if (*argv[0] == '#') argv[0]++;        // check if the first arg is prefixed with a #
    bnbr = getint(argv[0], 1, MAXBLITBUF); // get the buffer number
    if (spritebuff[bnbr].spritebuffptr) error("Buffer % in use", bnbr);
    if (argc == 0) error("Argument count");
    if (!InitSDCard()) return;
    unsigned char * q = getFstring(argv[2]); // get the file name
    if (argc >= 5 && *argv[4]) transparent = getint(argv[4], 0, 15);
    transparent = RGB121map[transparent];
    if (argc == 7) cutoff = getint(argv[6], 1, 254);
    if (strchr((char *)q, '.') == NULL) strcat((char *)q, ".png");
    upng = upng_new_from_file((char *)q);
    routinechecks();
    upng_header(upng);
    w = upng_get_width(upng);
    h = upng_get_height(upng);
    spritebuff[bnbr].spritebuffptr = GetMemory((w * h + 4) >> 1);
    spritebuff[bnbr].blitstoreptr = GetMemory((w * h + 4) >> 1);
    spritebuff[bnbr].w = w;
    spritebuff[bnbr].h = h;
    spritebuff[bnbr].master = 0;
    spritebuff[bnbr].mymaster = -1;
    spritebuff[bnbr].x = 10000;
    spritebuff[bnbr].y = 10000;
    spritebuff[bnbr].layer = -1;
    spritebuff[bnbr].next_x = 10000;
    spritebuff[bnbr].next_y = 10000;
    spritebuff[bnbr].active = false;
    spritebuff[bnbr].lastcollisions = 0;
    spritebuff[bnbr].edges = 0;
    unsigned char * t = (unsigned char *)spritebuff[bnbr].spritebuffptr;
    if (w > HRes || h > VRes) {
        upng_free(upng);
        error("Image too large");
    }
    if (!(upng_get_format(upng) == 3)) {
        upng_free(upng);
        error("Invalid format, must be RGBA8888");
    }
    routinechecks();
    upng_decode(upng);
    unsigned char * rr;
    routinechecks();
    rr = (unsigned char *)upng_get_buffer(upng);
    unsigned char * pp = rr;
    char d[3];
    int i = w * h;
    while (i--) {
        d[0] = rr[2];
        d[1] = rr[1];
        d[2] = rr[0];
        if (rr[3] > cutoff) {
            pp[0] = d[0];
            pp[1] = d[1];
            pp[2] = d[2];
        } else {
            pp[0] = (transparent & 0xFF0000) >> 16;
            pp[1] = (transparent & 0xFF00) >> 8;
            pp[2] = (transparent & 0xFF);
        }
        if (DISPLAY_TYPE == SCREENMODE1) {
            if (toggle) {
                *t |= (char)(((uint16_t)pp[2] + (uint16_t)pp[1] + (uint16_t)pp[0]) < 0x180 ? 0 : 0xF0);
            } else {
                *t = (char)(((uint16_t)pp[2] + (uint16_t)pp[1] + (uint16_t)pp[0]) < 0x180 ? 0 : 0xF);
            }
        } else {
            if (toggle) {
                *t |= ((pp[2] & 0x80)) | ((pp[1] & 0xC0) >> 1) | ((pp[0] & 0x80) >> 3);
            } else {
                *t = ((pp[2] & 0x80) >> 4) | ((pp[1] & 0xC0) >> 5) | ((pp[0] & 0x80) >> 7);
            }
        }
        if (toggle) t++;
        toggle = !toggle;
        pp += 3;
        rr += 4;
    }
    upng_free(upng);
}
