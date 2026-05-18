' Test constant expressions and complex evaluation
' Hex, octal, binary literals
DIM h%
h% = &HFF
PRINT h%
h% = &H1000
PRINT h%
h% = &O77
PRINT h%
h% = &B11001100
PRINT h%
' Complex nested expressions
DIM a%, b%, c%
a% = 5
b% = 10
c% = 15
PRINT (a% + b%) * c%
PRINT a% * (b% + c%)
PRINT (a% + b%) * (c% - a%)
PRINT ((a% + b%) * c%) \ 7
' Unary operators
PRINT -(-5)
PRINT -(a% + b%)
PRINT NOT (a% > b%)
PRINT NOT 0
PRINT NOT 1
' Comparison chains in conditions
IF a% < b% AND b% < c% THEN PRINT "ascending"
IF NOT (a% > c%) THEN PRINT "a not > c"
IF (a% + b%) = c% THEN PRINT "a+b=c"
IF (a% * b%) > (b% * c%) OR (a% = 5) THEN PRINT "complex or"
' Integer overflow behavior
DIM big%
big% = 1000000000
PRINT big% * 2
PRINT big% + big%
