#include "runtime/runtime.h"

#include <string.h>

#if defined(__has_include)
#if __has_include("pico.h")
#include "pico.h"
#elif __has_include("pico/platform.h")
#include "pico/platform.h"
#endif
#endif

#include "MMBasic_Includes.h"
#include "MM_Misc.h"
#include "Memory.h"
#include "shared/net/mm_net_interrupts.h"

#ifndef MMB_HOT_FUNC
#define MMB_HOT_FUNC(name) name
#endif

extern char *WAVInterrupt;
extern bool WAVcomplete;

void MMB_HOT_FUNC(mmbasic_runtime_interrupt_save_error_state)(
    int *saved_option_error_skip,
    char *saved_error_message,
    size_t saved_error_message_size,
    int *saved_errno,
    int *option_error_skip,
    char *error_message,
    int *errno_value)
{
    if (saved_option_error_skip) {
        *saved_option_error_skip = option_error_skip && *option_error_skip > 0
            ? *option_error_skip
            : 0;
    }
    if (option_error_skip) *option_error_skip = 0;
    if (saved_error_message && saved_error_message_size && error_message) {
        strncpy(saved_error_message, error_message, saved_error_message_size - 1);
        saved_error_message[saved_error_message_size - 1] = '\0';
    }
    if (saved_errno && errno_value) *saved_errno = *errno_value;
    if (error_message) *error_message = '\0';
    if (errno_value) *errno_value = 0;
}

void MMB_HOT_FUNC(mmbasic_runtime_interrupt_restore_error_state)(
    int saved_option_error_skip,
    const char *saved_error_message,
    int saved_errno,
    int *option_error_skip,
    char *error_message,
    int *errno_value)
{
    if (option_error_skip) {
        *option_error_skip = saved_option_error_skip > 0
            ? saved_option_error_skip + 1
            : saved_option_error_skip;
    }
    if (error_message && saved_error_message) strcpy(error_message, saved_error_message);
    if (errno_value) *errno_value = saved_errno;
}

void MMB_HOT_FUNC(mmbasic_runtime_interrupt_leave_state)(
    unsigned char **nextstmt_slot,
    unsigned char **interrupt_return_slot,
    int *local_index,
    mmbasic_runtime_clear_vars_fn clear_vars,
    bool *temp_memory_changed,
    char *current_interrupt_name)
{
    if (nextstmt_slot && interrupt_return_slot) {
        *nextstmt_slot = *interrupt_return_slot;
        *interrupt_return_slot = NULL;
    }
    if (local_index && *local_index && clear_vars) clear_vars((*local_index)--, true);
    if (temp_memory_changed) *temp_memory_changed = true;
    if (current_interrupt_name) *current_interrupt_name = '\0';
}

/*
 * Shared check_interrupt / cmd_ireturn for any port whose dispatch
 * surface follows the host_native / esp32 shape. Encapsulates the
 * "drain TCP/MQTT/UDP flags → save error state → cmdSUB trampoline"
 * logic that previously had two near-verbatim copies. Pico's
 * check_interrupt is a separate, more elaborate function (see
 * MM_Misc.c:checkdetailinterrupts) and does not use this path; pc386
 * stubs both functions and also does not use this path.
 *
 * The audit found host_native was missing the `udp_pending` hook the
 * ESP32 had; the dispatch adapter exposes the slot so any port can
 * plumb a non-blocking pending check alongside the UDPreceive flag.
 */
int MMB_HOT_FUNC(mmbasic_runtime_check_interrupt)(
    const mmbasic_runtime_interrupt_dispatch_adapter *adapter)
{
    if (!adapter) return 0;
    if (adapter->service) adapter->service();
    if (!InterruptUsed) return 0;
    if (InterruptReturn != NULL) return 0;

    unsigned char *intaddr = NULL;
    if (TCPreceiveInterrupt && (TCPreceived ||
        (adapter->tcp_pending && adapter->tcp_pending()))) {
        intaddr = (unsigned char *)TCPreceiveInterrupt;
        TCPreceived = false;
    } else if (MQTTInterrupt && MQTTComplete) {
        intaddr = (unsigned char *)MQTTInterrupt;
        MQTTComplete = false;
    } else if (UDPinterrupt && (UDPreceive ||
        (adapter->udp_pending && adapter->udp_pending()))) {
        intaddr = (unsigned char *)UDPinterrupt;
        UDPreceive = false;
    } else if (WAVInterrupt != NULL && WAVcomplete) {
        intaddr = (unsigned char *)WAVInterrupt;
        WAVcomplete = false;
    } else {
        return 0;
    }

    g_LocalIndex++;
    mmbasic_runtime_interrupt_save_error_state(
        adapter->save_option_error_skip,
        adapter->save_error_message,
        adapter->save_error_message_size,
        adapter->save_errno,
        &OptionErrorSkip, MMErrMsg, &MMerrno);
    InterruptReturn = nextstmt;

    if (adapter->commandtbl_decode &&
        adapter->commandtbl_decode(intaddr) == cmdSUB) {
        if (gosubindex >= MAXGOSUB) error("Too many SUBs for interrupt");
        intaddr = mmbasic_runtime_interrupt_prepare_sub_return(
            cmdIRET, C_BASETOKEN, intaddr,
            CurrentInterruptName, MAXVARLEN, true,
            adapter->interrupt_return_token,
            adapter->interrupt_return_token_size,
            &gosubindex, errorstack, gosubstack, CurrentLinePtr,
            &g_LocalIndex);
    }

    nextstmt = intaddr;
    return 1;
}

void MMB_HOT_FUNC(mmbasic_runtime_cmd_ireturn)(
    const mmbasic_runtime_interrupt_dispatch_adapter *adapter)
{
    if (InterruptReturn == NULL) error("Not in interrupt");
    checkend(cmdline);
    mmbasic_runtime_interrupt_leave_state(&nextstmt, &InterruptReturn,
                                          &g_LocalIndex, ClearVars,
                                          &g_TempMemoryIsChanged,
                                          CurrentInterruptName);
    if (!adapter) return;
    int saved_skip = adapter->save_option_error_skip
                     ? *adapter->save_option_error_skip : 0;
    int saved_errno = adapter->save_errno ? *adapter->save_errno : 0;
    mmbasic_runtime_interrupt_restore_error_state(
        saved_skip,
        adapter->save_error_message,
        saved_errno,
        &OptionErrorSkip, MMErrMsg, &MMerrno);
}

unsigned char *MMB_HOT_FUNC(
    mmbasic_runtime_interrupt_prepare_sub_return)(
    unsigned int ireturn_token,
    unsigned int token_base,
    unsigned char *interrupt_addr,
    char *current_interrupt_name,
    size_t interrupt_name_copy_len,
    bool terminate_interrupt_name,
    char *return_token,
    size_t return_token_size,
    int *gosub_index,
    unsigned char **error_stack,
    unsigned char **gosub_stack,
    unsigned char *current_line_ptr,
    int *local_index)
{
    if (current_interrupt_name && interrupt_name_copy_len) {
        strncpy(current_interrupt_name, (const char *)interrupt_addr + 2,
                interrupt_name_copy_len);
        if (terminate_interrupt_name) {
            current_interrupt_name[interrupt_name_copy_len] = '\0';
        }
    }
    if (return_token && return_token_size >= 2) {
        return_token[0] = (char)((ireturn_token & 0x7f) + token_base);
        return_token[1] = (char)((ireturn_token >> 7) + token_base);
        if (return_token_size > 2) return_token[2] = '\0';
    }
    if (gosub_index && error_stack && gosub_stack) {
        error_stack[*gosub_index] = current_line_ptr;
        gosub_stack[(*gosub_index)++] = (unsigned char *)return_token;
    }
    if (local_index) (*local_index)++;
    while (*interrupt_addr) interrupt_addr++;
    return interrupt_addr;
}
