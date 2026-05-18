OPTION EXPLICIT

DIM fps$

SUB DrawHUD()
  LOCAL s$
  s$ = "L1 SCORE 0 LIVES 3"
  PRINT "before-if"; LEN(fps$); ":"; fps$
  TEXT 6, 3, s$, "LT", , , RGB(WHITE), RGB(BLACK)
  IF fps$ <> "" THEN
    PRINT "right"; LEN(fps$); ":"; fps$
    TEXT MM.HRES-4, 3, fps$, "RT", , , RGB(WHITE), RGB(BLACK)
  END IF
END SUB

fps$ = ""
CLS RGB(BLACK)
DrawHUD
PRINT "after"; LEN(fps$); ":"; fps$
