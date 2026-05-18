OPTION EXPLICIT

DIM INTEGER a%(4, 7)

SUB ShowCell(r%, c%)
  LOCAL value%
  value% = a%(r%, c%)
  PRINT value%
END SUB

SUB Walk()
  LOCAL r%, c%
  FOR r% = 0 TO 4
    FOR c% = 0 TO 7
      ShowCell r%, c%
    NEXT
  NEXT
END SUB

a%(4, 7) = 123
Walk
