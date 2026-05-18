CONST VAL_A% = 30
CONST VAL_B% = 12

FUNCTION Lookup(x%) AS INTEGER
  SELECT CASE x%
    CASE VAL_A%
      Lookup = 111
    CASE VAL_B%
      Lookup = 222
    CASE ELSE
      Lookup = 999
  END SELECT
END FUNCTION

PRINT "direct:"; Lookup(30); Lookup(12); Lookup(0)

DIM INTEGER arr%(3)
arr%(0) = 30
arr%(1) = 12
arr%(2) = 0

DIM i%, r%
FOR i% = 0 TO 2
  r% = Lookup(arr%(i%))
  PRINT "arr("; i%; ")="; arr%(i%); " result="; r%
NEXT
END
