' Mandelbrot set computation — performance benchmark
' Renders a 40x20 ASCII Mandelbrot
DIM cx!, cy!, zx!, zy!, tmp!, i%, j%, iter%, maxiter%
maxiter% = 50
FOR j% = 0 TO 19
  FOR i% = 0 TO 39
    cx! = (i% - 26) / 12.0
    cy! = (j% - 10) / 10.0
    zx! = 0
    zy! = 0
    iter% = 0
    DO WHILE iter% < maxiter%
      tmp! = zx! * zx! - zy! * zy! + cx!
      zy! = 2 * zx! * zy! + cy!
      zx! = tmp!
      IF zx! * zx! + zy! * zy! > 4 THEN EXIT DO
      iter% = iter% + 1
    LOOP
    IF iter% = maxiter% THEN
      PRINT "*";
    ELSEIF iter% > 20 THEN
      PRINT "#";
    ELSEIF iter% > 10 THEN
      PRINT "+";
    ELSEIF iter% > 5 THEN
      PRINT ".";
    ELSE
      PRINT " ";
    ENDIF
  NEXT i%
  PRINT ""
NEXT j%
