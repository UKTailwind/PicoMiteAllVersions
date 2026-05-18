' Test deeply nested control flow
DIM x%, y%, count%, sum%, n%, v%, total%
' Nested FOR with EXIT FOR
count% = 0
FOR x% = 1 TO 10
  FOR y% = 1 TO 10
    IF x% * y% > 20 THEN EXIT FOR
    count% = count% + 1
  NEXT y%
  IF x% > 5 THEN EXIT FOR
NEXT x%
PRINT "Nested exit count:"; count%
' DO WHILE inside FOR
sum% = 0
FOR x% = 1 TO 5
  n% = x%
  DO WHILE n% > 0
    sum% = sum% + n%
    n% = n% - 1
  LOOP
NEXT x%
PRINT "DO in FOR sum:"; sum%
' Nested SELECT CASE
FOR x% = 1 TO 3
  SELECT CASE x%
    CASE 1
      PRINT "outer 1:";
      v% = 10
      SELECT CASE v%
        CASE 10
          PRINT " inner 10"
        CASE ELSE
          PRINT " inner other"
      END SELECT
    CASE 2
      PRINT "outer 2:";
      v% = 20
      SELECT CASE v%
        CASE 10
          PRINT " inner 10"
        CASE 20
          PRINT " inner 20"
        CASE ELSE
          PRINT " inner other"
      END SELECT
    CASE 3
      PRINT "outer 3: done"
  END SELECT
NEXT x%
' Complex IF chains
FOR x% = 1 TO 20
  IF x% MOD 15 = 0 THEN
    PRINT "FizzBuzz";
  ELSEIF x% MOD 3 = 0 THEN
    PRINT "Fizz";
  ELSEIF x% MOD 5 = 0 THEN
    PRINT "Buzz";
  ELSE
    PRINT x%;
  ENDIF
  IF x% < 20 THEN PRINT ",";
NEXT x%
PRINT ""
' FOR with STEP
total% = 0
FOR x% = 10 TO 1 STEP -2
  total% = total% + x%
NEXT x%
PRINT "Step -2 total:"; total%
total% = 0
FOR x% = 0 TO 100 STEP 7
  total% = total% + 1
NEXT x%
PRINT "Step 7 count:"; total%
