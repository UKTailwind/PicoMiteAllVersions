/*
 * ports/pc386/pc386_panic.h — fatal-error helpers.
 *
 * pc386_panic() prints a labelled message to whichever consoles are
 * up (serial COM1 + VGA text), then halts the CPU forever. Used by
 * HAL stubs to catch any code path that reaches an unimplemented
 * surface — under normal operation nothing here ever runs.
 *
 * pc386_halt() is the bare halt without a message; useful when a
 * caller has already printed.
 */

#ifndef PORTS_PC386_PANIC_H
#define PORTS_PC386_PANIC_H

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((noreturn)) void pc386_halt (void);
__attribute__((noreturn)) void pc386_panic(const char *msg);

extern volatile const char *pc386_fault_context;
void pc386_fault_set_context(const char *ctx);
void pc386_fault_clear_context(void);

#ifdef __cplusplus
}
#endif

#endif
