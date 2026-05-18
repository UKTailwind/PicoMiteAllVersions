FOR i% = 48 TO 57
  SELECT CASE i%
    CASE 49 TO 56
      PRINT "digit"; i%
    CASE ELSE
      PRINT "other"; i%
  END SELECT
NEXT i%
