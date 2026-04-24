PRINT "Mandelbrot (text mode)"
PRINT "======================"
DIM cx%, cy%, iter%, maxiter%
DIM x!, y!, zr!, zi!, tr!, ti!, t!
maxiter% = 30
t! = TIMER
FOR cy% = -12 TO 12
  FOR cx% = -39 TO 16
    x! = cx% / 16.0
    y! = cy% / 12.0
    zr! = 0 : zi! = 0
    iter% = 0
    DO WHILE iter% < maxiter%
      tr! = zr! * zr! - zi! * zi! + x!
      ti! = 2 * zr! * zi! + y!
      zr! = tr! : zi! = ti!
      IF zr! * zr! + zi! * zi! > 4 THEN EXIT DO
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
  NEXT cx%
  PRINT
NEXT cy%
t! = TIMER - t!
PRINT "Time: "; t!; " sec"
