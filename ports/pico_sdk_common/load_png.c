/*
 * ports/pico_sdk_common/load_png.c — LOAD PNG command handler.
 *
 * RP2350-only because the upng library is only linked on those targets.
 * On RP2040 the bottom of this file provides an error stub so FileIO.c's
 * universal `LoadPNG(p)` call site links cleanly and surfaces a sensible
 * error if the BASIC program does try `LOAD PNG ...`.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_keyboard.h"

#if defined(rp2350)

/* Real impl below; on rp2040 the file falls through to a no-op stub
 * at the bottom so FileIO.c's universal call site links cleanly. */
void LoadPNG(unsigned char *p) {
//	int fnbr;
	int xOrigin, yOrigin,w,h, transparent=0, cutoff=20;
    int maxW=HRes;
	int maxH=VRes;
	upng_t* upng;
	// get the command line arguments
	getargs(&p, 9, (unsigned char *)",");                                            // this MUST be the first executable line in the function
    if(argc == 0) error("Argument count");
    if(!InitSDCard()) return;

    unsigned char *q = getFstring(argv[0]);                                        // get the file name

    xOrigin = yOrigin = 0;
	if(argc >= 3 && *argv[2]) xOrigin = getinteger(argv[2]);                    // get the x origin (optional) argument
	if(argc >= 5 && *argv[4]){
		yOrigin = getinteger(argv[4]);                    // get the y origin (optional) argument
	}
	if(argc >= 7 && *argv[6])transparent=getint(argv[6],-1,15);
    if(transparent!=-1)transparent=RGB121map[transparent];
    if(argc==9)cutoff=getint(argv[8],1,254);
	if(strchr((char *)q, '.') == NULL) strcat((char *)q, ".png");
    upng = upng_new_from_file((char *)q);
	routinechecks();
    upng_header(upng);
    w=upng_get_width(upng);
    h= upng_get_height(upng);
    if(w+xOrigin >maxW || h+yOrigin >maxH){
        upng_free(upng);
        error("Image too large");
    }
    if(!(upng_get_format(upng)==3)){
        upng_free(upng);
        error("Invalid format, must be RGBA8888");
    }
	routinechecks();
    upng_decode(upng);
    unsigned char *rr;
	routinechecks();
    rr=(unsigned char *)upng_get_buffer(upng);
    unsigned char *pp=rr;
    unsigned char *ppp=rr;
    char d[3];
    if(transparent==-1){
        unsigned char *buff = GetTempMemory(w*h*3);
        ReadBuffer(xOrigin, yOrigin, xOrigin+w-1, yOrigin+h-1,buff);
        for(int i=0;i<w*h;i++){
            d[0]=rr[2];
            d[1]=rr[1];
            d[2]=rr[0];
            if(rr[3]>cutoff){
                pp[0]=d[0];
                pp[1]=d[1];
                pp[2]=d[2];
            } else {
                pp[0]=buff[0];
                pp[1]=buff[1];
                pp[2]=buff[2];
            }
            pp+=3;
            rr+=4;
            buff+=3;
        }
        DrawBuffer(xOrigin, yOrigin, xOrigin+w-1, yOrigin+h-1,ppp);
    } else {
    for(int i=0;i<w*h;i++){
        d[0]=rr[2];
        d[1]=rr[1];
        d[2]=rr[0];
        if(rr[3]>cutoff){
            pp[0]=d[0];
            pp[1]=d[1];
            pp[2]=d[2];
        } else {
            pp[0]=(transparent & 0xFF0000)>>16;
            pp[1]=(transparent & 0xFF00)>>8;
            pp[2]=(transparent & 0xFF);
        }
        pp+=3;
        rr+=4;
    }
        DrawBuffer(xOrigin, yOrigin, xOrigin+w-1, yOrigin+h-1,ppp);
    }
    upng_free(upng);
    hal_keyboard_clear_repeat_state();
}

#else /* !defined(rp2350) */

/* rp2040 ports don't link upng — universal stub keeps FileIO.c's
 * `LoadPNG(p)` call resolvable and surfaces a sensible error if the
 * BASIC program does try `LOAD PNG ...`. */
void LoadPNG(unsigned char *p) {
    (void)p;
    error("PNG not supported on this port");
}

#endif /* defined(rp2350) */
