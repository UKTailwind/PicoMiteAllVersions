SUB Hello(name$)
  LOCAL msg$
  msg$ = "hello " + name$
  PRINT msg$
END SUB

FUNCTION Twice%(x%)
  Twice% = x% * 2
END FUNCTION

Hello "vm"
PRINT Twice%(4)
