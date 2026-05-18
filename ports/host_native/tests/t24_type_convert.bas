' Test type conversions between int, float, and string
DIM i%, f!, s$
' Int to float
i% = 42
f! = i%
PRINT f!
' Float to int (truncation)
f! = 3.7
i% = f!
PRINT i%
f! = -3.7
i% = f!
PRINT i%
' Int/Float arithmetic mixed
i% = 10
f! = 3.5
PRINT i% + f!
PRINT i% * f!
PRINT i% - f!
' Power always returns float
DIM r!
r! = 2 ^ 10
PRINT r!
' Division always returns float
r! = 10 / 3
PRINT r!
' Integer division
i% = 10 \ 3
PRINT i%
i% = -10 \ 3
PRINT i%
' MOD
PRINT 17 MOD 5
PRINT -17 MOD 5
' STR$ and VAL conversions
s$ = STR$(12345)
PRINT "STR:"; s$
i% = VAL("6789")
PRINT "VAL:"; i%
f! = VAL("3.14159")
PRINT f!
' CHR$ and ASC
PRINT CHR$(65); CHR$(66); CHR$(67)
PRINT ASC("A")
PRINT ASC("Z")
' HEX$, OCT$, BIN$
PRINT HEX$(255)
PRINT HEX$(4096)
PRINT OCT$(255)
PRINT BIN$(170)
' Chained conversions
i% = VAL(STR$(42)) + VAL(STR$(58))
PRINT i%
' ABS preserves type
PRINT ABS(-5)
PRINT ABS(-3.14)
' SGN always returns int
PRINT SGN(42)
PRINT SGN(-42)
PRINT SGN(0)
' INT, FIX, CINT
PRINT INT(3.7)
PRINT INT(-3.7)
PRINT FIX(3.7)
PRINT FIX(-3.7)
PRINT CINT(3.5)
PRINT CINT(4.5)
