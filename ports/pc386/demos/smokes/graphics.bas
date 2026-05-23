' smokes/graphics.bas — modes + draw primitives + readback.

DIM YELLOW% = RGB(YELLOW)
DIM RED%    = RGB(RED)
DIM BLACK%  = RGB(BLACK)

' Mode 1 — 320x200 standard mode 13h.
MODE 1
IF MM.HRES = 320 AND MM.VRES = 200 THEN PRINT "OK_mode1_dims" ELSE PRINT "FAIL_mode1_dims " + STR$(MM.HRES) + "x" + STR$(MM.VRES)

' CLS to known colour, then read a pixel.
CLS YELLOW%
DIM px%
px% = PIXEL(10, 10)
' YELLOW = RGB(255,255,0). 332-quantised + reconstructed = some yellowish value.
' Just check the blue channel is small and red+green dominate.
DIM r% = (px% \ 65536) AND 255
DIM g% = (px% \ 256) AND 255
DIM b% = px% AND 255
IF r% > 200 AND g% > 200 AND b% < 80 THEN PRINT "OK_cls_yellow" ELSE PRINT "FAIL_cls_yellow rgb=" + STR$(r%) + "," + STR$(g%) + "," + STR$(b%)

' PIXEL set + read back.
PIXEL 50, 50, RED%
DIM rp% = PIXEL(50, 50)
DIM rr% = (rp% \ 65536) AND 255
DIM rg% = (rp% \ 256) AND 255
DIM rb% = rp% AND 255
IF rr% > 200 AND rg% < 80 AND rb% < 80 THEN PRINT "OK_pixel_rw" ELSE PRINT "FAIL_pixel_rw rgb=" + STR$(rr%) + "," + STR$(rg%) + "," + STR$(rb%)

' BOX (filled rectangle).
CLS BLACK%
BOX 10, 10, 30, 30, 1, RED%, RED%
DIM bp% = PIXEL(20, 20)
IF ((bp% \ 65536) AND 255) > 200 THEN PRINT "OK_box_filled" ELSE PRINT "FAIL_box_filled"
DIM corner% = PIXEL(50, 50)
IF corner% = 0 THEN PRINT "OK_box_clear_outside" ELSE PRINT "FAIL_box_clear_outside " + STR$(corner%)

' LINE (horizontal then vertical).
CLS BLACK%
LINE 0, 100, 319, 100, , RED%
LINE 160, 0, 160, 199, , RED%
IF ((PIXEL(50, 100) \ 65536) AND 255) > 200 THEN PRINT "OK_line_horz" ELSE PRINT "FAIL_line_horz"
IF ((PIXEL(160, 50) \ 65536) AND 255) > 200 THEN PRINT "OK_line_vert" ELSE PRINT "FAIL_line_vert"

' CIRCLE — sample several points on the rim.
CLS BLACK%
CIRCLE 160, 100, 40, , , , RED%
DIM hits% = 0
IF ((PIXEL(200, 100) \ 65536) AND 255) > 200 THEN hits% = hits% + 1
IF ((PIXEL(120, 100) \ 65536) AND 255) > 200 THEN hits% = hits% + 1
IF ((PIXEL(160, 60)  \ 65536) AND 255) > 200 THEN hits% = hits% + 1
IF ((PIXEL(160, 140) \ 65536) AND 255) > 200 THEN hits% = hits% + 1
IF hits% >= 3 THEN PRINT "OK_circle" ELSE PRINT "FAIL_circle hits=" + STR$(hits%)

' Mode 2 — 640x480.
MODE 2
IF MM.HRES = 640 AND MM.VRES = 480 THEN PRINT "OK_mode2_dims" ELSE PRINT "FAIL_mode2_dims"
CLS YELLOW%
DIM p2% = PIXEL(300, 200)
IF ((p2% \ 65536) AND 255) > 200 AND ((p2% \ 256) AND 255) > 200 THEN PRINT "OK_mode2_cls" ELSE PRINT "FAIL_mode2_cls"

' Mode 3 — 720x400.
MODE 3
IF MM.HRES = 720 AND MM.VRES = 400 THEN PRINT "OK_mode3_dims" ELSE PRINT "FAIL_mode3_dims"

' Return to mode 1 so the test harness sees a sensible prompt console.
MODE 1
PRINT "SMOKE_DONE"
END
