' RUN_ARGS: --interp --sd-root=/tmp/mmbasic-t197
' LOAD IMAGE round-trip on host (previously stubbed out as
' "Not supported on host"). Draws a known pattern, SAVEs it, CLSes
' the framebuffer, LOADs the BMP back, and verifies PIXEL() returns
' the original colours.
'
' Exercises BmpDecoder.c linked into the host build, plus the POSIX
' read path in IMG_FREAD.
Const WIDTH% = 16
Const HEIGHT% = 16

CLS RGB(BLACK)
BOX 0, 0, WIDTH%, HEIGHT%, 1, RGB(RED), RGB(GREEN)
Pixel 3, 3, RGB(BLUE)
Pixel 7, 7, RGB(WHITE)

Dim centreBefore% = Pixel(7, 7)
Dim fillBefore% = Pixel(5, 5)

SAVE IMAGE "rt.bmp", 0, 0, WIDTH%, HEIGHT%

CLS RGB(BLACK)
If Pixel(7, 7) = centreBefore% Then Error "CLS did not clear pixel"

LOAD IMAGE "rt.bmp"

Dim centreAfter% = Pixel(7, 7)
Dim fillAfter% = Pixel(5, 5)

If centreAfter% <> centreBefore% Then Error "LOAD IMAGE round-trip pixel mismatch at (7,7): expected " + Hex$(centreBefore%) + " got " + Hex$(centreAfter%)
If fillAfter% <> fillBefore% Then Error "LOAD IMAGE round-trip pixel mismatch at (5,5): expected " + Hex$(fillBefore%) + " got " + Hex$(fillAfter%)

KILL "rt.bmp"
Print "load image round-trip ok"
