' Mandelbrot renderer with menu (PicoMite MMBasic) - pure BASIC replacement for CSUB
OPTION EXPLICIT
OPTION CONTINUATION LINES ON

Const SCALE = 1073741824
Const HALF_SCALE = 536870912
Const QUARTER_SCALE = 268435456
Const SIXTEENTH_SCALE = 67108864
Const ESCAPE_LIMIT = 4294967296

' -- Screen geometry sourced from MM.HRES / MM.VRES so the renderer
'    adapts to whatever resolution the host provides. Historical
'    PicoCalc default is 320x320. Viewport stays square-pixel regardless
'    of viewport aspect ratio (see ResetViewport below).
Const MENU_H = 16
Const BLINDS = 16
Const MANDEL_TOP = MENU_H
Dim SCREEN_W As Integer
Dim SCREEN_H As Integer
Dim MANDEL_W As Integer
Dim MANDEL_H As Integer
Dim BLOCK_W As Integer
Dim OVERLAY_MAX As Integer
SCREEN_W = MM.HRES
SCREEN_H = MM.VRES
MANDEL_W = SCREEN_W
MANDEL_H = SCREEN_H - MENU_H
BLOCK_W = MANDEL_W
' Worst-case perimeter of the zoom box is 2*(W+H); used to size the
' overlay save/restore buffers. Old formula assumed a square box.
OVERLAY_MAX = 2 * (MANDEL_W + MANDEL_H)

' -- Default fractal region. 3x3 in complex plane, centred at (-0.5, 0).
'    ResetViewport recomputes a uniform (complex-units-per-pixel) scale
'    so the region is always visible and pixels are square.
Const X_CENTER! = -0.5
Const Y_CENTER! = 0.0
Const BASE_SPAN! = 3.0
Const MAX_ITER = 512
Const STACK_MAX = 16
Const MENU_BG = RGB(40, 40, 40)
Const MENU_FG = RGB(255, 255, 255)
Const CURSOR_CLR = RGB(255, 255, 0)

Dim iterColor%(MAX_ITER)
Dim dyFix%
Dim xMinFix%, yMinFix%
Dim linebuf%(MANDEL_W - 1)
Dim colors%(255)
Dim block%(BLOCK_W - 1)
Dim startX%
Dim startY%
Dim delta%
Dim x%
Dim y%
Dim bCount%
Dim i%
Dim iter%
Dim dx As Float
Dim dy As Float
Dim g As Integer
Dim rc%
Dim gc%
Dim bc%
Dim xMin As Float
Dim xMax As Float
Dim yMin As Float
Dim yMax As Float
Dim currDX As Float
Dim currDY As Float
Dim xHist(STACK_MAX - 1) As Float
Dim xHistMax(STACK_MAX - 1) As Float
Dim yHist(STACK_MAX - 1) As Float
Dim yHistMax(STACK_MAX - 1) As Float
Dim stackPtr As Integer
Dim running As Integer
Dim keyCode As Integer
Dim overlayX%(OVERLAY_MAX - 1)
Dim overlayY%(OVERLAY_MAX - 1)
Dim overlayClr%(OVERLAY_MAX - 1)
Dim overlayCnt As Integer
Dim pendingKey%
Dim PALETTE_NAME$

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
  cy2% = (cy% * cy%) \ SCALE
  cx% = startX%
  For i% = 0 To bCount% - 1
    xQuarter% = cx% - QUARTER_SCALE
    q% = (xQuarter% * xQuarter%) \ SCALE + cy2%
    prod% = (q% * (q% + xQuarter%)) \ SCALE
    If prod% <= (cy2% >> 2) Then
      block%(i%) = maxIter%
    Else
      xBulb% = cx% + SCALE
      If ((xBulb% * xBulb%) \ SCALE + cy2%) <= SIXTEENTH_SCALE Then
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
        '!FAST
        Do While iter% < maxIter%
          nextX% = zx2% - zy2% + cx%
          nextY% = (zx% * zy%) \ HALF_SCALE + cy%
          zx% = nextX%
          zy% = nextY%
          zx2% = (zx% * zx%) \ SCALE
          zy2% = (zy% * zy%) \ SCALE
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

PALETTE_NAME$ = "rainbow"
pendingKey% = -1
Function Frac2(a!) As Float
  Frac2 = a! - 2 * Int(a! / 2)
End Function

Function HslRGB(h!, sPct!, lPct!) As Integer
  Local Float s, l, c, hp, x, m
  Local Float r1, g1, b1
  Local R%, G%, B%
  Local hi%

  If h! < 0 Then
    h! = 0
  ElseIf h! >= 360 Then
    h! = h! - 360 * Int(h! \ 360)
  EndIf
  If sPct! < 0 Then
    sPct! = 0
  ElseIf sPct! > 100 Then
    sPct! = 100
  EndIf
  If lPct! < 0 Then
    lPct! = 0
  ElseIf lPct! > 100 Then
    lPct! = 100
  EndIf

  s = sPct! / 100.0
  l = lPct! / 100.0
  c = (1 - Abs(2 * l - 1)) * s
  hp = h! / 60.0
  x = c * (1 - Abs(Frac2(hp) - 1))
  m = l - c / 2

  hi% = Int(hp)
  If hi% = 0 Then
    r1 = c : g1 = x : b1 = 0
  ElseIf hi% = 1 Then
    r1 = x : g1 = c : b1 = 0
  ElseIf hi% = 2 Then
    r1 = 0 : g1 = c : b1 = x
  ElseIf hi% = 3 Then
    r1 = 0 : g1 = x : b1 = c
  ElseIf hi% = 4 Then
    r1 = x : g1 = 0 : b1 = c
  Else
    r1 = c : g1 = 0 : b1 = x
  EndIf

  R% = Int((r1 + m) * 255 + 0.5)
  If R% < 0 Then
    R% = 0
  ElseIf R% > 255 Then
    R% = 255
  EndIf
  G% = Int((g1 + m) * 255 + 0.5)
  If G% < 0 Then
    G% = 0
  ElseIf G% > 255 Then
    G% = 255
  EndIf
  B% = Int((b1 + m) * 255 + 0.5)
  If B% < 0 Then
    B% = 0
  ElseIf B% > 255 Then
    B% = 255
  EndIf
  HslRGB = RGB(R%, G%, B%)
End Function

Function ConsumeKeyIfAny() As Integer
  Local k$
  k$ = INKEY$
  If k$ = "" Then
    ConsumeKeyIfAny = -1
  Else
    ConsumeKeyIfAny = ASC(k$)
  EndIf
End Function

Function GetKey() As Integer
  Local k$
  Do
    k$ = INKEY$
  Loop Until k$ <> ""
  GetKey = ASC(k$)
End Function

Function Fix28(v!) As Integer
  If v! >= 0 Then
    Fix28 = Int(v! * SCALE + 0.5)
  Else
    Fix28 = -Int(-v! * SCALE + 0.5)
  EndIf
End Function

Function ClampInt(v%, lo%, hi%) As Integer
  If v% < lo% Then
    ClampInt = lo%
  ElseIf v% > hi% Then
    ClampInt = hi%
  Else
    ClampInt = v%
  EndIf
End Function

Sub PushViewport()
  If stackPtr < STACK_MAX Then
    xHist(stackPtr) = xMin
    xHistMax(stackPtr) = xMax
    yHist(stackPtr) = yMin
    yHistMax(stackPtr) = yMax
    stackPtr = stackPtr + 1
  EndIf
End Sub

Function PopViewport() As Integer
  If stackPtr > 0 Then
    stackPtr = stackPtr - 1
    xMin = xHist(stackPtr)
    xMax = xHistMax(stackPtr)
    yMin = yHist(stackPtr)
    yMax = yHistMax(stackPtr)
    PopViewport = 1
  Else
    PopViewport = 0
  EndIf
End Function

Sub ResetViewport()
  ' Uniform complex-plane units per pixel — pick the larger of the two
  ' axis scales so the BASE_SPAN region always fits and pixels stay
  ' square. Wider screens see extra real range; taller ones extra
  ' imaginary range.
  Local Float scalePerPx
  If BASE_SPAN! / MANDEL_W > BASE_SPAN! / MANDEL_H Then
    scalePerPx = BASE_SPAN! / MANDEL_W
  Else
    scalePerPx = BASE_SPAN! / MANDEL_H
  EndIf
  xMin = X_CENTER! - scalePerPx * MANDEL_W / 2
  xMax = X_CENTER! + scalePerPx * MANDEL_W / 2
  yMin = Y_CENTER! - scalePerPx * MANDEL_H / 2
  yMax = Y_CENTER! + scalePerPx * MANDEL_H / 2
  stackPtr = 0
End Sub

Sub DrawMenu()
  Box 0, 0, SCREEN_W, MENU_H, 1, MENU_BG, MENU_BG
  Text 6, 2, "Z)oom  O)ut  R)eset  P)alette  Esc", , , , MENU_FG, MENU_BG
End Sub

Sub DrawZoomBox(boxX%, boxY%, boxW%, boxH%)
  Local Integer idx, xPix, yPix

  idx = 0
  ' top + bottom edges
  For xPix = boxX% To boxX% + boxW% - 1
    overlayX%(idx) = xPix
    overlayY%(idx) = boxY%
    overlayClr%(idx) = Pixel(xPix, boxY%)
    Pixel xPix, boxY%, CURSOR_CLR
    idx = idx + 1
  Next xPix
  For xPix = boxX% To boxX% + boxW% - 1
    overlayX%(idx) = xPix
    overlayY%(idx) = boxY% + boxH% - 1
    overlayClr%(idx) = Pixel(xPix, boxY% + boxH% - 1)
    Pixel xPix, boxY% + boxH% - 1, CURSOR_CLR
    idx = idx + 1
  Next xPix
  ' left + right edges (skip corners already done above)
  For yPix = boxY% + 1 To boxY% + boxH% - 2
    overlayX%(idx) = boxX%
    overlayY%(idx) = yPix
    overlayClr%(idx) = Pixel(boxX%, yPix)
    Pixel boxX%, yPix, CURSOR_CLR
    idx = idx + 1
    overlayX%(idx) = boxX% + boxW% - 1
    overlayY%(idx) = yPix
    overlayClr%(idx) = Pixel(boxX% + boxW% - 1, yPix)
    Pixel boxX% + boxW% - 1, yPix, CURSOR_CLR
    idx = idx + 1
  Next yPix
  overlayCnt = idx
End Sub

Sub UndrawZoomBox()
  Local Integer idx
  For idx = 0 To overlayCnt - 1
    Pixel overlayX%(idx), overlayY%(idx), overlayClr%(idx)
  Next idx
  overlayCnt = 0
End Sub

Sub RenderViewport()
  Local Integer rX, region, lineInRegion, y, startR, endR, linesSinceCheck, linesDrawn, maxLines

  If MANDEL_W > 1 Then
    dx = (xMax - xMin) / (MANDEL_W - 1)
  Else
    dx = 0
  EndIf
  If MANDEL_H > 1 Then
    dy = (yMax - yMin) / (MANDEL_H - 1)
  Else
    dy = 0
  EndIf
  currDX = dx
  currDY = dy

  delta% = Fix28(dx)
  If delta% = 0 Then delta% = 1
  dyFix% = Fix28(dy)
  xMinFix% = Fix28(xMin)
  yMinFix% = Fix28(yMin)

  linesSinceCheck = 0
  linesDrawn = 0
  maxLines = ((BLINDS - 1) * MANDEL_H) \ BLINDS
  If (MANDEL_H Mod BLINDS) <> 0 Then
    maxLines = ((MANDEL_H + BLINDS - 1) \ BLINDS)
  EndIf

  For lineInRegion = 0 To maxLines - 1
    For region = 0 To BLINDS - 1
      startR = (region * MANDEL_H) \ BLINDS
      endR = ((region + 1) * MANDEL_H) \ BLINDS - 1
      y = startR + lineInRegion
      If y <= endR Then
        linesSinceCheck = linesSinceCheck + 1
        If linesSinceCheck >= 10 Then
          pendingKey% = ConsumeKeyIfAny()
          If pendingKey% <> -1 Then
            Exit Sub
          EndIf
          linesSinceCheck = 0
        EndIf

        startY% = yMinFix% + dyFix% * y
        For rX = 0 To MANDEL_W - 1 Step BLOCK_W
          bCount% = MANDEL_W - rX
          If bCount% > BLOCK_W Then bCount% = BLOCK_W
          startX% = xMinFix% + delta% * rX
          Mandelbrot block%(), startX%, startY%, delta%, bCount%, MAX_ITER
          For i% = 0 To bCount% - 1
            iter% = block%(i%)
            If iter% > MAX_ITER Then iter% = MAX_ITER
            Pixel rX + i%, MANDEL_TOP + y, iterColor%(iter%)
          Next i%
        Next rX
        linesDrawn = linesDrawn + 1
      EndIf
    Next region
  Next lineInRegion
End Sub

Sub DoZoom()
  Local Integer boxW, boxH, moveStep, boxX, boxY, done, key, pixLeft, pixTop, pixRight, pixBottom
  Local Float newXMin, newXMax, newYMin, newYMax

  ' Box matches the viewport aspect ratio so zooming doesn't distort
  ' complex-plane pixels. Target roughly a third of the smaller axis,
  ' then scale the other axis to keep the aspect.
  If MANDEL_W < MANDEL_H Then
    boxW = MANDEL_W \ 3
    boxH = (boxW * MANDEL_H) \ MANDEL_W
  Else
    boxH = MANDEL_H \ 3
    boxW = (boxH * MANDEL_W) \ MANDEL_H
  EndIf
  If boxW < 12 Then boxW = 12
  If boxH < 12 Then boxH = 12

  moveStep = ClampInt(boxW \ 6, 1, boxW)
  boxX = (MANDEL_W - boxW) \ 2
  boxY = MANDEL_TOP + (MANDEL_H - boxH) \ 2

  DrawZoomBox boxX, boxY, boxW, boxH
  done = 0
  Do While done = 0
    key = GetKey()
    Select Case key
      Case 128
        UndrawZoomBox
        boxY = ClampInt(boxY - moveStep, MANDEL_TOP, MANDEL_TOP + MANDEL_H - boxH)
        DrawZoomBox boxX, boxY, boxW, boxH
      Case 129
        UndrawZoomBox
        boxY = ClampInt(boxY + moveStep, MANDEL_TOP, MANDEL_TOP + MANDEL_H - boxH)
        DrawZoomBox boxX, boxY, boxW, boxH
      Case 130
        UndrawZoomBox
        boxX = ClampInt(boxX - moveStep, 0, MANDEL_W - boxW)
        DrawZoomBox boxX, boxY, boxW, boxH
      Case 131
        UndrawZoomBox
        boxX = ClampInt(boxX + moveStep, 0, MANDEL_W - boxW)
        DrawZoomBox boxX, boxY, boxW, boxH
      Case 13
        UndrawZoomBox
        PushViewport
        pixLeft = boxX
        pixRight = boxX + boxW - 1
        pixTop = boxY - MANDEL_TOP
        pixBottom = pixTop + boxH - 1
        newXMin = xMin + currDX * pixLeft
        newXMax = xMin + currDX * pixRight
        newYMin = yMin + currDY * pixTop
        newYMax = yMin + currDY * pixBottom
        xMin = newXMin
        xMax = newXMax
        yMin = newYMin
        yMax = newYMax
        done = 1
      Case 27
        UndrawZoomBox
        done = 1
    End Select
  Loop
  CLS
  DrawMenu
  RenderViewport
End Sub

Sub BuildPalette(name$)
  Local i%
  Local Float hue, sat, lit
  Local nm$

  nm$ = LCase$(name$)
  For i% = 0 To MAX_ITER - 1
    If nm$ = "rainbow" Then
      hue = (720.0 * i% / MAX_ITER)
      hue = hue - 360.0 * Int(hue \ 360.0)
      sat = 90
      lit = 50
      iterColor%(i%) = HslRGB(hue, sat, lit)
    ElseIf nm$ = "blue" Then
      hue = (240.0 * i% / MAX_ITER)
      hue = hue - 120.0 * Int(hue \ 120.0)
      hue = hue + 180.0
      sat = 90
      lit = 50
      iterColor%(i%) = HslRGB(hue, sat, lit)
    Else
      hue = 220
      sat = 90
      lit = (150.0 * i% / MAX_ITER)
      lit = lit - 75.0 * Int(lit \ 75.0)
      iterColor%(i%) = HslRGB(hue, sat, lit)
    EndIf
  Next i%
  iterColor%(MAX_ITER) = RGB(0, 0, 0)
End Sub

CLS
BuildPalette PALETTE_NAME$
ResetViewport
DrawMenu
RenderViewport

running = 1
Do While running = 1
  If pendingKey% <> -1 Then
    keyCode = pendingKey%
    pendingKey% = -1
  Else
    keyCode = GetKey()
  EndIf

  Select Case keyCode
    Case 90, 122, 128, 129, 130, 131
      DoZoom
    Case 66, 98
    Case 79, 111
      If PopViewport() Then
        CLS
        DrawMenu
        RenderViewport
      EndIf
    Case 82, 114
      ResetViewport
      CLS
      DrawMenu
      RenderViewport
    Case 27
      running = 0
    Case 80, 112
      If LCase$(PALETTE_NAME$) = "blue" Then
        BuildPalette "blue2"
        PALETTE_NAME$ = "blue2"
      ElseIf LCase$(PALETTE_NAME$) = "blue2" Then
        BuildPalette "rainbow"
        PALETTE_NAME$ = "rainbow"
      Else
        BuildPalette "blue"
        PALETTE_NAME$ = "blue"
      EndIf
      CLS
      DrawMenu
      RenderViewport
  End Select
Loop

Save Image "out.bmp", 0, 0, SCREEN_W, SCREEN_H
