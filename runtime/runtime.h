#ifndef MMBASIC_RUNTIME_RUNTIME_H
#define MMBASIC_RUNTIME_RUNTIME_H

#include <stdbool.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mm_runtime_key_kind {
    MM_RUNTIME_KEY_NONE,
    MM_RUNTIME_KEY_RAW_BYTE,
    MM_RUNTIME_KEY_MMBASIC_CODE
} mm_runtime_key_kind;

typedef struct mm_runtime_key {
    mm_runtime_key_kind kind;
    int value;
} mm_runtime_key;

typedef struct mm_runtime_adapter {
    const char *name;
    unsigned flags;

    void (*early_console_init)(void);
    void (*platform_pre_options)(void);
    void (*platform_validate_or_reset_options)(void);
    void (*platform_apply_option_defaults)(void);
    void (*platform_after_options)(void);

    void (*memory_backing_init)(void);
    void (*timebase_init)(void);
    uint64_t (*time_us)(void);
    void (*sleep_us)(uint32_t us);

    mm_runtime_key (*console_read_key_nonblock)(void);
    mm_runtime_key (*console_read_key_blocking)(uint32_t timeout_ms);
    void (*console_write)(char c, int flush);
    void (*console_drain)(void);

    void (*display_console_init)(void);
    void (*keyboard_init)(void);
    void (*audio_init)(void);
    void (*filesystem_init)(void);
    void (*network_service)(int mode);
    void (*background_service)(void);

    int  (*interrupt_pending)(unsigned char **target);
    void (*before_prompt_loop)(void);
    void (*after_load_program)(void);
    void (*soft_reset)(void);
    void (*fatal_fault)(const char *message);
} mm_runtime_adapter;

typedef struct mm_runtime_console_adapter {
    const char *name;
    unsigned flags;

    void (*service)(void);
    int (*scripted_key)(void);
    int (*sim_key)(void);

    int (*raw_mode_active)(void);
    int (*read_byte_nonblock)(void);
    int (*read_byte_blocking_ms)(int ms);
    void (*push_back_byte)(int c);
    void (*sleep_us)(uint32_t us);

    int (*repl_mode)(void);
    void (*on_ctrl_d)(void);

    void (*display_putc)(char c);
    char (*serial_putc)(char c, int flush);
    void (*telnet_putc)(int c, int flush);
    void (*stdout_flush)(void);
    void (*raw_write)(const char *text, int len);
} mm_runtime_console_adapter;

#define MM_RUNTIME_CONSOLE_FLAG_KEEP_STDIN_LF  (1u << 0)

void mmbasic_runtime_console_set_adapter(const mm_runtime_console_adapter *adapter);
const mm_runtime_console_adapter *mmbasic_runtime_console_get_adapter(void);
void mmbasic_runtime_console_print_raw(const char *text, int len);
int mmbasic_runtime_console_decode_escape_sequence(void);

/*
 * Common boot/run flags. Source tokenisation flags intentionally occupy the
 * low bits below; boot/run sequencing flags start higher so run_source can
 * accept both in one unsigned value.
 */
#define MMBASIC_RUNTIME_INIT_FLAG_LOAD_OPTIONS   (1u << 8)
#define MMBASIC_RUNTIME_INIT_FLAG_INIT_BASIC     (1u << 9)
#define MMBASIC_RUNTIME_INIT_FLAG_INIT_HEAP      (1u << 10)
#define MMBASIC_RUNTIME_INIT_FLAG_CLEAR_ERROR    (1u << 11)
#define MMBASIC_RUNTIME_INIT_FLAG_CLEAR_RUNTIME  (1u << 12)

#define MMBASIC_RUNTIME_RUN_FLAG_CLEAR_RUNTIME   (1u << 16)
#define MMBASIC_RUNTIME_RUN_FLAG_RESET_ERROR     (1u << 17)
#define MMBASIC_RUNTIME_RUN_FLAG_PREPARE_PROGRAM (1u << 18)

int mmbasic_runtime_init_common(const mm_runtime_adapter *port, unsigned flags);
int mmbasic_runtime_run_source(const mm_runtime_adapter *port,
                               const char *source,
                               unsigned flags);
void mmbasic_runtime_enter_repl(const mm_runtime_adapter *port, unsigned flags);

/* Compatibility names from the initial runtime-spine sketch. */
int mmbasic_runtime_boot(const mm_runtime_adapter *port);
void mmbasic_runtime_repl(const mm_runtime_adapter *port, unsigned flags);

#define MMBASIC_SOURCE_FLAG_CONTINUATION_LINES    (1u << 0)
#define MMBASIC_SOURCE_FLAG_CLEAR_PROGMEM         (1u << 1)
#define MMBASIC_SOURCE_FLAG_ERASED_TAIL_SENTINEL  (1u << 2)
#define MMBASIC_SOURCE_FLAGS_ALL \
    (MMBASIC_SOURCE_FLAG_CONTINUATION_LINES | \
     MMBASIC_SOURCE_FLAG_CLEAR_PROGMEM | \
     MMBASIC_SOURCE_FLAG_ERASED_TAIL_SENTINEL)

#define MMBASIC_SOURCE_FLAGS_HOST_LOAD \
    (MMBASIC_SOURCE_FLAG_CONTINUATION_LINES | \
     MMBASIC_SOURCE_FLAG_CLEAR_PROGMEM | \
     MMBASIC_SOURCE_FLAG_ERASED_TAIL_SENTINEL)

#define MMBASIC_SOURCE_FLAGS_BATCH_LOAD \
    (MMBASIC_SOURCE_FLAG_CLEAR_PROGMEM | \
     MMBASIC_SOURCE_FLAG_ERASED_TAIL_SENTINEL)

int mmbasic_tokenise_source_to_progmem(const char *source, unsigned flags);
int mmbasic_save_loaded_source(const char *source, unsigned flags);

typedef void (*mmbasic_runtime_service_fn)(void);
typedef void (*mmbasic_runtime_clear_vars_fn)(int level, bool keep);

typedef void (*mmbasic_runtime_abort_hook_fn)(void);

typedef struct mmbasic_runtime_abort_adapter {
    mmbasic_runtime_service_fn service;
    volatile int *abort_flag;
    unsigned flags;
    mmbasic_runtime_abort_hook_fn before_abort;
    mmbasic_runtime_abort_hook_fn after_poll;
} mmbasic_runtime_abort_adapter;

#define MMBASIC_RUNTIME_ABORT_FLAG_CHECK_ABORT     (1u << 0)
#define MMBASIC_RUNTIME_ABORT_FLAG_DO_END_LONGJMP  (1u << 1)

void mmbasic_runtime_poll_service(mmbasic_runtime_service_fn service);
void mmbasic_runtime_poll_service_once(int *active,
                                       mmbasic_runtime_service_fn service);
void mmbasic_runtime_checkabort_poll(mmbasic_runtime_service_fn service);
void mmbasic_runtime_routinechecks_poll(mmbasic_runtime_service_fn service);
bool mmbasic_runtime_abort_requested(volatile int *abort_flag);
void mmbasic_runtime_checkabort(const mmbasic_runtime_abort_adapter *adapter);
void mmbasic_runtime_routinechecks(const mmbasic_runtime_abort_adapter *adapter);

void mmbasic_runtime_interrupt_save_error_state(
    int *saved_option_error_skip,
    char *saved_error_message,
    size_t saved_error_message_size,
    int *saved_errno,
    int *option_error_skip,
    char *error_message,
    int *errno_value);

void mmbasic_runtime_interrupt_restore_error_state(
    int saved_option_error_skip,
    const char *saved_error_message,
    int saved_errno,
    int *option_error_skip,
    char *error_message,
    int *errno_value);

void mmbasic_runtime_interrupt_leave_state(
    unsigned char **nextstmt_slot,
    unsigned char **interrupt_return_slot,
    int *local_index,
    mmbasic_runtime_clear_vars_fn clear_vars,
    bool *temp_memory_changed,
    char *current_interrupt_name);

unsigned char *mmbasic_runtime_interrupt_prepare_sub_return(
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
    int *local_index);

static inline void mmbasic_runtime_clear_post_load_input(unsigned char *input_buffer,
                                                         size_t input_buffer_size)
{
    if (input_buffer && input_buffer_size) memset(input_buffer, 0, input_buffer_size);
}

static inline void mmbasic_runtime_post_load_longjmp(unsigned char *input_buffer,
                                                     size_t input_buffer_size,
                                                     jmp_buf prompt_mark)
{
    mmbasic_runtime_clear_post_load_input(input_buffer, input_buffer_size);
    longjmp(prompt_mark, 1);
}

/* Current name for each port's local runtime/display-console setup hook. */
void mmbasic_runtime_port_begin(void);

#ifdef __cplusplus
}
#endif

#endif
