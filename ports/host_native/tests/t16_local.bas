' Test LOCAL variable scoping
DIM x% = 100
SUB test()
  LOCAL x%
  x% = 999
  PRINT x%
END SUB
PRINT x%
test
PRINT x%
' Function with locals
FUNCTION Sum%(n%)
  LOCAL i%, s%
  s% = 0
  FOR i% = 1 TO n%
    s% = s% + i%
  NEXT i%
  Sum% = s%
END FUNCTION
PRINT Sum%(10)
PRINT Sum%(100)
