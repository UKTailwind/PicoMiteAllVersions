/* ports/pc386/include_shim/assert.h — freestanding shim. */
#ifndef _PC386_ASSERT_H
#define _PC386_ASSERT_H

/* Always-on assert routes failures through pc386_panic. There's no
 * NDEBUG distinction on bare metal — if an invariant trips during
 * boot, halt with a useful message. */
extern void pc386_panic(const char *msg) __attribute__((noreturn));

#define assert(x) do { if (!(x)) pc386_panic("assert: " #x); } while (0)

#endif
