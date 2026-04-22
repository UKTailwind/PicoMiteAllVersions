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

struct spritebuffer spritebuff[MAXBLITBUF + 1] = { 0 };

#ifndef PICOMITEWEB
struct D3D *struct3d[MAX3D + 1] = { NULL };
s_camera   camera[MAXCAM + 1];
#endif
