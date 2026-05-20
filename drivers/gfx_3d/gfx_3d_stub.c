/*
 * drivers/gfx_3d/gfx_3d_stub.c — stubs for the DRAW3D subsystem on
 * targets without it (PICOMITEWEB / WEBRP2350).
 *
 * Only `closeall3d` is referenced from the non-WEB path (FileIO.c
 * calls it). WEB's MMtcpserver.c already provides its own stub, but
 * MM_Misc.c now calls closeall3d unconditionally via the HAL path,
 * so this stub covers any link target that can't pull in the real
 * gfx_3d.c (e.g. a future minimal port).
 *
 * cmd_3D and fun_3D are only referenced in AllCommands.h under the
 * `#else` of PICOMITEWEB, so WEB links don't need stubs for them.
 */

void error(char *msg, ...);

void closeall3d(void) { }
void cmd_3D(void) { error("3D not available"); }
void fun_3D(void) { error("3D not available"); }
