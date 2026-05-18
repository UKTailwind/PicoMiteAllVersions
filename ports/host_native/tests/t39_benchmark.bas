' Performance benchmark: Mandelbrot computation (no output, just count)
' This tests raw compute performance of the VM vs interpreter
DIM cx!, cy!, zx!, zy!, tmp!, i%, j%, iter%, maxiter%, inside%
maxiter% = 100
inside% = 0
FOR j% = 0 TO 49
  FOR i% = 0 TO 79
    cx! = (i% - 52) / 24.0
    cy! = (j% - 25) / 20.0
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
    IF iter% = maxiter% THEN inside% = inside% + 1
  NEXT i%
NEXT j%
PRINT "Inside points:"; inside%
