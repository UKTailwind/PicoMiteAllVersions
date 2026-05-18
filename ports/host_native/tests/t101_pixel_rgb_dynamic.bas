OPTION EXPLICIT

DIM pal%(3)
DIM r% : DIM g% : DIM b%

FUNCTION MakeColor(r AS INTEGER, g AS INTEGER, b AS INTEGER) AS INTEGER
  MakeColor = RGB(r, g, b)
END FUNCTION

CLS RGB(BLACK)

r% = 255 : g% = 0   : b% = 0
pal%(0) = RGB(r%, g%, b%)
r% = 0   : g% = 255 : b% = 0
pal%(1) = RGB(r%, g%, b%)
r% = 0   : g% = 0   : b% = 255
pal%(2) = RGB(r%, g%, b%)
pal%(3) = MakeColor(255, 255, 0)

PIXEL 20, 20, pal%(0)
PIXEL 22, 20, pal%(1)
PIXEL 24, 20, pal%(2)
PIXEL 26, 20, pal%(3)
