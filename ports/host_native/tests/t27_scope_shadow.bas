' Test variable scoping and shadowing
DIM x% = 100
DIM y% = 200
PRINT "Global x="; x%; " y="; y%
SUB ModifyLocal()
  LOCAL x%
  x% = 999
  PRINT "In ModifyLocal: x="; x%
END SUB
SUB ModifyGlobal()
  x% = 555
  PRINT "In ModifyGlobal: x="; x%
END SUB
FUNCTION ReadGlobal%()
  ReadGlobal% = x%
END FUNCTION
FUNCTION ShadowTest%(n%)
  LOCAL x%
  x% = n% * 10
  PRINT "Shadow x="; x%; " global via func="; ReadGlobal%()
  ShadowTest% = x%
END FUNCTION
ModifyLocal
PRINT "After ModifyLocal: x="; x%
ModifyGlobal
PRINT "After ModifyGlobal: x="; x%
x% = 100
DIM r%
r% = ShadowTest%(7)
PRINT "ShadowTest returned:"; r%
PRINT "Global x still:"; x%
' Nested function calls with locals
FUNCTION Inner%(n%)
  LOCAL x%
  x% = n% + 1
  Inner% = x%
END FUNCTION
FUNCTION Outer%(n%)
  LOCAL x%
  x% = Inner%(n%) * 2
  Outer% = x%
END FUNCTION
PRINT "Outer(5)="; Outer%(5)
PRINT "Final global x="; x%
