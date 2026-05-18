/*
 * gfx_console_shared.h -- Console text rendering shared between device and
 * host (--sim) builds. Provides DisplayPutC (cursor-tracking, control-char
 * handling, wrap/scroll) and GUIPrintChar (single-glyph rasterisation).
 *
 * Both functions read/write the standard MMBasic globals (CurrentX/Y,
 * gui_*, Option.*) and dispatch pixel output via the DrawBitmap /
 * DrawRectangle / DrawCircle function pointers. Keeping them here (rather
 * than in the device-specific Draw.c) lets the host simulator reuse the
 * exact same terminal semantics — no drift between sim and device.
 */
#ifndef GFX_CONSOLE_SHARED_H
#define GFX_CONSOLE_SHARED_H

void GUIPrintChar(int fnt, int fc, int bc, char c, int orientation);
void DisplayPutC(char c);
void ShowCursor(int show);

#endif
