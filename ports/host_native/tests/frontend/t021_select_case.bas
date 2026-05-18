FOR k% = 1 TO 3
  SELECT CASE k%
    CASE 1
      PRINT "one"
    CASE 2, 3
      PRINT "two-three"
    CASE ELSE
      PRINT "bad"
  END SELECT
NEXT k%
