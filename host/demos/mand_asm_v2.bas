' Mandelbrot renderer with menu (PicoMite MMBasic) — '!ASM inner loop, v2.
' Same shell as host/demos/mand.bas (the SD-card res-independent
' version) but the iteration kernel is hand-written VM assembly using
' the bytecode VM's mulshr / sqrshr fixed-point intrinsics. Requires
' FRUN (the bytecode VM); the interpreter doesn't grok '!ASM blocks.
'
' v2 vs v1 (host/demos/mand_asm.bas):
'   - drop `checkint` from the inner loop. '!FAST never emits it
'     (bc_source.c's DO/LOOP compiler doesn't emit OP_CHECKINT at
'     backward jumps), so v1 was paying one CheckAbort + check_interrupt
'     pair per Mandelbrot iteration that the reference mand.bas never
'     pays. Ctrl-C / Esc responsiveness still comes from
'     RenderViewport's `ConsumeKeyIfAny()` poll every 10 lines.
'   - replace the top-of-loop `addi iter,1; jge iter,max,.escaped` +
'     the bottom-of-loop unconditional `jmp .loop` with a single
'     conditional back-jump at the bottom (`addi iter,1;
'     jlt iter,max,.loop` then fall through to .interior). One fewer
'     op per iter, semantics preserved.
OPTION EXPLICIT
OPTION CONTINUATION LINES ON

Const SCALE = 1073741824
Const HALF_SCALE = 536870912
Const QUARTER_SCALE = 268435456
Const SIXTEENTH_SCALE = 67108864
Const ESCAPE_LIMIT = 4294967296
Const SCREEN_W = MM.HRES   ' adaptive (was 320)
Const SCREEN_H = MM.VRES   ' adaptive (was 320)
Const MENU_H = 16
Const BLINDS = 16
Const MANDEL_W = SCREEN_W
Const MANDEL_H = SCREEN_H - MENU_H
Const MANDEL_TOP = MENU_H
Const BLOCK_W = SCREEN_W
Const X_MIN_DEF = -2.0
Const X_MAX_DEF = 1.0
Const Y_MIN_DEF = -1.5
Const Y_MAX_DEF = 1.5
Const MAX_ITER = 512
Const STACK_MAX = 16
Const MENU_BG = RGB(40, 40, 40)
Const MENU_FG = RGB(255, 255, 255)
Const CURSOR_CLR = RGB(255, 255, 0)
Const OVERLAY_MAX = MANDEL_W * 4

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

' Inline-assembly Mandelbrot kernel. Q2.30 fixed-point in int64
' (SCALE = 2^30, HALF_SCALE = 2^29, ESCAPE_LIMIT = 2^32).
'
' Three early-exit gates pulled from the BASIC reference:
'   1. Cardioid test            (rejects main cardioid)
'   2. Period-2 bulb test       (rejects the disc at -1)
'   3. Periodicity detection    (catches everything else interior)
'
' Inner loop: 12 ops on the common path via four tricks from
' docs/asm-syntax.md —
'   * mulshradd to fuse zy = (zx*zy)>>29 + cy into one op
'   * carry zx² / zy² across iterations so each iter only squares once
'   * write new zy/zx in place (no temp + mov, since after squaring,
'     mulshradd captures old zx/zy as inputs before writing zy)
'   * conditional back-jump at the bottom: `addi iter,1;
'     jlt iter,max,.loop` then fall through to .interior. Avoids both
'     a top-of-loop max-test branch and an unconditional `jmp .loop`.
' Iteration count matches the BASIC reference exactly.
Sub Mandelbrot(block%(), startX%, startY%, delta%, bCount%, maxIter%)
  Local i%, iter%
  Local cx%, cy%, cy2%
  Local xQuarter%, q%, qTemp%, prod%, cy2Q%
  Local xBulb%, bulb%
  Local zx%, zy%, zx2%, zy2%, mag%
  Local periodCount%, periodZX%, periodZY%

  cy% = startY%
  cy2% = (cy% * cy%) \ SCALE
  cx% = startX%
  For i% = 0 To bCount% - 1
    '!ASM
    .const BITS30,        30
    .const BITS29,        29
    .const TWO_BITS,      2
    .const ESC_FP,        4294967296
    .const QUARTER_FP,    268435456
    .const SCALE_FP,      1073741824
    .const SIXTEENTH_FP,  67108864
    .const PERIOD_INT,    16
    .const ZERO,          0
    .const ONE,           1

        ; --- Cardioid test: q*(q + cx-1/4) <= cy²/4  → interior
        subi      xQuarter, cx, QUARTER_FP    ; cx - 1/4
        sqrshr    q, xQuarter, BITS30         ; (cx-1/4)²
        addi      q, q, cy2                   ; + cy²
        addi      qTemp, q, xQuarter          ; q + (cx-1/4)
        mulshr    prod, q, qTemp, BITS30      ; q*(q+xQ)
        shr       cy2Q, cy2, TWO_BITS         ; cy²/4
        jle       prod, cy2Q, .interior

        ; --- Period-2 bulb test: (cx+1)² + cy² <= 1/16  → interior
        addi      xBulb, cx, SCALE_FP         ; cx + 1
        sqrshr    bulb, xBulb, BITS30         ; (cx+1)²
        addi      bulb, bulb, cy2             ; + cy²
        jle       bulb, SIXTEENTH_FP, .interior

        ; --- Iterate z_{n+1} = z_n² + c
        mov       zx, ZERO
        mov       zy, ZERO
        mov       zx2, ZERO
        mov       zy2, ZERO
        mov       iter, ZERO
        mov       periodCount, ZERO
        mov       periodZX, ZERO
        mov       periodZY, ZERO
    .loop:
        ; compute new z = z² + c using cached old zx²/zy²
        mulshradd zy, zx, zy, BITS29, cy      ; new zy = (old_zx*old_zy)>>29 + cy
        subi      zx, zx2, zy2                ; new zx = old_zx² - old_zy²
        addi      zx, zx, cx                  ;    + cx
        ; square new z, magnitude check
        sqrshr    zx2, zx, BITS30
        sqrshr    zy2, zy, BITS30
        addi      mag, zx2, zy2
        jgt       mag, ESC_FP, .escaped       ; escape: exit WITHOUT bumping iter
        ; periodicity check: did z return to a snapshot?
        jne       zx, periodZX, .periodNoMatch
        jne       zy, periodZY, .periodNoMatch
        jmp       .interior                   ; both match → interior point
    .periodNoMatch:
        addi      periodCount, periodCount, ONE
        jlt       periodCount, PERIOD_INT, .periodNoSnap
        mov       periodCount, ZERO
        mov       periodZX, zx
        mov       periodZY, zy
    .periodNoSnap:
        addi      iter, iter, ONE
        jlt       iter, maxIter, .loop        ; loop back if still under cap
        ; fall through to .interior when iter has reached maxIter
    .interior:
        mov       iter, maxIter
    .escaped:
        exit
    '!ENDASM

    block%(i%) = iter%
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
  xMin = X_MIN_DEF
  xMax = X_MAX_DEF
  yMin = Y_MIN_DEF
  yMax = Y_MAX_DEF
  stackPtr = 0
End Sub

Sub DrawMenu()
  Box 0, 0, SCREEN_W, MENU_H, 1, MENU_BG, MENU_BG
  Text 6, 2, "Z)oom  O)ut  R)eset  P)alette  Esc", , , , MENU_FG, MENU_BG
End Sub

Sub DrawZoomBox(boxX%, boxY%, boxSize%)
  Local Integer idx, xPix, yPix

  idx = 0
  For xPix = boxX% To boxX% + boxSize% - 1
    overlayX%(idx) = xPix
    overlayY%(idx) = boxY%
    overlayClr%(idx) = Pixel(xPix, boxY%)
    Pixel xPix, boxY%, CURSOR_CLR
    idx = idx + 1
  Next xPix
  For xPix = boxX% To boxX% + boxSize% - 1
    overlayX%(idx) = xPix
    overlayY%(idx) = boxY% + boxSize% - 1
    overlayClr%(idx) = Pixel(xPix, boxY% + boxSize% - 1)
    Pixel xPix, boxY% + boxSize% - 1, CURSOR_CLR
    idx = idx + 1
  Next xPix
  For yPix = boxY% + 1 To boxY% + boxSize% - 2
    overlayX%(idx) = boxX%
    overlayY%(idx) = yPix
    overlayClr%(idx) = Pixel(boxX%, yPix)
    Pixel boxX%, yPix, CURSOR_CLR
    idx = idx + 1
    overlayX%(idx) = boxX% + boxSize% - 1
    overlayY%(idx) = yPix
    overlayClr%(idx) = Pixel(boxX% + boxSize% - 1, yPix)
    Pixel boxX% + boxSize% - 1, yPix, CURSOR_CLR
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
  ' Square-pixel correction: expand the smaller-step axis so the
  ' Mandelbrot stays geometrically square on non-square displays.
  If dy > dx Then
    Local xMid : xMid = (xMin + xMax) / 2
    dx = dy
    xMin = xMid - dx * (MANDEL_W - 1) / 2
    xMax = xMid + dx * (MANDEL_W - 1) / 2
  ElseIf dx > dy Then
    Local yMid : yMid = (yMin + yMax) / 2
    dy = dx
    yMin = yMid - dy * (MANDEL_H - 1) / 2
    yMax = yMid + dy * (MANDEL_H - 1) / 2
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
  Local Integer boxSize, moveStep, boxX, boxY, done, key, pixLeft, pixTop, pixRight, pixBottom
  Local Float newXMin, newXMax, newYMin, newYMax

  boxSize = MANDEL_W \ 3
  If boxSize < 12 Then boxSize = 12
  moveStep = ClampInt(boxSize \ 6, 1, boxSize)
  boxX = (MANDEL_W - boxSize) \ 2
  boxY = MANDEL_TOP + (MANDEL_H - boxSize) \ 2

  DrawZoomBox boxX, boxY, boxSize
  done = 0
  Do While done = 0
    key = GetKey()
    Select Case key
      Case 128
        UndrawZoomBox
        boxY = ClampInt(boxY - moveStep, MANDEL_TOP, MANDEL_TOP + MANDEL_H - boxSize)
        DrawZoomBox boxX, boxY, boxSize
      Case 129
        UndrawZoomBox
        boxY = ClampInt(boxY + moveStep, MANDEL_TOP, MANDEL_TOP + MANDEL_H - boxSize)
        DrawZoomBox boxX, boxY, boxSize
      Case 130
        UndrawZoomBox
        boxX = ClampInt(boxX - moveStep, 0, MANDEL_W - boxSize)
        DrawZoomBox boxX, boxY, boxSize
      Case 131
        UndrawZoomBox
        boxX = ClampInt(boxX + moveStep, 0, MANDEL_W - boxSize)
        DrawZoomBox boxX, boxY, boxSize
      Case 13
        UndrawZoomBox
        PushViewport
        pixLeft = boxX
        pixRight = boxX + boxSize - 1
        pixTop = boxY - MANDEL_TOP
        pixBottom = pixTop + boxSize - 1
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
