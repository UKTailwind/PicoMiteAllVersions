' Test deeply nested loops and complex iteration patterns
' Triple nested FOR
DIM i%, j%, k%, count%
count% = 0
FOR i% = 1 TO 5
  FOR j% = 1 TO 5
    FOR k% = 1 TO 5
      IF i% + j% + k% = 10 THEN count% = count% + 1
    NEXT k%
  NEXT j%
NEXT i%
PRINT "Triples summing to 10:"; count%
' Collatz sequence
FUNCTION CollatzLen%(n%)
  LOCAL len%, val%
  val% = n%
  len% = 0
  DO WHILE val% <> 1
    IF val% MOD 2 = 0 THEN
      val% = val% \ 2
    ELSE
      val% = 3 * val% + 1
    ENDIF
    len% = len% + 1
  LOOP
  CollatzLen% = len%
END FUNCTION
PRINT "Collatz(1):"; CollatzLen%(1)
PRINT "Collatz(6):"; CollatzLen%(6)
PRINT "Collatz(27):"; CollatzLen%(27)
' Find longest Collatz under 100
DIM maxlen%, maxn%, clen%
maxlen% = 0
FOR i% = 1 TO 100
  clen% = CollatzLen%(i%)
  IF clen% > maxlen% THEN
    maxlen% = clen%
    maxn% = i%
  ENDIF
NEXT i%
PRINT "Longest under 100: n="; maxn%; " len="; maxlen%
' Pascal's triangle (first 8 rows)
DIM row%(10), prev%(10)
row%(0) = 1
FOR i% = 0 TO 7
  FOR j% = 0 TO 10
    prev%(j%) = row%(j%)
  NEXT j%
  FOR j% = 0 TO i%
    IF j% = 0 OR j% = i% THEN
      row%(j%) = 1
    ELSE
      row%(j%) = prev%(j% - 1) + prev%(j%)
    ENDIF
    PRINT row%(j%);
    IF j% < i% THEN PRINT " ";
  NEXT j%
  PRINT ""
NEXT i%
