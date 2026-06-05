#include "runtime/runtime.h"

#if defined(__has_include)
#if __has_include("pico.h")
#include "pico.h"
#elif __has_include("pico/platform.h")
#include "pico/platform.h"
#endif
#endif

#include "MMBasic_Includes.h"

#ifndef MMB_HOT_FUNC
#define MMB_HOT_FUNC(name) name
#endif

void MMB_HOT_FUNC(mmbasic_runtime_poll_service)(
    mmbasic_runtime_service_fn service) {
    if (service) service();
}

void MMB_HOT_FUNC(mmbasic_runtime_poll_service_once)(
    int * active,
    mmbasic_runtime_service_fn service) {
    if (!service) return;
    if (active && *active) return;
    if (active) *active = 1;
    service();
    if (active) *active = 0;
}

void MMB_HOT_FUNC(mmbasic_runtime_checkabort_poll)(
    mmbasic_runtime_service_fn service) {
    mmbasic_runtime_poll_service(service);
}

void MMB_HOT_FUNC(mmbasic_runtime_routinechecks_poll)(
    mmbasic_runtime_service_fn service) {
    mmbasic_runtime_poll_service(service);
}

bool MMB_HOT_FUNC(mmbasic_runtime_abort_requested)(
    volatile int * abort_flag) {
    return abort_flag && *abort_flag;
}

static void MMB_HOT_FUNC(runtime_abort_common)(
    const mmbasic_runtime_abort_adapter * adapter) {
    if (!adapter) return;
    mmbasic_runtime_poll_service(adapter->service);

    if ((adapter->flags & MMBASIC_RUNTIME_ABORT_FLAG_CHECK_ABORT) &&
        mmbasic_runtime_abort_requested(adapter->abort_flag)) {
        if (adapter->before_abort) adapter->before_abort();
        if (adapter->flags & MMBASIC_RUNTIME_ABORT_FLAG_DO_END_LONGJMP) {
            do_end(false);
            longjmp(mark, 1);
        }
    }

    if (adapter->after_poll) adapter->after_poll();
}

void MMB_HOT_FUNC(mmbasic_runtime_checkabort)(
    const mmbasic_runtime_abort_adapter * adapter) {
    runtime_abort_common(adapter);
}

void MMB_HOT_FUNC(mmbasic_runtime_routinechecks)(
    const mmbasic_runtime_abort_adapter * adapter) {
    runtime_abort_common(adapter);
}
