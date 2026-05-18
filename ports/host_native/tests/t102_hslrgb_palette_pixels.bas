OPTION EXPLICIT

CONST MAX_ITER = 4
DIM iterColor%(MAX_ITER)

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

iterColor%(0) = HslRGB(0, 90, 50)
iterColor%(1) = HslRGB(60, 90, 50)
iterColor%(2) = HslRGB(120, 90, 50)
iterColor%(3) = HslRGB(240, 90, 50)
iterColor%(4) = RGB(0, 0, 0)

PRINT iterColor%(0)
PRINT iterColor%(1)
PRINT iterColor%(2)
PRINT iterColor%(3)
PRINT iterColor%(4)

CLS RGB(BLACK)
PIXEL 20, 20, iterColor%(0)
PIXEL 22, 20, iterColor%(1)
PIXEL 24, 20, iterColor%(2)
PIXEL 26, 20, iterColor%(3)
PIXEL 28, 20, iterColor%(4)
