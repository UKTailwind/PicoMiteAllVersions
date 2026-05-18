FUNCTION Fib%(n%)
  IF n% <= 1 THEN
    Fib% = n%
  ELSE
    Fib% = Fib%(n% - 1) + Fib%(n% - 2)
  ENDIF
END FUNCTION
DIM t!, i%
PRINT "Fibonacci sequence:"
FOR i% = 0 TO 15
  PRINT "  Fib("; i%; ") = "; Fib%(i%)
NEXT i%
PRINT
PRINT "Timing Fib(25)..."
t! = TIMER
PRINT "Fib(25) = "; Fib%(25)
t! = TIMER - t!
PRINT "Time: "; t!; " sec"
