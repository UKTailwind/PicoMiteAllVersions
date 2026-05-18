' RUN_ARGS: --vm
' Mandelbrot inner loop using '!ASM inline assembly
OPTION EXPLICIT

Dim block%(319)

Sub Mandelbrot(block%(), startX%, startY%, delta%, bCount%, maxIter%)
  Local i%, iter%
  Local cx%, cy%
  Local zx%, zy%
  Local zx2%, zy2%
  Local nextX%, nextY%
  Local mag%

  cy% = startY%
  cx% = startX%
  For i% = 0 To bCount% - 1
    '!ASM
    .const BITS30,   30
    .const BITS29,   29
    .const ESC_FP,   4294967296
    .const ZERO,     0
    .const ONE,      1

        mov        zx,   ZERO
        mov        zy,   ZERO
        mov        zx2,  ZERO
        mov        zy2,  ZERO
        mov        iter, ZERO
    .loop:
        jge        iter, maxIter, .done
        ; nextX = zx2 - zy2 + cx
        subi       nextX, zx2, zy2
        addi       nextX, nextX, cx
        ; nextY = (zx * zy) >> 29 + cy  (= (zx*zy) \ HALF_SCALE + cy)
        mulshr     nextY, zx, zy, BITS29
        addi       nextY, nextY, cy
        ; zx = nextX, zy = nextY
        mov        zx, nextX
        mov        zy, nextY
        ; zx2 = (zx * zx) >> 30  (= (zx*zx) \ SCALE)
        sqrshr     zx2, zx, BITS30
        ; zy2 = (zy * zy) >> 30
        sqrshr     zy2, zy, BITS30
        ; if zx2 + zy2 > ESC_FP then exit
        addi       mag, zx2, zy2
        jgt        mag, ESC_FP, .done
        addi       iter, iter, ONE
        checkint
        jmp        .loop
    .done:
        exit
    '!ENDASM

    block%(i%) = iter%
    cx% = cx% + delta%
  Next i%
End Sub

' Run one scanline and check
Dim startX%, delta%
startX% = -2147483648
delta% = 10066330
Mandelbrot block%(), startX%, 0, delta%, 320, 512

If block%(0) <> 512 Then ERROR "pixel 0: " + Str$(block%(0))
If block%(319) < 2 Then ERROR "pixel 319 too low: " + Str$(block%(319))
PRINT "asm mandelbrot ok"
