#ifndef ANSI_MODE_H
#define ANSI_MODE_H

/* Override a single mode slot. Returns 0 on success, -1 on bad args.
 * Slots are 1-indexed up to ansi_mode_max(); width and height must
 * be positive. */
int ansi_mode_set(int n, int w, int h);

/* Read a configured mode slot. Returns 0 on success, -1 on bad args. */
int ansi_mode_get(int n, int * w, int * h);

/* Highest valid slot number (currently 5). */
int ansi_mode_max(void);

#endif
