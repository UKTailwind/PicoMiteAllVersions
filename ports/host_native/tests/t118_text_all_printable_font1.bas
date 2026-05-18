OPTION EXPLICIT

DIM c%, x%, y%

CLS RGB(BLACK)
FOR c% = 32 TO 126
  x% = ((c% - 32) MOD 16) * 12
  y% = ((c% - 32) \ 16) * 14
  TEXT x%, y%, CHR$(c%), "LT", , , RGB(WHITE), RGB(BLACK)
NEXT c%
