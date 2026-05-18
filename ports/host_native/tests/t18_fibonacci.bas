' Fibonacci recursive benchmark
FUNCTION Fib%(n%)
  IF n% <= 1 THEN
    Fib% = n%
  ELSE
    Fib% = Fib%(n% - 1) + Fib%(n% - 2)
  ENDIF
END FUNCTION
PRINT Fib%(1)
PRINT Fib%(5)
PRINT Fib%(10)
PRINT Fib%(20)
