/***********************************************************************************************************************
PicoMite MMBasic

XModem.c

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
/**
 * @file XModem.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for the MMBasic XMODEM command
 */
/**
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

// Protocol constants
#define SOH 0x01  // Start of 128-byte block
#define STX 0x02  // Start of 1024-byte block
#define EOT 0x04  // End of transmission
#define ACK 0x06  // Acknowledge
#define NAK 0x15  // Not acknowledge
#define CAN 0x18  // Cancel
#define CRC16 'C' // Request CRC-16 mode

#define PACKET_SIZE_128 128
#define PACKET_SIZE_1024 1024
#define PACKET_TIMEOUT 3000 // 3 seconds
#define YMAXRETRANS 25
#if defined(rp2350) && !defined(USBKEYBOARD)
#define XMODEMBUFFERSIZE EDIT_BUFFER_SIZE - 8192
#else
#define XMODEMBUFFERSIZE EDIT_BUFFER_SIZE
#endif
// Calculate CRC-16 (CCITT)
static unsigned short crc16_ccitt_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0};

void xmodemTransmit(char *p, int fnbr, long data_size);
void xmodemReceive(char *sp, int maxbytes, int fnbr, int crunch);
#if defined(rp2350) && !defined(USBKEYBOARD)
void ymodemTransmit(char *p, int fnbr, char *filename, long file_size);
void ymodemReceive(char *sp, int maxbytes, int fnbr, int crunch);
#endif
int FindFreeFileNbr(void);
bool rcvnoint;

/*  @endcond */

void MIPS16 cmd_xmodem(void)
{
    char *buf, BreakKeySave, *p, *fromp;
    int rcv = 0, fnbr, crunch = false;
    char *fname;
#if defined(rp2350) && !defined(USBKEYBOARD)
    bool xmodem = true;
    if (cmdtoken == GetCommandValue((unsigned char *)"YModem"))
        xmodem = false;
#endif
    ClearExternalIO();
    if (mytoupper(*cmdline) == 'R')
        rcv = true;
    else if (mytoupper(*cmdline) == 'S')
        rcv = false;
    else if (mytoupper(*cmdline) == 'C')
        crunch = rcv = true;
    else
        SyntaxError();
    ;
    while (isalpha(*cmdline))
        cmdline++; // find the filename (if it is there)
    skipspace(cmdline);

    BreakKeySave = BreakKey;
    BreakKey = 0;

    if (*cmdline == 0 || *cmdline == '\'')
    {
        // no file name, so this is a transfer to/from program memory
        if (CurrentLinePtr)
            StandardError(10);
        if (Option.DISPLAY_TYPE >= VIRTUAL && WriteBuf)
            FreeMemorySafe((void **)&WriteBuf);
        if (rcv)
            ClearProgram(true); // we need all the RAM
        else
        {
            closeframebuffer('A');
            CloseAudio(1);
            ClearVars(0, true);
        }
        buf = GetTempMemory(XMODEMBUFFERSIZE);
        if (rcv)
        {
#if defined(rp2350) && !defined(USBKEYBOARD)
            if (!xmodem)
                ymodemReceive(buf, XMODEMBUFFERSIZE, 0, crunch);
            else
#endif
                xmodemReceive(buf, XMODEMBUFFERSIZE, 0, crunch);
            ClearSavedVars(); // clear any saved variables
            SaveProgramToFlash((unsigned char *)buf, true);
        }
        else
        {
            int nbrlines = 0;
            // we must copy program memory into RAM expanding tokens as we go
            fromp = (char *)ProgMemory;
            p = buf; // the RAM buffer
#if defined(rp2350) && !defined(USBKEYBOARD)
            char ymodemname[FF_MAX_LFN] = {0};
#endif
            while (1)
            {
                if (*fromp == T_NEWLINE)
                {
                    fromp = (char *)llist((unsigned char *)p, (unsigned char *)fromp); // expand the line into the buffer
                    nbrlines++;
                    if (!(nbrlines == 1 && p[0] == '\'' && p[1] == '#'))
                    {
                        p += strlen(p);
                        if ((p - buf) > (XMODEMBUFFERSIZE - STRINGSIZE))
                            StandardError(29);
                        *p++ = '\n';
                        *p = 0; // terminate that line
                    }
#if defined(rp2350) && !defined(USBKEYBOARD)
                    else if (!xmodem && nbrlines == 1 && p[0] == '\'' && p[1] == '#') // we can use the filename for the ymodem transfer name
                    {
                        strcpy(ymodemname, &p[4]);
                    }
#endif
                }
                if (fromp[0] == 0 || fromp[0] == 0xff)
                    break; // finally, is it the end of the program?
            }
            --p;
            *p = 0; // erase the last line terminator
#if defined(rp2350) && !defined(USBKEYBOARD)
            if (!xmodem)
                ymodemTransmit(buf, 0, ymodemname[0] ? ymodemname : NULL, 0); // send it off
            else
#endif
                xmodemTransmit(buf, 0, 0); // send it off
        }
    }
    else
    {
        // this is a transfer to/from the SD card
        if (crunch)
            error("Invalid command");
        if (!InitSDCard())
            return;
        fnbr = FindFreeFileNbr();
        fname = (char *)getFstring(cmdline); // get the file name
        if (Option.SerialConsole)
        {
            rcvnoint = true;
            uart_set_irq_enables((Option.SerialConsole & 3) == 1 ? uart0 : uart1, false, false);
        }
        else
            rcvnoint = false;
        if (rcv)
        {
            if (!BasicFileOpen(fname, fnbr, FA_WRITE | FA_CREATE_ALWAYS))
                return;
#if defined(rp2350) && !defined(USBKEYBOARD)
            if (!xmodem)
                ymodemReceive(NULL, 0, fnbr, false);
            else
#endif
                xmodemReceive(NULL, 0, fnbr, false);
            if (rcvnoint)
                uart_set_irq_enables((Option.SerialConsole & 3) == 1 ? uart0 : uart1, true, false);
        }
        else
        {
            if (!BasicFileOpen(fname, fnbr, FA_READ))
                return;
            int fsize;
            if (filesource[fnbr] != FLASHFILE)
                fsize = f_size(FileTable[fnbr].fptr);
            else
                fsize = lfs_file_size(&lfs, FileTable[fnbr].lfsptr);
#if defined(rp2350) && !defined(USBKEYBOARD)
            if (xmodem)
                ymodemTransmit(NULL, fnbr, fname, fsize);
            else
#endif
                xmodemTransmit(NULL, fnbr, fsize);
            if (rcvnoint)
                uart_set_irq_enables((Option.SerialConsole & 3) == 1 ? uart0 : uart1, true, false);
        }
        FileClose(fnbr);
    }
    BreakKey = BreakKeySave;
    cmdline = NULL;
    do_end(false);
    longjmp(mark, 1); // jump back to the input prompt
}

/*
 * @cond
 * The following section will be excluded from the documentation.
 */

int __not_in_flash_func(_inbyte)(int timeout)
{
    int c;
    uint64_t timer = time_us_64() + timeout * 1000;
    if (!rcvnoint)
    {
        while (time_us_64() < timer)
        {
            c = getConsole();
            if (c != -1)
            {
                return c;
            }
        }
    }
    else
    {
        while (time_us_64() < timer && !uart_is_readable((Option.SerialConsole & 3) == 1 ? uart0 : uart1))
        {
        }
        if (time_us_64() < timer)
            return uart_getc((Option.SerialConsole & 3) == 1 ? uart0 : uart1);
    }
    return -1;
}
char _outbyte(char c, int f)
{
    if (!rcvnoint)
        SerialConsolePutC(c, f);
    else
        uart_putc_raw((Option.SerialConsole & 3) == 1 ? uart0 : uart1, c);
    return c;
}

// for the MX470 we don't want any XModem data echoed to the LCD panel
// #define _outbyte(c,d) SerialConsolePutC(c,d)

// YMODEM Protocol Implementation
// Simple, fast, and reliable file transfer protocol
//
// IMPORTANT: For CDC/USB connections, ACK characters must be transmitted
// immediately for proper flow control. The _outbyte() function with f=1
// parameter should flush both stdio AND the USB layer (tud_cdc_write_flush()).
// Without this, ACKs may be buffered in USB packets causing timeouts and
// retransmissions.

// YMODEM Protocol Implementation
// Simple, fast, and reliable file transfer protocol
//
// IMPORTANT: For CDC/USB connections, ACK characters must be transmitted
// immediately for proper flow control. The _outbyte() function with f=1
// parameter should flush both stdio AND the USB layer (tud_cdc_write_flush()).
// Without this, ACKs may be buffered in USB packets causing timeouts and
// retransmissions.
//
// PERFORMANCE: Uses write buffering (8KB) to minimize slow disk write operations.
// This prevents timeouts during long file transfers.

unsigned short crc16_ccitt(unsigned char *buf, int len)
{
    unsigned short crc = 0;
    int i;

    for (i = 0; i < len; i++)
    {
        crc = (crc << 8) ^ crc16_ccitt_table[((crc >> 8) ^ buf[i]) & 0xFF];
    }
    return crc;
}
#if defined(rp2350) && !defined(USBKEYBOARD)
// Parse file size from YMODEM header
long parse_file_size(unsigned char *data)
{
    // Format: "filename\0size rest..."
    // Skip filename
    int i = 0;
    while (i < 128 && data[i] != 0)
        i++;
    i++; // Skip null

    if (i >= 128)
        return 0;

    // Parse decimal number
    long size = 0;
    while (i < 128 && data[i] >= '0' && data[i] <= '9')
    {
        size = size * 10 + (data[i] - '0');
        i++;
    }
    return size;
}

// Receive a YMODEM/XMODEM packet
// Returns: packet size on success, 0 on error, -1 on EOT, -2 on CAN
int receive_packet(unsigned char *buf, int *packet_num, int use_crc)
{
    int c;
    unsigned char seq, seq_comp;
    int packet_size;
    unsigned short crc_recv, crc_calc;
    unsigned char csum_recv, csum_calc;
    int i;

    // Get packet start (SOH or STX)
    c = _inbyte(PACKET_TIMEOUT);
    if (c < 0)
        return 0; // Timeout

    if (c == EOT)
        return -1; // End of transmission
    if (c == CAN)
    {
        // Check for second CAN
        if (_inbyte(1000) == CAN)
            return -2; // Cancelled
        return 0;      // False alarm
    }

    if (c == SOH)
    {
        packet_size = PACKET_SIZE_128;
    }
    else if (c == STX)
    {
        packet_size = PACKET_SIZE_1024;
    }
    else
    {
        return 0; // Invalid start
    }

    // Get sequence number
    if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
        return 0;
    seq = c;

    // Get complement of sequence number
    if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
        return 0;
    seq_comp = c;

    // Verify sequence numbers are complements
    if (seq != (unsigned char)(~seq_comp))
    {
        return 0; // Sequence error
    }

    // Read data
    for (i = 0; i < packet_size; i++)
    {
        if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
            return 0;
        buf[i] = c;
    }

    // Read and verify checksum/CRC
    if (use_crc)
    {
        // CRC-16
        if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
            return 0;
        crc_recv = (c << 8);
        if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
            return 0;
        crc_recv |= c;

        crc_calc = crc16_ccitt(buf, packet_size);
        if (crc_calc != crc_recv)
        {
            return 0; // CRC error
        }
    }
    else
    {
        // Simple checksum
        if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
            return 0;
        csum_recv = c;

        csum_calc = 0;
        for (i = 0; i < packet_size; i++)
        {
            csum_calc += buf[i];
        }
        if (csum_calc != csum_recv)
        {
            return 0; // Checksum error
        }
    }

    *packet_num = seq;
    return packet_size; // Return packet size on success
}

// YMODEM receive function
void ymodemReceive(char *sp, int maxbytes, int fnbr, int crunch)
{
    unsigned char buf[PACKET_SIZE_1024];
    unsigned char *write_buffer = GetTempMemory(PACKET_SIZE_1024 * 8); // Buffer 8 packets (8KB) before writing
    int write_buffer_used = 0;
    int packet_num;
    int expected_seq = 0;
    int retry;
    int result;
    int use_crc = 1; // Always use CRC-16 for YMODEM
    long file_size = 0;
    long bytes_received = 0;
    int receiving_file = 0;

    CrunchData((unsigned char **)&sp, 0); // Initialize crunch if needed

    // Wait for sender - use NAK instead of 'C' to avoid printing
    // Most YMODEM senders (including TeraTerm) will use CRC anyway
    for (retry = 0; retry < 10; retry++)
    {
        // Send NAK (non-printing) instead of 'C'
        _outbyte(NAK, 1);

        // Wait to see if sender responds
        result = receive_packet(buf, &packet_num, use_crc);
        if (result > 0)
            break; // Got a packet

        if (retry == 5)
        {
            // Try fallback to checksum mode
            use_crc = 0;
        }
    }

    if (retry >= 10)
    {
        error("Sender did not respond");
        return;
    }

    // First packet should be header (packet 0)
    if (packet_num != 0)
    {
        _outbyte(CAN, 0);
        _outbyte(CAN, 1);
        error("Expected header packet");
        return;
    }

    // Parse file info from header
    if (buf[0] != 0)
    { // Not an empty header
        file_size = parse_file_size(buf);
        receiving_file = 1;
    }

    // ACK the header
    _outbyte(ACK, 0);
    _outbyte(CRC16, 1); // Ready for first data packet

    expected_seq = 1;

    // Main receive loop
    while (receiving_file)
    {
        result = receive_packet(buf, &packet_num, use_crc);

        if (result == -1)
        {
            // EOT - end of file
            _outbyte(NAK, 1); // NAK the first EOT

            // Wait for second EOT
            result = receive_packet(buf, &packet_num, use_crc);
            if (result == -1)
            {
                _outbyte(ACK, 1); // ACK the second EOT

                // Terminate data if saving to memory
                if (sp != NULL && maxbytes > 0)
                {
                    *sp = 0;
                }
                else if (write_buffer_used > 0)
                {
                    // Flush any remaining buffered data to file
                    FilePutdata((char *)write_buffer, fnbr, write_buffer_used);
                    write_buffer_used = 0;
                }

                // Send 'C' to see if there are more files
                _outbyte(CRC16, 1);

                // Wait for next header or final EOT
                result = receive_packet(buf, &packet_num, use_crc);
                if (result == -1)
                {
                    // Final EOT - all done
                    _outbyte(ACK, 1);
                    return;
                }
                else if (result > 0 && packet_num == 0)
                {
                    // Another file header
                    if (buf[0] == 0)
                    {
                        // Empty header means end of batch
                        _outbyte(ACK, 1);
                        return;
                    }
                    // Could handle another file here
                    _outbyte(ACK, 1);
                    return; // For now, just accept one file
                }
            }
            return;
        }
        else if (result == -2)
        {
            // Cancelled by sender
            error("Cancelled by remote");
            return;
        }
        else if (result == 0)
        {
            // Timeout or error
            _outbyte(NAK, 1);
            if (++retry > YMAXRETRANS)
            {
                _outbyte(CAN, 0);
                _outbyte(CAN, 1);
                error("Too many errors");
                return;
            }
            continue;
        }

        // Check sequence number
        if (packet_num != expected_seq)
        {
            // If it's the previous packet, sender didn't get our ACK
            if (packet_num == ((expected_seq - 1) & 0xFF))
            {
                _outbyte(ACK, 1); // Re-ACK it
                continue;
            }
            // Otherwise, sequence error
            _outbyte(NAK, 1);
            continue;
        }

        // Good packet received with correct sequence
        // ACK IMMEDIATELY to stop sender from timing out
        _outbyte(ACK, 1);
        expected_seq = (expected_seq + 1) & 0xFF;
        retry = 0; // Reset retry counter on success

        // NOW process data (this can be slow)
        int bytes_to_process = result; // Packet size (128 or 1024)

        // If we know the file size, don't process beyond it
        if (file_size > 0 && bytes_received + bytes_to_process > file_size)
        {
            bytes_to_process = file_size - bytes_received;
        }

        // Only save data if we haven't already received the full file
        if (file_size == 0 || bytes_received < file_size)
        {
            if (sp != NULL)
            {
                // Write to memory
                for (int i = 0; i < bytes_to_process; i++)
                {
                    if (--maxbytes > 0)
                    {
                        if (buf[i] == 0)
                            continue; // Skip nulls
                        if (crunch)
                            CrunchData((unsigned char **)&sp, buf[i]);
                        else
                            *sp++ = buf[i];
                    }
                }
            }
            else
            {
                // Write to file - buffer data to reduce write operations
                // Copy to write buffer
                memcpy(&write_buffer[write_buffer_used], buf, bytes_to_process);
                write_buffer_used += bytes_to_process;

                // Flush buffer if full (8KB accumulated)
                if (write_buffer_used >= PACKET_SIZE_1024 * 8)
                {
                    FilePutdata((char *)write_buffer, fnbr, write_buffer_used);
                    write_buffer_used = 0;
                }
            }

            bytes_received += bytes_to_process;

            // Check if we've received the complete file - flush remaining buffer
            if (file_size > 0 && bytes_received >= file_size && write_buffer_used > 0 && sp == NULL)
            {
                FilePutdata((char *)write_buffer, fnbr, write_buffer_used);
                write_buffer_used = 0;
            }
        }
    }
}

// YMODEM transmit function
// p: pointer to data in memory (NULL if sending from file)
// fnbr: file number if sending from file
// filename: base filename only (e.g., "data.txt"), path will be stripped
//           Use NULL or "" for default "FILE.DAT"
// file_size: exact size in bytes, 0 to auto-calculate from memory or file
void ymodemTransmit(char *p, int fnbr, char *filename, long file_size)
{
    unsigned char buf[PACKET_SIZE_1024];
    int packet_num = 0;
    int retry;
    int c;
    int use_crc = 0;
    long bytes_sent = 0;
    int use_1k = 1; // Use 1024-byte packets

    // Validate filename - use default if NULL or empty
    if (filename == NULL || filename[0] == 0)
    {
        filename = "FILE.DAT";
    }

    // If file_size not provided, try to determine it
    if (file_size == 0)
    {
        if (p != NULL)
        {
            // Sending from memory - calculate string length
            file_size = strlen(p);
        }
        else
        {
            // Sending from file - file_size should be provided by caller
            // If not provided, we'll send without size info (compatible but not ideal)
            file_size = 0;
        }
    }

    // Wait for receiver to request start (C or NAK)
    for (retry = 0; retry < 60; retry++)
    {
        c = _inbyte(1000);
        if (c == CRC16)
        {
            use_crc = 1;
            break;
        }
        else if (c == NAK)
        {
            use_crc = 0;
            break;
        }
        else if (c == CAN)
        {
            error("Cancelled by remote");
            return;
        }
    }

    if (retry >= 60)
    {
        error("Receiver did not respond");
        return;
    }

    // Send header packet (packet 0)
    memset(buf, 0, PACKET_SIZE_128);

    // Format: "filename\0size\0"
    // Only use basename, strip any path separators
    char *basename = filename;
    char *p_slash = strrchr(filename, '/');
    char *p_backslash = strrchr(filename, '\\');
    if (p_backslash > p_slash)
        basename = p_backslash + 1;
    else if (p_slash != NULL)
        basename = p_slash + 1;

    // Build header: filename\0size\0
    int offset = 0;
    while (*basename && offset < 127)
    {
        buf[offset++] = *basename++;
    }
    buf[offset++] = 0; // Null terminator

    // Add file size if known
    if (file_size > 0 && offset < 120)
    {
        sprintf((char *)&buf[offset], "%ld", file_size);
    }

    for (retry = 0; retry < YMAXRETRANS; retry++)
    {
        _outbyte(SOH, 0);
        _outbyte(0, 0);    // Packet number 0
        _outbyte(0xFF, 1); // Complement

        // Send data
        for (int i = 0; i < PACKET_SIZE_128; i++)
        {
            _outbyte(buf[i], 0);
        }
        // Send CRC or checksum
        if (use_crc)
        {
            unsigned short crc = crc16_ccitt(buf, PACKET_SIZE_128);
            _outbyte((crc >> 8) & 0xFF, 0);
            _outbyte(crc & 0xFF, 1);
        }
        else
        {
            unsigned char csum = 0;
            for (int i = 0; i < PACKET_SIZE_128; i++)
            {
                csum += buf[i];
            }
            _outbyte(csum, 1);
        }

        // Wait for ACK
        c = _inbyte(PACKET_TIMEOUT);
        if (c == ACK)
        {
            // Wait for 'C' to start data
            c = _inbyte(PACKET_TIMEOUT);
            if (c == CRC16 || c == NAK)
            {
                break;
            }
        }
        else if (c == CAN)
        {
            error("Cancelled by remote");
            return;
        }
    }

    if (retry >= YMAXRETRANS)
    {
        _outbyte(CAN, 0);
        _outbyte(CAN, 1);
        error("Header not acknowledged");
        return;
    }

    packet_num = 1;

    // Send data packets
    while (1)
    {
        int packet_size = use_1k ? PACKET_SIZE_1024 : PACKET_SIZE_128;
        int data_len = 0;

        // Check if we're already done before reading more
        if (bytes_sent >= file_size)
            break;

        // Read data
        memset(buf, 0x1A, packet_size); // Fill with SUB (padding)

        if (p != NULL)
        {
            // From memory
            while (data_len < packet_size && bytes_sent + data_len < file_size)
            {
                buf[data_len] = *p++;
                data_len++;
            }
        }
        else
        {
            // From file
            while (data_len < packet_size && !FileEOF(fnbr))
            {
                buf[data_len++] = FileGetChar(fnbr);
            }
        }

        // If we read no data at all, we're done
        if (data_len == 0)
            break;

        // Send packet with retries
        for (retry = 0; retry < YMAXRETRANS; retry++)
        {
            _outbyte(use_1k ? STX : SOH, 0);
            _outbyte(packet_num & 0xFF, 0);
            _outbyte((~packet_num) & 0xFF, 0);

            // Send data
            for (int i = 0; i < packet_size; i++)
            {
                _outbyte(buf[i], 0);
            }

            // Send CRC or checksum
            if (use_crc)
            {
                unsigned short crc = crc16_ccitt(buf, packet_size);
                _outbyte((crc >> 8) & 0xFF, 0);
                _outbyte(crc & 0xFF, 1);
            }
            else
            {
                unsigned char csum = 0;
                for (int i = 0; i < packet_size; i++)
                {
                    csum += buf[i];
                }
                _outbyte(csum, 1);
            }

            // Wait for ACK
            c = _inbyte(PACKET_TIMEOUT);
            if (c == ACK)
            {
                break;
            }
            else if (c == CAN)
            {
                error("Cancelled by remote");
                return;
            }
            else if (c == NAK)
            {
                // Receiver wants us to resend - retry
                continue;
            }
            // Timeout - retry
        }

        if (retry >= YMAXRETRANS)
        {
            _outbyte(CAN, 0);
            _outbyte(CAN, 1);
            error("Too many errors");
            return;
        }

        packet_num = (packet_num + 1) & 0xFF;
        bytes_sent += data_len;
    }

    // Send EOT
    for (retry = 0; retry < 10; retry++)
    {
        _outbyte(EOT, 1);
        c = _inbyte((PACKET_TIMEOUT) << 1);
        if (c == ACK)
        {
            break;
        }
        else if (c == NAK)
        {
            continue;
        }
    }

    if (retry >= 10)
    {
        return;
    }

    // TeraTerm will now send 'C' or NAK asking for next file
    // Send empty header to indicate no more files
    c = _inbyte(PACKET_TIMEOUT);
    if (c == CRC16 || c == NAK)
    {
        // Send empty header (null filename = end of batch)
        memset(buf, 0, PACKET_SIZE_128);

        _outbyte(SOH, 0);
        _outbyte(0, 0);
        _outbyte(0xFF, 0);

        for (int i = 0; i < PACKET_SIZE_128; i++)
        {
            _outbyte(buf[i], 0);
        }

        if (use_crc)
        {
            unsigned short crc = crc16_ccitt(buf, PACKET_SIZE_128);
            _outbyte((crc >> 8) & 0xFF, 0);
            _outbyte(crc & 0xFF, 1);
        }
        else
        {
            unsigned char csum = 0;
            _outbyte(csum, 1);
        }

        c = _inbyte(PACKET_TIMEOUT);
    }

    return;
}
#endif
// XMODEM Protocol Implementation
// Simple, efficient file transfer protocol
// Uses 128-byte blocks with CRC-16 for reliability

#include <string.h>
#include <stdio.h>

// Note: Protocol constants (SOH, EOT, ACK, NAK, CAN, CRC16) are shared with YMODEM
// Note: PACKET_TIMEOUT is shared with YMODEM
// Note: Uses crc16_ccitt() function and table from YMODEM implementation

#define XPACKET_SIZE 128
#define XMAXRETRANS 25

// Receive an XMODEM packet
// Returns: packet size on success, 0 on error, -1 on EOT, -2 on CAN
int xmodem_receive_packet(unsigned char *buf, int *packet_num, int use_crc)
{
    int c;
    unsigned char seq, seq_comp;
    unsigned short crc_recv, crc_calc;
    unsigned char csum_recv, csum_calc;
    int i;

    // Get packet start (SOH)
    c = _inbyte(PACKET_TIMEOUT);
    if (c < 0)
        return 0; // Timeout

    if (c == EOT)
        return -1; // End of transmission
    if (c == CAN)
    {
        // Check for second CAN
        if (_inbyte(1000) == CAN)
            return -2; // Cancelled
        return 0;      // False alarm
    }

    if (c != SOH)
    {
        return 0; // Invalid start (XMODEM only uses SOH/128-byte blocks)
    }

    // Get sequence number
    if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
        return 0;
    seq = c;

    // Get complement of sequence number
    if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
        return 0;
    seq_comp = c;

    // Verify sequence numbers are complements
    if (seq != (unsigned char)(~seq_comp))
    {
        return 0; // Sequence error
    }

    // Read data
    for (i = 0; i < XPACKET_SIZE; i++)
    {
        if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
            return 0;
        buf[i] = c;
    }

    // Read and verify checksum/CRC
    if (use_crc)
    {
        // CRC-16
        if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
            return 0;
        crc_recv = (c << 8);
        if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
            return 0;
        crc_recv |= c;

        crc_calc = crc16_ccitt(buf, XPACKET_SIZE);
        if (crc_calc != crc_recv)
        {
            return 0; // CRC error
        }
    }
    else
    {
        // Simple checksum
        if ((c = _inbyte(PACKET_TIMEOUT)) < 0)
            return 0;
        csum_recv = c;

        csum_calc = 0;
        for (i = 0; i < XPACKET_SIZE; i++)
        {
            csum_calc += buf[i];
        }
        if (csum_calc != csum_recv)
        {
            return 0; // Checksum error
        }
    }

    *packet_num = seq;
    return XPACKET_SIZE; // Return packet size on success
}

// XMODEM receive function
// sp: pointer to memory buffer (NULL if saving to file)
// maxbytes: size of memory buffer
// fnbr: file number (if saving to file)
// crunch: whether to use CrunchData processing
void xmodemReceive(char *sp, int maxbytes, int fnbr, int crunch)
{
    unsigned char buf[XPACKET_SIZE];
    unsigned char prev_buf[XPACKET_SIZE]; // Hold previous packet
    int packet_num;
    int expected_seq = 1; // XMODEM starts at packet 1
    int retry;
    int result;
    int use_crc = 0;          // Use checksum mode like original XMODEM
    int have_prev_packet = 0; // Flag to track if we have a previous packet

    CrunchData((unsigned char **)&sp, 0); // Initialize crunch if needed

    // Send initial NAK to request start (checksum mode)
    _outbyte(NAK, 1); // Flush needed - single byte transmission

    // Wait for first packet with retries
    for (retry = 0; retry < 10; retry++)
    {
        // Wait to see if sender responds
        result = xmodem_receive_packet(buf, &packet_num, use_crc);
        if (result > 0)
            break; // Got a packet

        // No response, send NAK again
        _outbyte(NAK, 1); // Flush needed - single byte transmission
    }

    if (retry >= 10)
    {
        error("Sender did not respond");
        return;
    }

    // Check first packet number
    if (packet_num != 1)
    {
        _outbyte(CAN, 0);
        _outbyte(CAN, 1); // Flush on last byte
        error("Expected packet 1");
        return;
    }

    // Good packet received with correct sequence
    expected_seq = 2;
    retry = 0;

    // Save first packet but don't write it yet (in case it's the last packet with padding)
    memcpy(prev_buf, buf, XPACKET_SIZE);
    have_prev_packet = 1;

    // Process first packet if going to memory (memory doesn't need padding removal)
    if (sp != NULL)
    {
        for (int i = 0; i < XPACKET_SIZE && maxbytes > 0; i++)
        {
            if (--maxbytes > 0)
            {
                if (buf[i] == 0x1A)
                    continue; // Skip padding (SUB/Ctrl-Z)
                if (crunch)
                    CrunchData((unsigned char **)&sp, buf[i]);
                else
                    *sp++ = buf[i];
            }
        }
    }

    // ACK AFTER processing - ready for next packet
    _outbyte(ACK, 1); // Flush needed - single byte transmission

    // Main receive loop
    while (1)
    {
        result = xmodem_receive_packet(buf, &packet_num, use_crc);

        if (result == -1)
        {
            // EOT - end of transmission
            _outbyte(ACK, 1); // ACK the EOT, flush needed

            // Now write the last packet (prev_buf) with padding removed
            if (sp == NULL && have_prev_packet)
            {
                // Remove padding bytes (0x1A/SUB) from end
                int write_len = XPACKET_SIZE;
                while (write_len > 0 && prev_buf[write_len - 1] == 0x1A)
                {
                    write_len--;
                }

                if (write_len > 0)
                {
                    FilePutdata((char *)prev_buf, fnbr, write_len);
                }
            }

            // Terminate string if saving to memory
            if (sp != NULL && maxbytes > 0)
            {
                *sp = 0;
            }

            return;
        }
        else if (result == -2)
        {
            // Cancelled by sender
            error("Cancelled by remote");
            return;
        }
        else if (result == 0)
        {
            // Timeout or error
            _outbyte(NAK, 1); // Flush needed - single byte transmission
            if (++retry > XMAXRETRANS)
            {
                _outbyte(CAN, 0);
                _outbyte(CAN, 1); // Flush on last byte
                error("Too many errors");
                return;
            }
            continue;
        }

        // Check sequence number
        if (packet_num != expected_seq)
        {
            // If it's the previous packet, sender didn't get our ACK
            if (packet_num == ((expected_seq - 1) & 0xFF))
            {
                _outbyte(ACK, 1); // Re-ACK it, flush needed
                continue;
            }
            // Otherwise, sequence error
            _outbyte(NAK, 1); // Flush needed - single byte transmission
            continue;
        }

        // Good packet received with correct sequence
        expected_seq = (expected_seq + 1) & 0xFF;
        retry = 0; // Reset retry counter on success

        // Write the PREVIOUS packet to file BEFORE ACKing
        // (so interrupts can be disabled during flash write without losing data)
        if (sp == NULL && have_prev_packet)
        {
            // Write entire packet at once (more efficient than byte-by-byte)
            FilePutdata((char *)prev_buf, fnbr, XPACKET_SIZE);
        }

        // Save current packet as previous for next iteration
        memcpy(prev_buf, buf, XPACKET_SIZE);
        have_prev_packet = 1;

        // Process data if going to memory
        if (sp != NULL)
        {
            for (int i = 0; i < XPACKET_SIZE && maxbytes > 0; i++)
            {
                if (--maxbytes > 0)
                {
                    if (buf[i] == 0x1A)
                        continue; // Skip padding
                    if (crunch)
                        CrunchData((unsigned char **)&sp, buf[i]);
                    else
                        *sp++ = buf[i];
                }
            }
        }

        // ACK AFTER all processing/writing complete - now ready for next packet
        _outbyte(ACK, 1); // Flush needed - single byte transmission
    }
}

// XMODEM transmit function
// p: pointer to data in memory (NULL if sending from file)
// fnbr: file number if sending from file
// data_size: size of data to send (0 to auto-calculate from string or read until EOF)
void xmodemTransmit(char *p, int fnbr, long data_size)
{
    unsigned char buf[XPACKET_SIZE];
    int packet_num = 1; // XMODEM starts at packet 1
    int retry;
    int c;
    int use_crc = 0;
    long bytes_sent = 0;

    // Calculate data size if not provided
    if (data_size == 0 && p != NULL)
    {
        data_size = strlen(p);
    }

    // Wait for receiver to request start (NAK)
    for (retry = 0; retry < 60; retry++)
    {
        c = _inbyte(1000);
        if (c == NAK)
        {
            use_crc = 0; // Receiver wants checksum
            break;
        }
        else if (c == CRC16)
        {
            use_crc = 1; // Receiver wants CRC (not standard XMODEM but common)
            break;
        }
        else if (c == CAN)
        {
            error("Cancelled by remote");
            return;
        }
    }

    if (retry >= 60)
    {
        error("Receiver did not respond");
        return;
    }

    // Send data packets
    while (1)
    {
        int data_len = 0;

        // Check if we're done
        if (p != NULL && bytes_sent >= data_size)
            break;
        if (p == NULL && FileEOF(fnbr))
            break;

        // Read data for this packet
        memset(buf, 0x1A, XPACKET_SIZE); // Fill with SUB (padding)

        if (p != NULL)
        {
            // From memory
            while (data_len < XPACKET_SIZE && bytes_sent + data_len < data_size)
            {
                buf[data_len] = *p++;
                data_len++;
            }
        }
        else
        {
            // From file
            while (data_len < XPACKET_SIZE && !FileEOF(fnbr))
            {
                buf[data_len++] = FileGetChar(fnbr);
            }
        }

        // If we read no data, we're done
        if (data_len == 0)
            break;

        // Send packet with retries
        for (retry = 0; retry < XMAXRETRANS; retry++)
        {
            _outbyte(SOH, 0);
            _outbyte(packet_num & 0xFF, 0);
            _outbyte((~packet_num) & 0xFF, 0);

            // Send data
            for (int i = 0; i < XPACKET_SIZE; i++)
            {
                _outbyte(buf[i], 0);
            }

            // Send CRC or checksum
            if (use_crc)
            {
                unsigned short crc = crc16_ccitt(buf, XPACKET_SIZE);
                _outbyte((crc >> 8) & 0xFF, 0);
                _outbyte(crc & 0xFF, 1); // Flush on last byte of packet
            }
            else
            {
                unsigned char csum = 0;
                for (int i = 0; i < XPACKET_SIZE; i++)
                {
                    csum += buf[i];
                }
                _outbyte(csum, 1); // Flush on last byte of packet
            }

            // Wait for ACK
            c = _inbyte(PACKET_TIMEOUT);
            if (c == ACK)
            {
                break;
            }
            else if (c == CAN)
            {
                error("Cancelled by remote");
                return;
            }
            else if (c == NAK)
            {
                // Receiver wants us to resend - retry
                continue;
            }
            // Timeout - retry
        }

        if (retry >= XMAXRETRANS)
        {
            _outbyte(CAN, 0);
            _outbyte(CAN, 0);
            _outbyte(CAN, 1); // Flush on last byte
            error("Too many errors");
            return;
        }

        packet_num = (packet_num + 1) & 0xFF;
        bytes_sent += data_len;
    }

    // Send EOT
    for (retry = 0; retry < 10; retry++)
    {
        _outbyte(EOT, 1);
        c = _inbyte(PACKET_TIMEOUT);
        if (c == ACK)
        {
            return; // Success
        }
    }

    // Timeout on EOT, but transfer is probably complete anyway
    return;
}