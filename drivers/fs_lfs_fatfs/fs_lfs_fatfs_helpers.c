/*
 * drivers/fs_lfs_fatfs/fs_lfs_fatfs_helpers.c
 *
 * Drive-aware file existence / file size helpers for ports whose B: drive
 * is FatFS-backed SD and A: drive is littlefs-backed flash. This includes
 * every RP2040/RP2350 PicoMite variant and the ESP32-S3 Metro port.
 *
 * Host ports (host_native, host_wasm, mmbasic_stdio, mmbasic_ansi) have
 * their own POSIX-backed implementations of these symbols in
 * ports/host_native/host_fs_shims.c, so they don't link this TU. pc386
 * has a FatFs-only port and provides its own equivalents in
 * ports/pc386/pc386_peripheral_stubs.c.
 *
 * Picking the right TU is a build-system decision (CMake / Makefile),
 * NOT a preprocessor decision. Shared MMBasic code (mm_misc_shared.c)
 * must not contain target-macro ifdefs for SD-routing.
 *
 * Behaviour: the dispatch reads the optional A:/B: prefix via
 * drivecheck(), then resolves the rest of the path with
 * getfullfilename(). FatFSFileSystem is stomped on temporarily to steer
 * the lfs_stat/f_stat branch and restored before return so we don't
 * surprise the caller. Editor.c's cmd_edit relies on ExistsFile telling
 * the truth for B: paths — otherwise it leaves its `p` pointer NULL and
 * loads an empty buffer.
 */

#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "ff.h"
#include "lfs.h"

extern lfs_t lfs;

int FileSize(char *p){
    char q[FF_MAX_LFN]={0};
    int retval=0;
    int waste=0, t=FatFSFileSystem+1;
    int localfilesystemsave=FatFSFileSystem;
    t = drivecheck(p,&waste);
    p+=waste;
    getfullfilename(p,q);
    FatFSFileSystem=t-1;
    if(FatFSFileSystem==0){
        struct lfs_info lfsinfo;
        memset(&lfsinfo,0,sizeof(DIR));
        FSerror = lfs_stat(&lfs, q, &lfsinfo);
        if(lfsinfo.type==LFS_TYPE_REG)retval= lfsinfo.size;
    } else {
        DIR djd;
        FILINFO fnod;
        memset(&djd,0,sizeof(DIR));
        memset(&fnod,0,sizeof(FILINFO));
        if(!InitSDCard()) return -1;
        FSerror = f_stat(q, &fnod);
        if(FSerror != FR_OK)iret=0;
        else if(!(fnod.fattrib & AM_DIR))retval=fnod.fsize;
    }
    FatFSFileSystem=localfilesystemsave;
    return retval;
}

int ExistsFile(char *p){
    char q[FF_MAX_LFN]={0};
    int retval=0;
    int waste=0, t=FatFSFileSystem+1;
    int localfilesystemsave=FatFSFileSystem;
    t = drivecheck(p,&waste);
    p+=waste;
    getfullfilename(p,q);
    FatFSFileSystem=t-1;
    if(FatFSFileSystem==0){
        struct lfs_info lfsinfo;
        memset(&lfsinfo,0,sizeof(DIR));
        FSerror = lfs_stat(&lfs, q, &lfsinfo);
        if(lfsinfo.type==LFS_TYPE_REG)retval= 1;
    } else {
        DIR djd;
        FILINFO fnod;
        memset(&djd,0,sizeof(DIR));
        memset(&fnod,0,sizeof(FILINFO));
        if(!InitSDCard()) return -1;
        FSerror = f_stat(q, &fnod);
        if(FSerror != FR_OK)iret=0;
        else if(!(fnod.fattrib & AM_DIR))retval=1;
    }
    FatFSFileSystem=localfilesystemsave;
    return retval;
}

int ExistsDir(char *p, char *q, int *filesystem){
    int ireturn=0;
    ireturn=0;
    int localfilesystemsave=FatFSFileSystem;
    int waste=0, t=FatFSFileSystem+1;
    t = drivecheck(p,&waste);
    p+=waste;
    getfullfilename(p,q);
    FatFSFileSystem=t-1;
    *filesystem=FatFSFileSystem;
    if(strcmp(q,"/")==0 || strcmp(q,"/.")==0 || strcmp(q,"/..")==0 ){FatFSFileSystem=localfilesystemsave;ireturn= 1; return ireturn;}
    if(FatFSFileSystem==0){
        struct lfs_info lfsinfo;
        memset(&lfsinfo,0,sizeof(DIR));
        FSerror = lfs_stat(&lfs, q, &lfsinfo);
        if(lfsinfo.type==LFS_TYPE_DIR)ireturn= 1;
    } else {
        DIR djd;
        FILINFO fnod;
        memset(&djd,0,sizeof(DIR));
        memset(&fnod,0,sizeof(FILINFO));
        if(q[strlen(q)-1]=='/')strcat(q,".");
        if(!InitSDCard()) {FatFSFileSystem=localfilesystemsave;ireturn= -1; return ireturn;}
        FSerror = f_stat(q, &fnod);
        if(FSerror != FR_OK)ireturn=0;
        else if((fnod.fattrib & AM_DIR))ireturn=1;
    }
    FatFSFileSystem=localfilesystemsave;
    return ireturn;
}
