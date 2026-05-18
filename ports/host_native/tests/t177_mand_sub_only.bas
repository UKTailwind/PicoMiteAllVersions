' RUN_ARGS: --vm
' Extract just the Mandelbrot inner loop to verify peephole fusing
OPTION EXPLICIT

Const SCALE = 1073741824
Const HALF_SCALE = 536870912
Const QUARTER_SCALE = 268435456
Const SIXTEENTH_SCALE = 67108864
Const ESCAPE_LIMIT = 4294967296
Const MAX_ITER = 512

Dim block%(319)

Sub Mandelbrot(block%(), startX%, startY%, delta%, bCount%, maxIter%)
  Local i%, iter%
  Local cx%, cy%
  Local zx%, zy%
  Local zx2%, zy2%
  Local nextX%, nextY%
  Local cy2%
  Local xQuarter%, q%, xBulb%
  Local prod%
  Local periodCount%
  Local periodZX%, periodZY%

  cy% = startY%
  cy2% = (cy% * cy%) \ 1073741824
  cx% = startX%
  For i% = 0 To bCount% - 1
    xQuarter% = cx% - QUARTER_SCALE
    q% = (xQuarter% * xQuarter%) \ 1073741824 + cy2%
    prod% = (q% * (q% + xQuarter%)) \ 1073741824
    If prod% <= (cy2% >> 2) Then
      block%(i%) = maxIter%
    Else
      xBulb% = cx% + SCALE
      If ((xBulb% * xBulb%) \ 1073741824 + cy2%) <= SIXTEENTH_SCALE Then
        block%(i%) = maxIter%
      Else
        zx% = 0
        zy% = 0
        zx2% = 0
        zy2% = 0
        iter% = 0
        periodCount% = 0
        periodZX% = 0
        periodZY% = 0
        Do While iter% < maxIter%
          nextX% = zx2% - zy2% + cx%
          nextY% = (zx% * zy%) \ 536870912 + cy%
          zx% = nextX%
          zy% = nextY%
          zx2% = (zx% * zx%) \ 1073741824
          zy2% = (zy% * zy%) \ 1073741824
          If zx2% + zy2% > ESCAPE_LIMIT Then Exit Do
          iter% = iter% + 1
          If zx% = periodZX% Then
            If zy% = periodZY% Then
              iter% = maxIter%
              Exit Do
            EndIf
          EndIf
          periodCount% = periodCount% + 1
          If periodCount% >= 16 Then
            periodCount% = 0
            periodZX% = zx%
            periodZY% = zy%
          EndIf
        Loop
        block%(i%) = iter%
      EndIf
    EndIf
    cx% = cx% + delta%
  Next i%
End Sub

' Quick sanity: run one scanline
Dim startX%, delta%
startX% = -2147483648
delta% = 10066330
Mandelbrot block%(), startX%, 0, delta%, 320, MAX_ITER
PRINT "ok"
