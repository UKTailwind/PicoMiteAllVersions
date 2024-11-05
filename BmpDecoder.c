/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
#define __BMPDECODER_C__
/******************************************************************************

* FileName:        BmpDecoder.c
* Dependencies:    Image decoding library; project requires File System library
* Processor:       PIC24/dsPIC30/dsPIC33/PIC32MX
* Compiler:        C30 v2.01/C32 v0.00.18
* Company:         Microchip Technology, Inc.

 * Software License Agreement
 *
 * Copyright � 2008 Microchip Technology Inc.  All rights reserved.
 * Microchip licenses to you the right to use, modify, copy and distribute
 * Software only when embedded on a Microchip microcontroller or digital
 * signal controller, which is integrated into your product or third party
 * product (pursuant to the sublicense terms in the accompanying license
 * agreement).  
 *
 * You should refer to the license agreement accompanying this Software
 * for additional information regarding your rights and obligations.
 *
 * SOFTWARE AND DOCUMENTATION ARE PROVIDED �AS IS� WITHOUT WARRANTY OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY WARRANTY
 * OF MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR
 * PURPOSE. IN NO EVENT SHALL MICROCHIP OR ITS LICENSORS BE LIABLE OR
 * OBLIGATED UNDER CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION,
 * BREACH OF WARRANTY, OR OTHER LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT
 * DAMAGES OR EXPENSES INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL,
 * INDIRECT, PUNITIVE OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA,
 * COST OF PROCUREMENT OF SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY
 * CLAIMS BY THIRD PARTIES (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF),
 * OR OTHER SIMILAR COSTS.

Author                 Date           Comments
--------------------------------------------------------------------------------
Pradeep Budagutta    03-Mar-2008    First release
*******************************************************************************/

//#include "GenericTypeDefs.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
//** SD CARD INCLUDES ***********************************************************
#include "ff.h"


#define  IMG_FILE   fnbr
int IMG_FREAD(int fnbr, void *buff, int count, unsigned int *read){
        if(filesource[IMG_FILE]==FATFSFILE){
                return f_read(FileTable[IMG_FILE].fptr, buff, count, (UINT *)read);
        } else {
                int n=lfs_file_read(&lfs, FileTable[IMG_FILE].lfsptr, buff, count);
                if(n>=0)return 0;
                else return n;
        }       
}
#define  IMG_vSetboundaries()
#define  IMG_vLoopCallback()
#define  IMG_vCheckAndAbort()  CheckAbort()
union colourmap
{
  char rgbbytes[4];
  unsigned int rgb;
} colour, colour2;
#define  IMG_vSetColor(red, green, blue)    {colour.rgbbytes[2] = red; colour.rgbbytes[1] = green; colour.rgbbytes[0] = blue;}
#define  IMG_vSetColor2(red, green, blue)    {colour2.rgbbytes[2] = red; colour2.rgbbytes[1] = green; colour2.rgbbytes[0] = blue;}
#define  IMG_vPutPixel(xx, yy)              DrawPixel(xx + x, yy + y, colour.rgb)
#define  IMG_vPutPixel2(xx, yy)              DrawPixel(xx + x, yy + y, colour2.rgb)
#define LONG long
int IMG_wImageWidth, IMG_wImageHeight;
int bufpos;

/*************************/
/**** DATA STRUCTURES ****/
/*************************/
typedef struct _BMPDECODER
{
        LONG lWidth;
        LONG lHeight;
        LONG lImageOffset;
        WORD wPaletteEntries;
        BYTE bBitsPerPixel;
        BYTE bHeaderType;
        BYTE blBmMarkerFlag : 1;
        BYTE blCompressionType : 3;
        BYTE bNumOfPlanes : 3;
        BYTE b16bit565flag : 1;
        BYTE aPalette[256][3]; /* Each palette entry has RGB */
} BMPDECODER;

/**************************/
/******* FUNCTIONS  *******/
/**************************/

/*******************************************************************************
Function:       void BDEC_vResetData(BMPDECODER *pBmpDec)

Precondition:   None

Overview:       This function resets the variables so that new Bitmap image
                can be decoded

Input:          Bitmap decoder's data structure

Output:         None
*******************************************************************************/
void BDEC_vResetData(BMPDECODER *pBmpDec)
{
    pBmpDec->lWidth = 0;
    pBmpDec->lHeight = 0;
    pBmpDec->lImageOffset = 0;
    pBmpDec->wPaletteEntries = 0;
    pBmpDec->bBitsPerPixel = 0;
    pBmpDec->bHeaderType = 0;
    pBmpDec->blBmMarkerFlag = 0;
    pBmpDec->blCompressionType = 0;
    pBmpDec->bNumOfPlanes = 0;
    pBmpDec->b16bit565flag = 0;    
}

/*******************************************************************************
Function:       BYTE BDEC_bReadHeader(BMPDECODER *pBmpDec)

Precondition:   None

Overview:       This function reads the Bitmap file header and 
                fills the data structure

Input:          Bitmap decoder's data structure

Output:         Error code - '0' means no error
*******************************************************************************/
BYTE BDEC_bReadHeader(BMPDECODER *pBmpDec, int fnbr)
{
        BYTE bByte1, bByte2;
        WORD wWord;
        LONG lLong;
        unsigned int nbr;
        FSerror = IMG_FREAD(IMG_FILE, &bByte1, 1, &nbr);  /* Marker */
        FSerror = IMG_FREAD(IMG_FILE, &bByte2, 1, &nbr);  /* Marker */
        
        if(bByte1 == 'B' && bByte2 == 'M')
        {
                  pBmpDec->blBmMarkerFlag = 1;
        }
        else
        {
                  return(100);
        }

        FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* File length */
        FSerror = IMG_FREAD(IMG_FILE, &wWord, 2, &nbr);  /* Reserved */
        FSerror = IMG_FREAD(IMG_FILE, &wWord, 2, &nbr);  /* Reserved */

        FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* Image offset */
        pBmpDec->lImageOffset = lLong;

        FSerror = IMG_FREAD(IMG_FILE,&lLong, 4, &nbr);  /* Header length */
        pBmpDec->bHeaderType = (BYTE)lLong;

        if(pBmpDec->bHeaderType >= 40)
        {
                  FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* Image Width */
                  pBmpDec->lWidth = lLong;

                  FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* Image Height */
                  pBmpDec->lHeight = lLong;

                  FSerror = IMG_FREAD(IMG_FILE, &wWord, 2, &nbr);  /* Number of Planes */
                  pBmpDec->bNumOfPlanes = (BYTE)wWord;

                  FSerror = IMG_FREAD(IMG_FILE, &wWord, 2, &nbr);  /* Bits per Pixel */
                  pBmpDec->bBitsPerPixel = (BYTE)wWord;

                  FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* Compression info */
                  pBmpDec->blCompressionType = (BYTE)lLong;

                  FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* Image length */
                  FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* xPixels per metre */
                  FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* yPixels per metre */

                  FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* Palette entries */
                  pBmpDec->wPaletteEntries = (WORD)lLong;

                  if(pBmpDec->wPaletteEntries == 0)
                  {
                          WORD wTemp = (WORD)(pBmpDec->lImageOffset - 14 - 40)/4;
                          if(wTemp > 0)
                          {
                                   pBmpDec->wPaletteEntries = wTemp; /* This is because of a bug in MSPAINT */
                          }
                  }

                  FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* Important colors */
                  if(pBmpDec->bBitsPerPixel == 16 && pBmpDec->bHeaderType > 40)
                  {
                          FSerror = IMG_FREAD(IMG_FILE, &lLong, 4, &nbr);  /* Red mask */
                          if((WORD)lLong == 0xF800)
                          {
                                     pBmpDec->b16bit565flag = 1;
                          }
                  }

//                  IMG_FSEEK(&nbr, pBmpDec->bHeaderType + 14, 0);

                  if(pBmpDec->wPaletteEntries <= 256)
                  {
                          WORD wCounter;
                          for(wCounter = 0; wCounter < pBmpDec->wPaletteEntries; wCounter++)
                          {
                                     FSerror = IMG_FREAD(IMG_FILE, &pBmpDec->aPalette[wCounter][2], 1, &nbr); /* R */
                                     FSerror = IMG_FREAD(IMG_FILE, &pBmpDec->aPalette[wCounter][1], 1, &nbr); /* G */
                                     FSerror = IMG_FREAD(IMG_FILE, &pBmpDec->aPalette[wCounter][0], 1, &nbr); /* B */
                                     FSerror = IMG_FREAD(IMG_FILE, &wWord, 1, &nbr); /* Dummy */
                          }
                  }
        }
        return(0);
}

/*******************************************************************************
Function:       BYTE BMP_bDecode(IMG_FILE *pFile)

Precondition:   None

Overview:       This function decodes and displays a Bitmap image

Input:          Image file

Output:         Error code - '0' means no error
*******************************************************************************/
BYTE BMP_bDecode(int x, int y, int fnbr)
{
        BMPDECODER BmpDec;
        WORD wX, wY;
        BYTE bPadding;
        unsigned int nbr;
        BDEC_vResetData(&BmpDec);
        BDEC_bReadHeader(&BmpDec, fnbr);
        if(BmpDec.blBmMarkerFlag == 0 || BmpDec.bHeaderType < 40 || (BmpDec.blCompressionType != 0 && BmpDec.blCompressionType != 2 && BmpDec.blCompressionType != 3))
        {
            return 100;
        }    
        IMG_wImageWidth = (WORD)BmpDec.lWidth;
        IMG_wImageHeight = (WORD)BmpDec.lHeight;
        IMG_vSetboundaries();
        char *linebuff=GetMemory(IMG_wImageWidth*3); //get a line buffer

//        IMG_FSEEK(pFile, BmpDec.lImageOffset, 0);
        if(BmpDec.wPaletteEntries == 0 && BmpDec.bBitsPerPixel == 8) /* Grayscale Image */
        {
                bPadding = (4 - (BmpDec.lWidth % 4))%4;
                for(wY = 0; wY < BmpDec.lHeight; wY++)
                {
                         IMG_vLoopCallback();
                         IMG_vCheckAndAbort();
                         for(wX = 0; wX < BmpDec.lWidth; wX++)
                         {
                                   BYTE bY;
                                   FSerror = IMG_FREAD(IMG_FILE, &bY, 1, &nbr); /* Y */
                                   IMG_vSetColor(bY, bY, bY);
                                   IMG_vPutPixel(wX, BmpDec.lHeight - wY - 1);
                         }
                         for(wX = 0; wX < bPadding; wX++)
                         {
                                   BYTE bValue;
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue, 1, &nbr);
                         }
                }
        }
        else if(BmpDec.bBitsPerPixel == 16) /* 16-bit Color Image */
        {
                bPadding = (4 - ((BmpDec.lWidth*2) % 4))%4;
                for(wY = 0; wY < BmpDec.lHeight; wY++)
                {
                         IMG_vLoopCallback();
                         IMG_vCheckAndAbort();
                         for(wX = 0; wX < BmpDec.lWidth; wX++)
                         {
                                   WORD wColor;
                                   BYTE bR, bG, bB;
                                   FSerror = IMG_FREAD(IMG_FILE, &wColor, 2, &nbr); /* RGB */
                                   if(BmpDec.b16bit565flag == 1)
                                   {
                                              bR = (wColor >> 11) << 3;
                                              bG = ((wColor & 0x07E0) >> 5) << 2;
                                              bB = (wColor & 0x001F) << 3;
                                   }
                                   else
                                   {
                                              bR = ((wColor & 0x7FFF) >> 10) << 3;
                                              bG = ((wColor & 0x03E0) >> 5) << 3;
                                              bB = (wColor & 0x001F) << 3;
                                   }
                                   IMG_vSetColor(bR, bG, bB);
                                   IMG_vPutPixel(wX, BmpDec.lHeight - wY - 1);
                         }
                         for(wX = 0; wX < bPadding; wX++)
                         {
                                   BYTE bValue;
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue, 1, &nbr);
                         }
                }
        }
        else if(BmpDec.bBitsPerPixel == 24) /* True color Image */
        {
                int pp;
                bPadding = (4 - ((BmpDec.lWidth*3) % 4))%4;
                for(wY = 0; wY < BmpDec.lHeight; wY++)
                {
                        IMG_vLoopCallback();
                        IMG_vCheckAndAbort();
                        FSerror = IMG_FREAD(IMG_FILE, linebuff, BmpDec.lWidth*3, &nbr); /* B */
                        pp=0;
                        for(wX = 0; wX < BmpDec.lWidth; wX++)
                        {
                            colour.rgbbytes[0]=linebuff[pp++];
                            colour.rgbbytes[1]=linebuff[pp++];
                            colour.rgbbytes[2]=linebuff[pp++];
                            IMG_vPutPixel(wX, BmpDec.lHeight - wY - 1);
                        }
                        for(wX = 0; wX < bPadding; wX++)
                        {
                                   BYTE bValue;
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue,  1, &nbr);
                        }
                }
        }
        else if(BmpDec.wPaletteEntries != 0 && BmpDec.bBitsPerPixel == 1) /* B/W Image */
        {
                WORD wBytesPerRow = BmpDec.lWidth/8;
                BYTE bAdditionalBitsPerRow = BmpDec.lWidth % 8;
                bPadding = (4 - ((wBytesPerRow + (bAdditionalBitsPerRow?1:0)) % 4))%4;
                for(wY = 0; wY < BmpDec.lHeight; wY++)
                {
                         BYTE bBits, bValue;
                         IMG_vLoopCallback();
                         IMG_vCheckAndAbort();
                         for(wX = 0; wX < wBytesPerRow; wX++)
                         {
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue, 1, &nbr);
                                   
                                   for(bBits = 0; bBits < 8; bBits++)
                                   {
                                             BYTE bIndex = (bValue & (0x80 >> bBits))?1:0;
                                             IMG_vSetColor(BmpDec.aPalette[bIndex][0], BmpDec.aPalette[bIndex][1], BmpDec.aPalette[bIndex][2]);
                                             IMG_vPutPixel(wX*8+bBits, BmpDec.lHeight - wY - 1);
                                   }
                         }
                         if(bAdditionalBitsPerRow > 0)
                         {
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue,  1, &nbr);
                                   
                                   for(bBits = 0; bBits < bAdditionalBitsPerRow; bBits++)
                                   {
                                             BYTE bIndex = (bValue & (0x80 >> bBits))?1:0;
                                             IMG_vSetColor(BmpDec.aPalette[bIndex][0], BmpDec.aPalette[bIndex][1], BmpDec.aPalette[bIndex][2]);
                                             IMG_vPutPixel(wX*8+bBits, BmpDec.lHeight - wY - 1);
                                   }
                         }
                         for(wX = 0; wX < bPadding; wX++)
                         {
                                   BYTE bValue;
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue, 1, &nbr);
                         }
                }
        }
        else if(BmpDec.wPaletteEntries != 0 && BmpDec.bBitsPerPixel == 4 && BmpDec.blCompressionType != 2) /* 16 colors Image */
        {
                WORD wBytesPerRow = BmpDec.lWidth/2;
                BYTE bAdditionalNibblePerRow = BmpDec.lWidth % 2;
                bPadding = (4 - ((wBytesPerRow + bAdditionalNibblePerRow) % 4))%4;
                for(wY = 0; wY < BmpDec.lHeight; wY++)
                {
                         IMG_vLoopCallback();
                         IMG_vCheckAndAbort();
                         for(wX = 0; wX < wBytesPerRow; wX++)
                         {
                                   BYTE bIndex, bValue;
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue, 1, &nbr);
                                   bIndex = bValue >> 4;
                                   IMG_vSetColor(BmpDec.aPalette[bIndex][0], BmpDec.aPalette[bIndex][1], BmpDec.aPalette[bIndex][2]);
                                   IMG_vPutPixel(wX*2, BmpDec.lHeight - wY - 1);
                                   bIndex = bValue & 0x0F;
                                   IMG_vSetColor(BmpDec.aPalette[bIndex][0], BmpDec.aPalette[bIndex][1], BmpDec.aPalette[bIndex][2]);
                                   IMG_vPutPixel(wX*2+1, BmpDec.lHeight - wY - 1);
                         }
                         if(bAdditionalNibblePerRow)
                         {
                                   BYTE bIndex, bValue;
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue, 1, &nbr); /* Bits8 */
                                   bIndex = bValue >> 4;
                                   IMG_vSetColor(BmpDec.aPalette[bIndex][0], BmpDec.aPalette[bIndex][1], BmpDec.aPalette[bIndex][2]);
                                   IMG_vPutPixel(wX*2, BmpDec.lHeight - wY - 1);
                         }
                         for(wX = 0; wX < bPadding; wX++)
                         {
                                   BYTE bValue;
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue,  1, &nbr);
                         }
                }
        }
        else if(BmpDec.wPaletteEntries != 0 && BmpDec.bBitsPerPixel == 4 && BmpDec.blCompressionType==2) /* 16 colors Image */
        {
                BYTE bIndex;
                int c;
                unsigned char b[2];
                for(wY = 0; wY < BmpDec.lHeight; wY++)
                {
                        IMG_vCheckAndAbort();
                        wX=0;
                        do {
                                FSerror = IMG_FREAD(IMG_FILE, b, 2, &nbr);  
                                c=b[0];
                                bIndex = b[1] >> 4;
                                IMG_vSetColor(BmpDec.aPalette[bIndex][0], BmpDec.aPalette[bIndex][1], BmpDec.aPalette[bIndex][2]);
                                bIndex = b[1] & 0xf;
                                IMG_vSetColor2(BmpDec.aPalette[bIndex][0], BmpDec.aPalette[bIndex][1], BmpDec.aPalette[bIndex][2]);
                                while(c){
                                        IMG_vPutPixel(wX++, BmpDec.lHeight - wY - 1);
                                        IMG_vPutPixel2(wX++, BmpDec.lHeight - wY - 1);
                                        c-=2;
                                }
                        } while(!(b[0]==0 && b[1]==0));
                        
                }
        }

        else if(BmpDec.wPaletteEntries != 0 && BmpDec.bBitsPerPixel == 8) /* 256 colors Image */
        {
                bPadding = (4 - (BmpDec.lWidth % 4))%4;
                for(wY = 0; wY < BmpDec.lHeight; wY++)
                {
                         IMG_vLoopCallback();                         IMG_vCheckAndAbort();
                         for(wX = 0; wX < BmpDec.lWidth; wX++)
                         {
                                   BYTE bIndex;
                                   FSerror = IMG_FREAD(IMG_FILE, &bIndex, 1, &nbr);
                                   IMG_vSetColor(BmpDec.aPalette[bIndex][0], BmpDec.aPalette[bIndex][1], BmpDec.aPalette[bIndex][2]);
                                   IMG_vPutPixel(wX, BmpDec.lHeight - wY - 1);
                         }
                         for(wX = 0; wX < bPadding; wX++)
                         {
                                   BYTE bValue;
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue,  1, &nbr);
                         }
                }
        }
        FreeMemory((void *)linebuff);
        return 0;
}
/*******************************************************************************
Function:       BYTE BMP_bDecode(IMG_FILE *pFile)

Precondition:   None

Overview:       This function decodes and puts a section of an image into a memory array

Input:          Image file

Output:         Error code - '0' means no error
*******************************************************************************/
BYTE BMP_bDecode_memory(int x, int y, int xlen, int ylen, int fnbr, char *p)
{
        BMPDECODER BmpDec;
        WORD wX, wY;
        BYTE bPadding;
        unsigned int nbr;
        BDEC_vResetData(&BmpDec);
        BDEC_bReadHeader(&BmpDec, fnbr);
        if(BmpDec.blBmMarkerFlag == 0 || BmpDec.bHeaderType < 40 || (BmpDec.blCompressionType != 0 && BmpDec.blCompressionType != 3))
        {
            return 100;
        }    
        IMG_wImageWidth = (WORD)BmpDec.lWidth;
        IMG_wImageHeight = (WORD)BmpDec.lHeight;
        IMG_vSetboundaries();
        char *linebuff=GetMemory(IMG_wImageWidth*3); //get a line buffer

//        IMG_FSEEK(pFile, BmpDec.lImageOffset, 0);
        if(BmpDec.bBitsPerPixel == 24) /* True color Image */
        {
                int pp;
                bPadding = (4 - ((BmpDec.lWidth*3) % 4))%4;
                for(wY = 0; wY < BmpDec.lHeight; wY++)
                {
                         routinechecks();
                         IMG_vLoopCallback();
                         IMG_vCheckAndAbort();
                         FSerror = IMG_FREAD(IMG_FILE, linebuff, BmpDec.lWidth*3, &nbr); /* B */
 /*                        if((void *)DrawBuffer != (void *)DisplayNotSet) {
                                 PInt(linebuff[0]);PIntComma(linebuff[1]);PIntComma(linebuff[2]);PRet();
                             DrawBuffer(x, BmpDec.lHeight - wY - 1 + y, x+BmpDec.lWidth-1, BmpDec.lHeight - wY - 1 + y,  linebuff);
                         } else */{ //must be a loadable driver so no helper function available
                            pp=0;
                            for(wX = 0; wX < BmpDec.lWidth; wX++)
                            {
                                colour.rgbbytes[0]=linebuff[pp++];
                                colour.rgbbytes[1]=linebuff[pp++];
                                colour.rgbbytes[2]=linebuff[pp++];
                                int px=wX-x;
                                int py=BmpDec.lHeight - wY - 1 - y;
                                if(px<xlen && px>=0 && py<ylen && py>=0){
                                        char *q= p+(py*xlen+px)*3;
                                        *q++=colour.rgbbytes[0];
                                        *q++=colour.rgbbytes[1];
                                        *q++=colour.rgbbytes[2];
                                }
                            }
                         }
                         for(wX = 0; wX < bPadding; wX++)
                         {
                                   BYTE bValue;
                                   FSerror = IMG_FREAD(IMG_FILE, &bValue,  1, &nbr);
                         }
                }
        }
        else error("Only 24-bit colour images supported");
        FreeMemory((void *)linebuff);
        return 0;
}
/*  @endcond */

