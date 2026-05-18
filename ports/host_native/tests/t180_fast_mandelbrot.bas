' RUN_ARGS: --vm
' Mandelbrot inner loop with '!FAST — correctness test
OPTION EXPLICIT

Const SCALE = 1073741824
Const ESCAPE_LIMIT = 4294967296
Const MAX_ITER = 512

Dim block%(319)

Sub Mandelbrot(block%(), startX%, startY%, delta%, bCount%, maxIter%)
  Local i%, iter%
  Local cx%, cy%
  Local zx%, zy%
  Local zx2%, zy2%
  Local nextX%, nextY%

  cy% = startY%
  cx% = startX%
  For i% = 0 To bCount% - 1
    zx% = 0
    zy% = 0
    zx2% = 0
    zy2% = 0
    iter% = 0
    '!FAST
    Do While iter% < maxIter%
      nextX% = zx2% - zy2% + cx%
      nextY% = (zx% * zy%) \ 536870912 + cy%
      zx% = nextX%
      zy% = nextY%
      zx2% = (zx% * zx%) \ 1073741824
      zy2% = (zy% * zy%) \ 1073741824
      If zx2% + zy2% > ESCAPE_LIMIT Then Exit Do
      iter% = iter% + 1
    Loop
    block%(i%) = iter%
    cx% = cx% + delta%
  Next i%
End Sub

' Run one scanline and check a few values
Dim startX%, delta%
startX% = -2147483648
delta% = 10066330
Mandelbrot block%(), startX%, 0, delta%, 320, MAX_ITER

' Spot checks: pixel 0 is c=(-2,0), exactly on boundary (|z|^2=4, not >4), so iter=MAX_ITER.
' Pixel 160 is near the real axis center of the main cardioid, should be in the set.
' Pixel 319 is far right (~+1.2), should escape quickly.
If block%(0) <> MAX_ITER Then ERROR "pixel 0: " + Str$(block%(0))
If block%(319) < 2 Then ERROR "pixel 319 too low: " + Str$(block%(319))
PRINT "mandelbrot ok"
