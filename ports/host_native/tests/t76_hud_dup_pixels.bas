OPTION EXPLICIT

CONST BG% = RGB(BLACK)
CONST FG% = RGB(WHITE)

DIM fps$

SUB DrawHUD()
  LOCAL s$
  BOX 0, 0, MM.HRES, 18, 0, , BG%
  s$ = "L1 SCORE 0 LIVES 3"
  TEXT 6, 3, s$, "LT", , , FG%, BG%
  IF fps$ <> "" THEN
    TEXT MM.HRES-4, 3, fps$, "RT", , , FG%, BG%
  END IF
END SUB

fps$ = ""
CLS BG%
DrawHUD
