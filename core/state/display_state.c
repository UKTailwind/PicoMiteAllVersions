/*
 * core/state/display_state.c — cross-cutting display globals.
 *
 * Storage for display-state globals referenced from many translation
 * units (Draw.c, MM_Misc.c, External.c, Editor.c, GUI.c, the VM). Keeping
 * the definitions here lets per-display driver files live alongside each
 * other without hitting multiple-definition link errors.
 *
 * Extern declarations live in Draw.h.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

short HRes = 0, VRes = 0;

int layer_in_use[MAXLAYER + 1];

struct spritebuffer spritebuff[MAXBLITBUF + 1] = {0};

/* 3D polygon + camera state. Referenced only from
 * drivers/gfx_3d/gfx_3d.c, which is not linked on WEB builds (WEB
 * provides a closeall3d stub via MMtcpserver.c). Storage is
 * unconditional — the few bytes of BSS on WEB are not worth a
 * target-macro gate in this file. */
struct D3D * struct3d[MAX3D + 1] = {NULL};
s_camera __scratch_y("display_state") camera[MAXCAM + 1];

/* Async layer/framebuffer merge-pipeline state. Only the PICOMITE
 * (SPI-LCD) device target actually runs the pipeline on core1; on VGA,
 * HDMI, WEB, and host builds, mergerunning is never set true but the
 * storage has to exist because the flags are read from every build
 * (Draw.c, MM_Misc.c, vm_sys_graphics.c, host_fb.c, …). Previously
 * these lived in a PICOMITE-gated branch in Draw.c and were duplicated
 * on host via the same branch falling through. Consolidating here lets
 * the #ifdef in Draw.c go away. */
bool mergerunning = false;
volatile bool mergedone = false;
uint32_t mergetimer = 0;

/* SSD1963 scroll-start register shadow. Only SSD1963.c actually
 * exercises this (as the display's current scroll origin); non-
 * SSD1963 targets never read a non-zero value but Draw.c /
 * MM_Misc.c reference it unconditionally now that the
 * ScrollLCD==ScrollLCDMEM332 branch is gate-free. */
volatile int ScrollStart = 0;

/* GUI-related globals formerly split across GUI.c (GUICONTROLS
 * builds), Draw.c (non-GUICONTROLS non-host PicoMite), and
 * host_runtime.c (host). Consolidated here so Draw.c can drop its
 * `#if !defined(GUICONTROLS) && !defined(MMBASIC_HOST)` storage
 * block and the definitions don't multiply across GUI.c / host. */
short gui_font_width;
short gui_font_height;
int display_backlight;
int gui_click_pin = 0;
int last_fcolour;
int last_bcolour;
volatile int CursorTimer = 0;
