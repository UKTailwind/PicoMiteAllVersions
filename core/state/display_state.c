/*
 * core/state/display_state.c — hoisted cross-cutting display globals.
 *
 * See docs/real-hal-plan.md § "Cross-cutting state — hoisted in Phase 0.5".
 *
 * Phase 0.5 hoist: move storage of display-state globals out of Draw.c so that
 * later phases can split Draw.c into per-display drivers in drivers/<name>/
 * without hitting multiple-definition link errors.
 *
 * This file defines the storage. Callers continue to use the extern
 * declarations already in Draw.h / Include.h. No behavioural change — only
 * the translation unit that owns the storage has moved.
 *
 * Subsequent hoists (FontTable, CursorTimer, gui_font*, other gated display
 * globals) land in follow-up commits to this same file.
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
