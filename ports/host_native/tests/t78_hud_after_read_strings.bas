OPTION EXPLICIT

DIM fps$

DATA "R","R","R","R","R","R","R","R"
DATA "R","R","R","R","R","R","R","R"
DATA "R","R","R","R","R","R","R","R"
DATA "R","R","R","R","R","R","R","R"

SUB InitBlocks()
  LOCAL blockChar$, i%
  RESTORE
  FOR i% = 1 TO 32
    READ blockChar$
  NEXT
END SUB

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
InitBlocks
CLS RGB(BLACK)
DrawHUD
PRINT "after"; LEN(fps$); ":"; fps$
