' Test operator precedence and complex expressions
' Precedence: unary > ^ > * / \ MOD > + - > comparisons > NOT > AND > OR > XOR
PRINT 2 + 3 * 4
PRINT (2 + 3) * 4
PRINT 2 ^ 3 ^ 2
PRINT 10 - 3 - 2
PRINT 100 \ 10 \ 3
PRINT 2 + 3 * 4 - 1
PRINT 2 * 3 + 4 * 5
PRINT 10 MOD 3 + 1
PRINT 2 ^ 3 + 1
PRINT -3 * -4
PRINT -(3 + 4) * 2
' Boolean expressions
DIM a%, b%, c%
a% = 5
b% = 10
c% = 15
PRINT (a% < b%) AND (b% < c%)
PRINT (a% > b%) OR (b% < c%)
PRINT NOT (a% = b%)
PRINT (a% < b%) AND NOT (b% > c%)
' Chained comparisons (evaluated left to right with boolean intermediates)
PRINT a% < b%
PRINT b% > a%
PRINT a% <= 5
PRINT b% >= 10
PRINT a% <> b%
PRINT a% = a%
' Complex nested expressions
DIM r%
r% = (a% + b%) * (c% - a%) \ 2
PRINT r%
r% = a% * b% + b% * c% + a% * c%
PRINT r%
' Bitwise in expressions
r% = 255 AND 15
PRINT r%
r% = 240 OR 15
PRINT r%
r% = 255 XOR 128
PRINT r%
' Mixed arithmetic with function calls
PRINT ABS(-5) + SQR(16)
PRINT INT(3.14) * 2 + 1
DIM f!
f! = SIN(0) + COS(0)
PRINT f!
