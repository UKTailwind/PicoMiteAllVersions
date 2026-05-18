' Test various recursive patterns
FUNCTION GCD%(a%, b%)
  IF b% = 0 THEN
    GCD% = a%
  ELSE
    GCD% = GCD%(b%, a% MOD b%)
  ENDIF
END FUNCTION
FUNCTION Power%(base%, exp%)
  IF exp% = 0 THEN
    Power% = 1
  ELSE
    Power% = base% * Power%(base%, exp% - 1)
  ENDIF
END FUNCTION
FUNCTION SumDigits%(n%)
  IF n% < 10 THEN
    SumDigits% = n%
  ELSE
    SumDigits% = (n% MOD 10) + SumDigits%(n% \ 10)
  ENDIF
END FUNCTION
FUNCTION Ackermann%(m%, n%)
  IF m% = 0 THEN
    Ackermann% = n% + 1
  ELSEIF n% = 0 THEN
    Ackermann% = Ackermann%(m% - 1, 1)
  ELSE
    Ackermann% = Ackermann%(m% - 1, Ackermann%(m%, n% - 1))
  ENDIF
END FUNCTION
' GCD tests
PRINT GCD%(12, 8)
PRINT GCD%(100, 75)
PRINT GCD%(17, 13)
PRINT GCD%(0, 5)
PRINT GCD%(48, 36)
' Power tests
PRINT Power%(2, 0)
PRINT Power%(2, 10)
PRINT Power%(3, 5)
PRINT Power%(10, 4)
' Sum of digits
PRINT SumDigits%(123)
PRINT SumDigits%(9999)
PRINT SumDigits%(100000)
' Ackermann (small values only — grows fast!)
PRINT Ackermann%(0, 0)
PRINT Ackermann%(1, 1)
PRINT Ackermann%(2, 2)
PRINT Ackermann%(2, 3)
