OPTION EXPLICIT

CONST W% = MM.HRES
CONST H% = MM.VRES
CONST BRADIUS = 6
CONST COL_BG% = RGB(BLACK)
CONST COL_PAD% = RGB(GREEN)
CONST COL_BALL% = RGB(RED)

DIM FLOAT bx!, by!
DIM INTEGER br%
DIM px!, py!, pw%, ph%

SUB Draw3DHighlight(x%, y%, w%, h%)
  LINE x%, y%, x%+w%-1, y%, , RGB(WHITE)
  LINE x%, y%, x%, y%+h%-1, , RGB(WHITE)
END SUB

SUB DrawPaddleAt(x%, y%)
  BOX x%, y%, pw%, ph%, 0, , COL_PAD%
  Draw3DHighlight x%, y%, pw%, ph%
END SUB

SUB DrawBallAt(x%, y%)
  LOCAL cx%, cy%
  cx% = x% + br%
  cy% = y% + br%
  CIRCLE cx%, cy%, br%, 0, 1.0, , COL_BALL%
  CIRCLE cx%-2, cy%-2, 1, 0, 1.0, , RGB(WHITE)
END SUB

pw% = W% \ 6 : IF pw% < 30 THEN pw% = 30
ph% = 6
px! = (W% - pw%) / 2
py! = H% - (ph% + 6)
br% = BRADIUS
bx! = W% \ 2 : by! = H% \ 2

CLS COL_BG%
DrawPaddleAt INT(px!), INT(py!)
DrawBallAt INT(bx!), INT(by!)
PRINT "done"
END
