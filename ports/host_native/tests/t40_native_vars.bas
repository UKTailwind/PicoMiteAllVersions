' Test native functions reading VM variables
DIM n%
n% = 3
PRINT "|"; SPACE$(n%); "|"
n% = 0
PRINT "|"; SPACE$(n%); "|"
n% = 7
PRINT "|"; SPACE$(n%); "|"
' STRING$() with VM variables
DIM c%
c% = 42
PRINT STRING$(5, c%)
c% = 35
n% = 3
PRINT STRING$(n%, c%)
' Test with expressions involving VM variables
DIM x%
x% = 2
PRINT "|"; SPACE$(x% + 1); "|"
PRINT "|"; SPACE$(x% * 3); "|"
