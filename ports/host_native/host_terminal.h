#ifndef HOST_TERMINAL_H
#define HOST_TERMINAL_H

#ifdef __cplusplus
extern "C" {
#endif

void host_raw_mode_enter(void);
int  host_raw_mode_is_active(void);
int  host_read_byte_nonblock(void);
int  host_read_byte_blocking_ms(int ms);
void host_push_back_byte(int c);

/* Query the current terminal size. Returns 0 on success and writes rows/cols;
 * returns -1 if size can't be determined. */
int host_terminal_get_size(int *rows, int *cols);

#ifdef __cplusplus
}
#endif

#endif
