OPTION EXPLICIT

FUNCTION HitCheck%(n%)
  LOCAL i%, j%
  FOR i% = 1 TO 2
    FOR j% = 1 TO 2
      IF n% > 0 THEN
        HitCheck% = 1
        EXIT FUNCTION
      END IF
    NEXT j%
  NEXT i%
  HitCheck% = 0
END FUNCTION

DIM INTEGER k%, total%
FOR k% = 1 TO 40
  total% = total% + HitCheck%(1)
NEXT k%
PRINT total%
