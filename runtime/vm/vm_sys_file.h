#ifndef __VM_SYS_FILE_H
#define __VM_SYS_FILE_H

#include <stdint.h>

#define VM_FILE_MODE_INPUT  1
#define VM_FILE_MODE_OUTPUT 2
#define VM_FILE_MODE_APPEND 3
#define VM_FILE_MODE_RANDOM 4

void vm_sys_file_open(const char *filename, int fnbr, int mode);
void vm_sys_file_close(int fnbr);
void vm_sys_file_reset(void);
void vm_sys_file_print_buf(int fnbr, const char *buf, int len);
void vm_sys_file_print_str(int fnbr, const uint8_t *mstr);
void vm_sys_file_print_newline(int fnbr);
void vm_sys_file_line_input(int fnbr, uint8_t *dest);
int vm_sys_file_eof(int fnbr);
int vm_sys_file_lof(int fnbr);
int vm_sys_file_getc(int fnbr);
#ifdef MMBASIC_HOST
void vm_sys_file_host_resolve_path(const char *filename, char *path, int path_size);
#endif
void vm_sys_file_drive(const char *drive);
void vm_sys_file_seek(int fnbr, int position);
void vm_sys_file_mkdir(const char *path);
void vm_sys_file_chdir(const char *path);
void vm_sys_file_rmdir(const char *path);
void vm_sys_file_kill(const char *path);
void vm_sys_file_rename(const char *old_name, const char *new_name);
void vm_sys_file_copy(const char *from_name, const char *to_name, int mode);

#endif
