OPTION EXPLICIT
DIM i%
DIM j%

FOR i% = 1 TO 4
  IF i% <= 3 THEN PRINT "A";
NEXT i%
PRINT

j% = 0
DO WHILE j% < 3
  PRINT j%;
  j% = j% + 1
LOOP
PRINT
