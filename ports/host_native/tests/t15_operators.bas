' Test operator precedence and edge cases
DIM r%
r% = 2 + 3 * 4
PRINT r%
r% = (2 + 3) * 4
PRINT r%
r% = 2 ^ 3 + 1
PRINT r%
r% = 10 - 2 - 3
PRINT r%
r% = 2 * 3 + 4 * 5
PRINT r%
r% = 100 \ 3 MOD 2
PRINT r%
' Logical operators
DIM a% = 1, b% = 0
IF a% AND NOT b% THEN PRINT "pass1" ELSE PRINT "fail1"
IF a% OR b% THEN PRINT "pass2" ELSE PRINT "fail2"
IF NOT (a% AND b%) THEN PRINT "pass3" ELSE PRINT "fail3"
' 64-bit integers
DIM big% = 2147483647
DIM one% = 1
DIM c% = big% + one%
PRINT c%
big% = &H7FFFFFFFFFFFFFFF
PRINT big%
