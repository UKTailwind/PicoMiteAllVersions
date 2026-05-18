' Boundary integer tests — overflow, negation, division edge cases
' Negation of zero and small values
DIM a%
a% = 0
PRINT -a%
a% = 1
PRINT -a%
a% = -1
PRINT -a%
' Large integer values near 64-bit limits
a% = 9223372036854775807
PRINT a%
' Integer division edge cases
a% = 100
PRINT a% \ 1
PRINT a% \ -1
a% = -100
PRINT a% \ 1
PRINT a% \ -1
' MOD edge cases
a% = 100
PRINT a% MOD 1
PRINT a% MOD -1
a% = -100
PRINT a% MOD 1
PRINT a% MOD -1
' Multiplication near overflow
a% = 3037000499
DIM b%
b% = 3
PRINT a% * b%
' Negation of large values
a% = 9223372036854775807
PRINT -a%
