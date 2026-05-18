' Test nested function calls and mutual function interaction
FUNCTION Max3%(a%, b%, c%)
  LOCAL t%
  IF a% >= b% AND a% >= c% THEN
    t% = a%
  ELSEIF b% >= a% AND b% >= c% THEN
    t% = b%
  ELSE
    t% = c%
  ENDIF
  Max3% = t%
END FUNCTION
FUNCTION Min3%(a%, b%, c%)
  LOCAL t%
  IF a% <= b% AND a% <= c% THEN
    t% = a%
  ELSEIF b% <= a% AND b% <= c% THEN
    t% = b%
  ELSE
    t% = c%
  ENDIF
  Min3% = t%
END FUNCTION
FUNCTION Clamp%(val%, lo%, hi%)
  Clamp% = Min3%(Max3%(val%, lo%, 0), hi%, hi%)
END FUNCTION
PRINT Max3%(10, 20, 30)
PRINT Max3%(30, 10, 20)
PRINT Max3%(5, 5, 5)
PRINT Min3%(10, 20, 30)
PRINT Min3%(30, 10, 20)
PRINT Clamp%(50, 0, 100)
PRINT Clamp%(150, 0, 100)
PRINT Clamp%(-10, 0, 100)
' Nested calls in expressions
DIM r%
r% = Max3%(1, 2, 3) + Min3%(4, 5, 6) * 2
PRINT r%
r% = Max3%(Min3%(10, 20, 30), Min3%(40, 50, 60), Min3%(70, 80, 90))
PRINT r%
