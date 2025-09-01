/***********************************************************************************************************************
PicoMite MMBasic

MMtftp.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "lwip/apps/tftp_common.h"
#include "lwip/apps/tftp_server.h"
struct tftp_context ctx;
int tftp_fnbr;
void *tftp_open(const char *fname, const char *fmode, u8_t write)
{
    if (!InitSDCard())
        return NULL;
    BYTE mode = 0;
    tftp_fnbr = FindFreeFileNbr();
    if (write)
    {
        mode = FA_WRITE | FA_CREATE_ALWAYS;
        if (!optionsuppressstatus)
            MMPrintString("TFTP request to create ");
    }
    else
    {
        mode = FA_READ;
        if (!optionsuppressstatus)
            MMPrintString("TFTP request to read ");
    }
    if (!optionsuppressstatus)
        MMPrintString(strcmp(fmode, "octet") == 0 ? "binary file : " : "ascii file : ");
    if (!optionsuppressstatus)
        MMPrintString((char *)fname);
    if (!optionsuppressstatus)
        PRet();
    if (!BasicFileOpen((char *)fname, tftp_fnbr, mode))
        return NULL;
    return &tftp_fnbr;
}

void tftp_close(void *handle)
{
    //    int nbr;
    int fnbr = *(int *)handle;
    FileClose(fnbr);
    if (!optionsuppressstatus)
        MMPrintString("TFTP transfer complete\r\n");
}
int tftp_read(void *handle, void *buf, int bytes)
{
    int n_read;
    int fnbr = *(int *)handle;
    if (filesource[fnbr] == FATFSFILE)
        f_read(FileTable[fnbr].fptr, buf, bytes, (UINT *)&n_read);
    else
        n_read = lfs_file_read(&lfs, FileTable[fnbr].lfsptr, buf, bytes);
    return n_read;
}
int tftp_write(void *handle, struct pbuf *p)
{
    int nbr;
    int fnbr = *(int *)handle;
    if (filesource[fnbr] == FATFSFILE)
        f_write(FileTable[fnbr].fptr, p->payload, p->tot_len, (UINT *)&nbr);
    else
    {
        nbr = FSerror = lfs_file_write(&lfs, FileTable[fnbr].lfsptr, p->payload, p->tot_len);
    }
    if (FSerror > 0)
        FSerror = 0;
    ErrorCheck(tftp_fnbr);
    return nbr;
}
void tftp_error(void *handle, int err, const char *msg, int size)
{
    int fnbr = *(int *)handle;
    ForceFileClose(fnbr);
    MMPrintString("TFTP Error: ");
    MMPrintString((char *)msg);
    PRet();
}
int cmd_tftp_server_init(void)
{
    ctx.open = tftp_open;
    ctx.close = tftp_close;
    ctx.error = tftp_error;
    ctx.write = tftp_write;
    ctx.read = tftp_read;
    tftp_init_server(&ctx);
    return 1;
}