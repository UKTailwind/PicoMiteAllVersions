/* Stub LittleFS for host build */
#ifndef LFS_H
#define LFS_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { int dummy; } lfs_t;
typedef struct { int dummy; } lfs_dir_t;
typedef struct { int dummy; } lfs_file_t;
struct lfs_info { int type; uint32_t size; char name[256]; };
#define LFS_ERR_OK 0
#define LFS_O_RDONLY 1
#define LFS_O_WRONLY 2
#define LFS_O_RDWR   3
#define LFS_O_CREAT  0x100
#define LFS_O_TRUNC  0x200
#define LFS_O_APPEND 0x400
#define LFS_TYPE_REG 1
#define LFS_TYPE_DIR 2
#endif
