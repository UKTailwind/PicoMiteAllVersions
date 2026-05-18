' Test SUB and FUNCTION
SUB AddPrint(a%, b%)
  LOCAL c%
  c% = a% + b%
  PRINT c%
END SUB
FUNCTION Square%(n%)
  Square% = n% * n%
END FUNCTION
FUNCTION Factorial%(n%)
  IF n% <= 1 THEN
    Factorial% = 1
  ELSE
    Factorial% = n% * Factorial%(n% - 1)
  ENDIF
END FUNCTION
AddPrint 3, 4
AddPrint 10, 20
PRINT Square%(5)
PRINT Square%(12)
DIM x% = Square%(7) + Square%(3)
PRINT x%
PRINT Factorial%(1)
PRINT Factorial%(5)
PRINT Factorial%(10)
' Nested function calls
FUNCTION Double%(n%)
  Double% = n% * 2
END FUNCTION
FUNCTION AddOne%(n%)
  AddOne% = n% + 1
END FUNCTION
PRINT Double%(AddOne%(5))
PRINT AddOne%(Double%(5))
PRINT Double%(Double%(Double%(1)))
