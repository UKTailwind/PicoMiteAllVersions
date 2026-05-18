' Test type coercion across native functions
' INT/FIX/CINT edge cases
PRINT INT(2.5)
PRINT INT(3.5)
PRINT INT(-2.5)
PRINT INT(-3.5)
PRINT FIX(2.7)
PRINT FIX(-2.7)
PRINT CINT(2.5)
PRINT CINT(-2.5)
' ABS with different types
DIM i%
i% = -42
PRINT ABS(i%)
PRINT ABS(-3.14)
PRINT ABS(0)
' SGN edge cases
PRINT SGN(-100)
PRINT SGN(0)
PRINT SGN(100)
PRINT SGN(-0.001)
PRINT SGN(0.001)
' Mixed type math
DIM f!
f! = 2.5
i% = 3
PRINT f! + i%
PRINT f! * i%
PRINT i% / 2
PRINT i% \ 2
PRINT i% MOD 2
' Chained conversions
PRINT INT(ABS(-3.7))
PRINT CINT(ABS(-2.5))
PRINT STR$(INT(99.9))
PRINT VAL(STR$(42))
