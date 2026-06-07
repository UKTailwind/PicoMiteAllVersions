' mand.bas - short Mandelbrot benchmark for RUN and FRUN / bytecode !FAST
' Use FRUN "mand.bas" to exercise the bytecode optimizer path.
OPTION EXPLICIT

CONST SCALE = 1073741824
CONST HALF_SCALE = 536870912
CONST QUARTER_SCALE = 268435456
CONST SIXTEENTH_SCALE = 67108864
CONST ESCAPE_LIMIT = 4294967296
CONST WIDTH = 64
CONST HEIGHT = 48
CONST MAX_ITER = 96

DIM INTEGER y%, checksum%
DIM FLOAT t0, elapsed

t0 = TIMER
checksum% = 0

FOR y% = 0 TO HEIGHT - 1
  MandelLine -2147483648, -1073741824 + y% * 44739243, 67108864, WIDTH, MAX_ITER
NEXT y%

elapsed = TIMER - t0
PRINT "Mandelbrot ", WIDTH; "x"; HEIGHT; " iter "; MAX_ITER
PRINT "Checksum "; checksum%
PRINT "Elapsed ms "; elapsed
IF elapsed > 0 THEN PRINT "Pixels/sec "; INT((WIDTH * HEIGHT * 1000) / elapsed)
END

SUB MandelLine(startX%, cy%, delta%, count%, maxIter%)
  LOCAL INTEGER i%, iter%
  LOCAL INTEGER cx%, zx%, zy%, zx2%, zy2%
  LOCAL INTEGER nextX%, nextY%
  LOCAL INTEGER cy2%, xQuarter%, q%, xBulb%, prod%
  LOCAL INTEGER periodCount%, periodZX%, periodZY%

  cy2% = (cy% * cy%) \ SCALE
  cx% = startX%

  FOR i% = 0 TO count% - 1
    xQuarter% = cx% - QUARTER_SCALE
    q% = (xQuarter% * xQuarter%) \ SCALE + cy2%
    prod% = (q% * (q% + xQuarter%)) \ SCALE

    IF prod% <= (cy2% >> 2) THEN
      iter% = maxIter%
    ELSE
      xBulb% = cx% + SCALE
      IF ((xBulb% * xBulb%) \ SCALE + cy2%) <= SIXTEENTH_SCALE THEN
        iter% = maxIter%
      ELSE
        zx% = 0
        zy% = 0
        zx2% = 0
        zy2% = 0
        iter% = 0
        periodCount% = 0
        periodZX% = 0
        periodZY% = 0

        '!FAST
        DO WHILE iter% < maxIter%
          nextX% = zx2% - zy2% + cx%
          nextY% = (zx% * zy%) \ HALF_SCALE + cy%
          zx% = nextX%
          zy% = nextY%
          zx2% = (zx% * zx%) \ SCALE
          zy2% = (zy% * zy%) \ SCALE
          IF zx2% + zy2% > ESCAPE_LIMIT THEN EXIT DO
          iter% = iter% + 1
          IF zx% = periodZX% THEN
            IF zy% = periodZY% THEN
              iter% = maxIter%
              EXIT DO
            ENDIF
          ENDIF
          periodCount% = periodCount% + 1
          IF periodCount% >= 16 THEN
            periodCount% = 0
            periodZX% = zx%
            periodZY% = zy%
          ENDIF
        LOOP
      ENDIF
    ENDIF
    checksum% = checksum% + iter% * ((i% AND 7) + 1)
    cx% = cx% + delta%
  NEXT i%
END SUB
