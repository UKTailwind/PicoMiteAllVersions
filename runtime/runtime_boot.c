#include "runtime/runtime.h"

#include <setjmp.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern jmp_buf mark;
extern void MMBasic_RunPromptLoop(void);

static void runtime_call(void (*fn)(void)) {
    if (fn) fn();
}

int mmbasic_runtime_init_common(const mm_runtime_adapter * port, unsigned flags) {
    if (port) {
        runtime_call(port->early_console_init);
        runtime_call(port->memory_backing_init);
        runtime_call(port->platform_pre_options);
    }

    if (flags & MMBASIC_RUNTIME_INIT_FLAG_LOAD_OPTIONS) LoadOptions();

    if (port) {
        runtime_call(port->platform_apply_option_defaults);
        runtime_call(port->platform_validate_or_reset_options);
        runtime_call(port->platform_after_options);
    }

    if (flags & MMBASIC_RUNTIME_INIT_FLAG_INIT_BASIC) InitBasic();
    if (flags & MMBASIC_RUNTIME_INIT_FLAG_INIT_HEAP) InitHeap(true);

    if (flags & MMBASIC_RUNTIME_INIT_FLAG_CLEAR_ERROR) {
        MMerrno = 0;
        MMErrMsg[0] = '\0';
    }

    if (port) {
        runtime_call(port->timebase_init);
        runtime_call(port->display_console_init);
        runtime_call(port->keyboard_init);
        runtime_call(port->audio_init);
        runtime_call(port->filesystem_init);
    }

    if (flags & MMBASIC_RUNTIME_INIT_FLAG_CLEAR_RUNTIME) ClearRuntime(true);
    return 0;
}

int mmbasic_runtime_run_source(const mm_runtime_adapter * port,
                               const char * source,
                               unsigned flags) {
    unsigned source_flags = flags & MMBASIC_SOURCE_FLAGS_ALL;

    if (flags & MMBASIC_RUNTIME_RUN_FLAG_RESET_ERROR) {
        MMerrno = 0;
        MMErrMsg[0] = '\0';
    }

    if (source && mmbasic_tokenise_source_to_progmem(source, source_flags) != 0) {
        return 1;
    }
    if (port && port->after_load_program) port->after_load_program();

    if (flags & MMBASIC_RUNTIME_RUN_FLAG_CLEAR_RUNTIME) ClearRuntime(true);
    if (flags & MMBASIC_RUNTIME_RUN_FLAG_PREPARE_PROGRAM) PrepareProgram(1);

    if (setjmp(mark) == 0) {
        ExecuteProgram(ProgMemory);
    }

    return MMErrMsg[0] ? 1 : 0;
}

void mmbasic_runtime_enter_repl(const mm_runtime_adapter * port, unsigned flags) {
    (void)flags;
    if (port && port->before_prompt_loop) port->before_prompt_loop();
    MMBasic_RunPromptLoop();
}

int mmbasic_runtime_boot(const mm_runtime_adapter * port) {
    return mmbasic_runtime_init_common(port, port ? port->flags : 0);
}

void mmbasic_runtime_repl(const mm_runtime_adapter * port, unsigned flags) {
    mmbasic_runtime_enter_repl(port, flags);
}
