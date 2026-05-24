/*
 * host_sim_emit_stub.c — weak no-op stubs for the WebSocket emit
 * stream used by --sim mode.
 *
 * The native sim variant links host_sim_server.c, which provides
 * strong overrides for every symbol below. Builds that don't link
 * host_sim_server.c (native test harness, WASM, ANSI, stdio) get
 * these no-ops instead — same effect as the previous
 * #ifdef MMBASIC_SIM guards at every call site, but the gate now
 * sits at link time instead of being open-coded by every caller.
 */

#include <stddef.h>
#include <stdint.h>

/* PE/COFF (mingw-w64) doesn't honour ELF/Mach-O-style weak symbols,
 * so on Windows the attribute degrades to nothing and these become
 * strong definitions. That is fine for the variants that include
 * this file — none of them also link host_sim_server.c, which is
 * what would have provided strong overrides on POSIX. */
#ifdef _WIN32
#define HOST_SIM_WEAK
#else
#define HOST_SIM_WEAK __attribute__((weak))
#endif

HOST_SIM_WEAK int host_sim_cmds_target_is_front(void) {
    return 0;
}

HOST_SIM_WEAK void host_sim_emit_cls(int colour) {
    (void)colour;
}

HOST_SIM_WEAK void host_sim_emit_rect(int x1, int y1, int x2, int y2, int colour) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)colour;
}

HOST_SIM_WEAK void host_sim_emit_pixel(int x, int y, int colour) {
    (void)x; (void)y; (void)colour;
}

HOST_SIM_WEAK void host_sim_emit_scroll(int lines, int bg) {
    (void)lines; (void)bg;
}

HOST_SIM_WEAK void host_sim_emit_blit(int x, int y, int w, int h, const uint32_t *pixels) {
    (void)x; (void)y; (void)w; (void)h; (void)pixels;
}
