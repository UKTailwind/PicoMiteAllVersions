' Test string processing with functions
FUNCTION CountChar%(s$, c$)
  LOCAL i%, n%
  n% = 0
  FOR i% = 1 TO LEN(s$)
    IF MID$(s$, i%, 1) = c$ THEN n% = n% + 1
  NEXT i%
  CountChar% = n%
END FUNCTION
FUNCTION ReverseStr$(s$)
  LOCAL i%
  LOCAL r$
  r$ = ""
  FOR i% = LEN(s$) TO 1 STEP -1
    r$ = r$ + MID$(s$, i%, 1)
  NEXT i%
  ReverseStr$ = r$
END FUNCTION
FUNCTION IsPalindrome%(s$)
  LOCAL lo%, hi%
  lo% = 1
  hi% = LEN(s$)
  IsPalindrome% = 1
  DO WHILE lo% < hi%
    IF MID$(s$, lo%, 1) <> MID$(s$, hi%, 1) THEN
      IsPalindrome% = 0
      EXIT DO
    ENDIF
    lo% = lo% + 1
    hi% = hi% - 1
  LOOP
END FUNCTION
FUNCTION RepeatStr$(s$, n%)
  LOCAL i%
  LOCAL r$
  r$ = ""
  FOR i% = 1 TO n%
    r$ = r$ + s$
  NEXT i%
  RepeatStr$ = r$
END FUNCTION
DIM t$
t$ = "Hello, World!"
PRINT "Length:"; LEN(t$)
PRINT "Upper: "; UCASE$(t$)
PRINT "Lower: "; LCASE$(t$)
PRINT "Left5: "; LEFT$(t$, 5)
PRINT "Right6: "; RIGHT$(t$, 6)
PRINT "Mid 8,5: "; MID$(t$, 8, 5)
PRINT "Count l:"; CountChar%(t$, "l")
PRINT "Rev: "; ReverseStr$("ABCDE")
PRINT "Pal1:"; IsPalindrome%("racecar")
PRINT "Pal2:"; IsPalindrome%("hello")
PRINT "Pal3:"; IsPalindrome%("abba")
PRINT "Rep: "; RepeatStr$("ab", 4)
' String concatenation in loops
DIM s$
s$ = ""
DIM i%
FOR i% = 1 TO 5
  s$ = s$ + STR$(i%)
NEXT i%
PRINT "Cat:"; s$
' Nested string function calls
PRINT UCASE$(LEFT$("hello world", 5))
PRINT LEN(MID$("abcdefgh", 3, 4))
