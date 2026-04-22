#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "lwip/apps/tftp_common.h"
#include "lwip/apps/tftp_server.h"
struct tftp_context ctx;
int tftp_fnbr;
void* tftp_open(const char* fname, const char* fmode, u8_t write){
    if (!InitSDCard()) return NULL;
    BYTE mode = 0;
    tftp_fnbr=FindFreeFileNbr();
    if (write){
        mode = FA_WRITE | FA_CREATE_ALWAYS;
        if(!optionsuppressstatus)MMPrintString("TFTP request to create ");
    }
    else {
        mode = FA_READ;
        if(!optionsuppressstatus)MMPrintString("TFTP request to read ");
    }
    if(!optionsuppressstatus)MMPrintString(strcmp(fmode,"octet")==0? "binary file : " : "ascii file : ");
    if(!optionsuppressstatus)MMPrintString((char *)fname);
    if(!optionsuppressstatus)PRet();
    if (!BasicFileOpen((char *)fname, tftp_fnbr, mode))
            return NULL;
    return &tftp_fnbr;
}

void tftp_close(void* handle){
//    int nbr;
    int fnbr=*(int *)handle;
    FileClose(fnbr);
    if(!optionsuppressstatus)MMPrintString("TFTP transfer complete\r\n");
}
int tftp_read(void* handle, void* buf, int bytes){
    int fnbr=*(int *)handle;
    ssize_t n = hal_fs_read(hal_fds[fnbr], buf, (size_t)bytes);
    return n < 0 ? 0 : (int)n;
}
int tftp_write(void* handle, struct pbuf* p){
    int fnbr=*(int *)handle;
    ssize_t w = hal_fs_write(hal_fds[fnbr], p->payload, p->tot_len);
    FSerror = (w < 0) ? (int)w : 0;
    ErrorCheck(tftp_fnbr);
    return w < 0 ? (int)w : (int)w;
}
void tftp_error(void* handle, int err, const char* msg, int size){
    int fnbr=*(int *)handle;
    ForceFileClose(fnbr);
    MMPrintString("TFTP Error: ");
    MMPrintString((char *)msg);
    PRet();
}
int cmd_tftp_server_init(void){
    ctx.open=tftp_open;
    ctx.close=tftp_close;
    ctx.error=tftp_error;
    ctx.write=tftp_write;
    ctx.read=tftp_read;
    tftp_init_server(&ctx);
    return 1;
}