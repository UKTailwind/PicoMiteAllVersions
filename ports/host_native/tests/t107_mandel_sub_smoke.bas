OPTION EXPLICIT
OPTION CONTINUATION LINES ON

CONST SCALE = 1073741824
CONST HALF_SCALE = 536870912
CONST ESCAPE_LIMIT = 4294967296
CONST BLOCK_W = 8

DIM block%(BLOCK_W - 1)
DIM i%

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

Mandelbrot block%(), -2147483648, -1610612736, 100663296, BLOCK_W, 32

FOR i% = 0 TO BLOCK_W - 1
  PRINT block%(i%)
NEXT i%
