/*
 * runtime/runtime_cmd_load_post_cleanup.c — shared default body for the
 * BASIC-core hook `cmd_load_post_cleanup` invoked by `cmd_load` at the
 * tail of `LOAD "file"`.
 *
 * `cmd_load` runs while ExecuteProgram is iterating over the immediate
 * command buffer (tknbuf / inpbuf). LOAD then tokenises each line of
 * the loaded file through that same tknbuf, leaving the iterator
 * pointing at corrupted bytes. The canonical recovery — used by every
 * port that doesn't keep a separate tokeniser buffer — is to zero
 * inpbuf (so the prompt's next EditInputLine doesn't echo the tail of
 * the last loaded line) and longjmp back to the prompt mark.
 *
 * Previously duplicated byte-identically in:
 *   - ports/host_native/host_runtime.c
 *   - ports/pc386/pc386_runtime.c
 *
 * Pico deliberately does NOT link this TU — its SaveProgramToFlash
 * uses a private tokeniser buffer and `cmd_load` can return normally
 * (the override is an empty body in ports/pico_sdk_common/cmd_files_hooks.c).
 *
 * ESP32 also does NOT link this TU — its body adds an autorun
 * early-return ahead of the longjmp (LOAD "file",R needs nextstmt to
 * stay at ProgMemory so the freshly-loaded program runs). That
 * override lives in ports/esp32_s3_metro/main/esp32_peripheral_stubs.c.
 *
 * docs/port-duplication-audit.md Finding 9.
 */

#include <setjmp.h>
#include <stddef.h>

#include "MMBasic_Includes.h"
#include "runtime/runtime.h"

extern unsigned char inpbuf[];
extern jmp_buf mark;

void cmd_load_post_cleanup(void)
{
    mmbasic_runtime_post_load_longjmp(inpbuf, STRINGSIZE, mark);
}
