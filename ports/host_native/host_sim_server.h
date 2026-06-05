/*
 * host_sim_server.h -- Start/stop API for the --sim HTTP+WS server.
 */

#ifndef HOST_SIM_SERVER_H
#define HOST_SIM_SERVER_H

#include <stddef.h>
#include <stdint.h>

int host_sim_server_start(const char * listen_addr, int port, const char * web_root);
void host_sim_server_stop(void);

/*
 * 1ms tick thread. On device PicoMite.c:826 `timer_callback` bumps
 * mSecTimer / CursorTimer / PauseTimer / Timer1..5 / TickTimer[] /
 * ScrewUpTimer every ms from an IRQ. The --sim build runs the same
 * updates on a background pthread so PAUSE, TIMER, ON INTERRUPT TICK,
 * and cursor blink advance without hardware timers.
 *
 * Only available in MMBASIC_SIM builds — non-sim test harness calls
 * host_time_us_64 / host_sync_msec_timer to advance the counters
 * lazily instead.
 */
void host_sim_tick_start(void);
void host_sim_tick_stop(void);

/*
 * Single-producer (WS handler) / single-consumer (MMBasic main thread)
 * key queue. The server pushes on WS message receipt; MMInkey() drains
 * via host_sim_pop_key(). Returns -1 when empty.
 */
void host_sim_push_key(int code);
int host_sim_pop_key(void);

/*
 * Graphics command stream used by --sim mode. Drawing primitives record
 * every mutation of the FRONT framebuffer (host_framebuffer) via emit_*;
 * the server thread calls host_sim_cmd_drain() on its broadcast tick and
 * forwards the opcode bytes as one "CMDS" WebSocket message.
 *
 * Non-sim builds (test harness, WASM, ANSI, stdio) don't link
 * host_sim_server.c. They link host_sim_emit_stub.c instead, which
 * provides weak no-ops for every emit_* + host_sim_cmds_target_is_front;
 * the strong defs in host_sim_server.c override at link time when the
 * sim variant is built. Call sites stay unconditional.
 */
int host_sim_cmds_target_is_front(void);
void host_sim_emit_cls(int colour);
void host_sim_emit_rect(int x1, int y1, int x2, int y2, int colour);
void host_sim_emit_pixel(int x, int y, int colour);
void host_sim_emit_scroll(int lines, int bg);
void host_sim_emit_blit(int x, int y, int w, int h, const uint32_t * pixels);
size_t host_sim_cmd_drain(uint8_t ** out_buf, size_t * out_cap);

#endif
