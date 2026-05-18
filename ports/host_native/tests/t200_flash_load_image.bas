' RUN_ARGS: --interp
' FLASH LOAD IMAGE: author a 4x2 24-bit BMP, decode it into flash slot 1,
' then PEEK each byte of the resulting RGB121-packed payload + 8-byte
' [width][height] header.
'
' Interp-only because cmd_flash is bridged-only; the VM lacks an opcode
' for it. The bridge path executes the same C code as native interp.

OPTION EXPLICIT

DIM INTEGER fnbr = 1
OPEN "B:/tile.bmp" FOR OUTPUT AS #fnbr

' --- BITMAPFILEHEADER (14 bytes) ---
PRINT #fnbr, "BM";                              ' bfType
PRINT #fnbr, CHR$(78);CHR$(0);CHR$(0);CHR$(0);  ' bfSize = 78
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);   ' bfReserved
PRINT #fnbr, CHR$(54);CHR$(0);CHR$(0);CHR$(0);  ' bfOffBits = 54

' --- BITMAPINFOHEADER (40 bytes) ---
PRINT #fnbr, CHR$(40);CHR$(0);CHR$(0);CHR$(0);  ' biSize
PRINT #fnbr, CHR$(4);CHR$(0);CHR$(0);CHR$(0);   ' biWidth = 4
PRINT #fnbr, CHR$(2);CHR$(0);CHR$(0);CHR$(0);   ' biHeight = 2 (bottom-up)
PRINT #fnbr, CHR$(1);CHR$(0);                   ' biPlanes
PRINT #fnbr, CHR$(24);CHR$(0);                  ' biBitCount = 24
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);   ' biCompression = 0
PRINT #fnbr, CHR$(24);CHR$(0);CHR$(0);CHR$(0);  ' biSizeImage
PRINT #fnbr, CHR$(&H13);CHR$(&H0B);CHR$(0);CHR$(0); ' biXPelsPerMeter
PRINT #fnbr, CHR$(&H13);CHR$(&H0B);CHR$(0);CHR$(0); ' biYPelsPerMeter
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);   ' biClrUsed
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);   ' biClrImportant

' --- Pixel data (BMP rows are bottom-up) ---
' File row 0 = image bottom = BLACK x4
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0); CHR$(0);CHR$(0);CHR$(0); CHR$(0);CHR$(0);CHR$(0); CHR$(0);CHR$(0);CHR$(0);
' File row 1 = image top = RED, GREEN, BLUE, WHITE  (bytes are BGR)
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(&HFF);          ' RED   (B=0  G=0   R=FF)
PRINT #fnbr, CHR$(0);CHR$(&HFF);CHR$(0);          ' GREEN (B=0  G=FF  R=0)
PRINT #fnbr, CHR$(&HFF);CHR$(0);CHR$(0);          ' BLUE  (B=FF G=0   R=0)
PRINT #fnbr, CHR$(&HFF);CHR$(&HFF);CHR$(&HFF);    ' WHITE
CLOSE #fnbr

' Note: MM.INFO(FILESIZE) reads the wrong filesystem on host's vm_host_fat
' (returns 0). The file IS written correctly — verify by FLASH LOAD IMAGE
' completing without error.

FLASH LOAD IMAGE 1, "B:/tile.bmp"

DIM INTEGER base = MM.INFO(FLASH ADDRESS 1)

' --- Header [width=4][height=2] LE ---
IF PEEK(INT8 base + 0) <> 4 THEN ERROR "width.lo"
IF PEEK(INT8 base + 1) <> 0 THEN ERROR "width.b1"
IF PEEK(INT8 base + 2) <> 0 THEN ERROR "width.b2"
IF PEEK(INT8 base + 3) <> 0 THEN ERROR "width.b3"
IF PEEK(INT8 base + 4) <> 2 THEN ERROR "height.lo"
IF PEEK(INT8 base + 5) <> 0 THEN ERROR "height.b1"
IF PEEK(INT8 base + 6) <> 0 THEN ERROR "height.b2"
IF PEEK(INT8 base + 7) <> 0 THEN ERROR "height.b3"

' --- Top row: RED(0x8) GREEN(0x6) BLUE(0x1) WHITE(0xF), packed even-low / odd-high ---
IF PEEK(INT8 base +  8) <> &H68 THEN ERROR "top.b0 = " + HEX$(PEEK(INT8 base + 8))
IF PEEK(INT8 base +  9) <> &HF1 THEN ERROR "top.b1 = " + HEX$(PEEK(INT8 base + 9))

' --- Bottom row: all BLACK ---
IF PEEK(INT8 base + 10) <> 0 THEN ERROR "bot.b0"
IF PEEK(INT8 base + 11) <> 0 THEN ERROR "bot.b1"

PRINT "ok"
END
