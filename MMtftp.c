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
    int n_read;
    int fnbr=*(int *)handle;
    if(filesource[fnbr]==FATFSFILE)  f_read(FileTable[fnbr].fptr, buf, bytes, (UINT *)&n_read);
    else n_read=lfs_file_read(&lfs, FileTable[fnbr].lfsptr, buf, bytes);
    return n_read;
}
int tftp_write(void* handle, struct pbuf* p){
    int nbr;
    int fnbr=*(int *)handle;
    if(filesource[fnbr]==FATFSFILE) f_write(FileTable[fnbr].fptr, p->payload, p->tot_len, (UINT *)&nbr);
    else {
        nbr=FSerror=lfs_file_write(&lfs, FileTable[fnbr].lfsptr, p->payload, p->tot_len); 
    }
    if(FSerror>0)FSerror=0;
    ErrorCheck(tftp_fnbr);
    return nbr; 
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