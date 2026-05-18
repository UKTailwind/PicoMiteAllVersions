OPTION EXPLICIT
OPTION CONTINUATION LINES ON

CONST SCALE = 1073741824
CONST HALF_SCALE = 536870912
CONST ESCAPE_LIMIT = 4294967296
CONST MANDEL_W = 32
CONST MANDEL_H = 24
CONST BLINDS = 4
CONST BLOCK_W = 8
CONST MAX_ITER = 32
CONST X_MIN_DEF = -2.0
CONST X_MAX_DEF = 1.0
CONST Y_MIN_DEF = -1.5
CONST Y_MAX_DEF = 1.5

DIM iterColor%(MAX_ITER)
DIM block%(BLOCK_W - 1)
DIM dyFix%, xMinFix%, yMinFix%
DIM startX%, startY%, delta%
DIM i%, iter%
DIM dx AS FLOAT
DIM dy AS FLOAT
DIM xMin AS FLOAT
DIM xMax AS FLOAT
DIM yMin AS FLOAT
DIM yMax AS FLOAT
DIM gfxReady%

SUB EnsureFastGFX()
  IF gfxReady% = 0 THEN
    FASTGFX CREATE
    FASTGFX FPS 1000
    gfxReady% = 1
  ENDIF
END SUB

SUB PresentLine()
  FASTGFX SWAP
  FASTGFX SYNC
END SUB

FUNCTION Frac2(a!) AS FLOAT
  Frac2 = a! - 2 * INT(a! / 2)
END FUNCTION

FUNCTION HslRGB(h!, sPct!, lPct!) AS INTEGER
  LOCAL FLOAT s, l, c, hp, x, m
  LOCAL FLOAT r1, g1, b1
  LOCAL R%, G%, B%
  LOCAL hi%

  IF h! < 0 THEN
    h! = 0
  ELSEIF h! >= 360 THEN
    h! = h! - 360 * INT(h! \ 360)
  ENDIF
  IF sPct! < 0 THEN
    sPct! = 0
  ELSEIF sPct! > 100 THEN
    sPct! = 100
  ENDIF
  IF lPct! < 0 THEN
    lPct! = 0
  ELSEIF lPct! > 100 THEN
    lPct! = 100
  ENDIF

  s = sPct! / 100.0
  l = lPct! / 100.0
  c = (1 - ABS(2 * l - 1)) * s
  hp = h! / 60.0
  x = c * (1 - ABS(Frac2(hp) - 1))
  m = l - c / 2

  hi% = INT(hp)
  IF hi% = 0 THEN
    r1 = c : g1 = x : b1 = 0
  ELSEIF hi% = 1 THEN
    r1 = x : g1 = c : b1 = 0
  ELSEIF hi% = 2 THEN
    r1 = 0 : g1 = c : b1 = x
  ELSEIF hi% = 3 THEN
    r1 = 0 : g1 = x : b1 = c
  ELSEIF hi% = 4 THEN
    r1 = x : g1 = 0 : b1 = c
  ELSE
    r1 = c : g1 = 0 : b1 = x
  ENDIF

  R% = INT((r1 + m) * 255 + 0.5)
  IF R% < 0 THEN
    R% = 0
  ELSEIF R% > 255 THEN
    R% = 255
  ENDIF
  G% = INT((g1 + m) * 255 + 0.5)
  IF G% < 0 THEN
    G% = 0
  ELSEIF G% > 255 THEN
    G% = 255
  ENDIF
  B% = INT((b1 + m) * 255 + 0.5)
  IF B% < 0 THEN
    B% = 0
  ELSEIF B% > 255 THEN
    B% = 255
  ENDIF
  HslRGB = RGB(R%, G%, B%)
END FUNCTION

FUNCTION Fix28(v!) AS INTEGER
  IF v! >= 0 THEN
    Fix28 = INT(v! * SCALE + 0.5)
  ELSE
    Fix28 = -INT(-v! * SCALE + 0.5)
  ENDIF
END FUNCTION

SUB Mandelbrot(block%(), startX%, startY%, delta%, bCount%, maxIter%)
  LOCAL i%, iter%
  LOCAL cx%, cy%
  LOCAL zx%, zy%
  LOCAL zx2%, zy2%
  LOCAL nextX%, nextY%

  cy% = startY%
  cx% = startX%
  FOR i% = 0 TO bCount% - 1
    zx% = 0
    zy% = 0
    iter% = 0
    DO WHILE iter% < maxIter%
      zx2% = (zx% * zx%) \ SCALE
      zy2% = (zy% * zy%) \ SCALE
      nextX% = zx2% - zy2% + cx%
      nextY% = ((zx% * zy%) \ HALF_SCALE) + cy%
      zx% = nextX%
      zy% = nextY%
      IF ((zx% * zx%) \ SCALE) + ((zy% * zy%) \ SCALE) > ESCAPE_LIMIT THEN EXIT DO
      iter% = iter% + 1
    LOOP
    block%(i%) = iter%
    cx% = cx% + delta%
  NEXT i%
END SUB

SUB BuildPalette()
  LOCAL i%
  LOCAL FLOAT hue
  FOR i% = 0 TO MAX_ITER - 1
    hue = (720.0 * i% / MAX_ITER)
    hue = hue - 360.0 * INT(hue \ 360.0)
    iterColor%(i%) = HslRGB(hue, 90, 50)
  NEXT i%
  iterColor%(MAX_ITER) = RGB(0, 0, 0)
END SUB

SUB RenderViewport()
  LOCAL INTEGER rX, region, lineInRegion, y, startR, endR, bCount%
  LOCAL INTEGER maxLines

  EnsureFastGFX

  dx = (xMax - xMin) / (MANDEL_W - 1)
  dy = (yMax - yMin) / (MANDEL_H - 1)
  delta% = Fix28(dx)
  IF delta% = 0 THEN delta% = 1
  dyFix% = Fix28(dy)
  xMinFix% = Fix28(xMin)
  yMinFix% = Fix28(yMin)

  maxLines = ((BLINDS - 1) * MANDEL_H) \ BLINDS
  IF (MANDEL_H MOD BLINDS) <> 0 THEN
    maxLines = ((MANDEL_H + BLINDS - 1) \ BLINDS)
  ENDIF

  FOR lineInRegion = 0 TO maxLines - 1
    FOR region = 0 TO BLINDS - 1
      startR = (region * MANDEL_H) \ BLINDS
      endR = ((region + 1) * MANDEL_H) \ BLINDS - 1
      y = startR + lineInRegion
      IF y <= endR THEN
        startY% = yMinFix% + dyFix% * y
        FOR rX = 0 TO MANDEL_W - 1 STEP BLOCK_W
          bCount% = MANDEL_W - rX
          IF bCount% > BLOCK_W THEN bCount% = BLOCK_W
          startX% = xMinFix% + delta% * rX
          Mandelbrot block%(), startX%, startY%, delta%, bCount%, MAX_ITER
          FOR i% = 0 TO bCount% - 1
            iter% = block%(i%)
            IF iter% > MAX_ITER THEN iter% = MAX_ITER
            PIXEL rX + i%, y, iterColor%(iter%)
          NEXT i%
        NEXT rX
        PresentLine
      ENDIF
    NEXT region
  NEXT lineInRegion
END SUB

CLS RGB(BLACK)
BuildPalette
xMin = X_MIN_DEF
xMax = X_MAX_DEF
yMin = Y_MIN_DEF
yMax = Y_MAX_DEF
RenderViewport
