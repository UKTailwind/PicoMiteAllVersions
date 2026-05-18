' Test native string functions (SPACE$, STRING$, FIELD$)
' SPACE$ with various lengths
PRINT "|"; SPACE$(0); "|"
PRINT "|"; SPACE$(1); "|"
PRINT "|"; SPACE$(5); "|"
' SPACE$ with variable
DIM n%
n% = 3
PRINT "|"; SPACE$(n%); "|"
' STRING$ with character code
PRINT STRING$(5, 42)
PRINT STRING$(3, 65)
PRINT STRING$(4, 48)
' STRING$ with string argument
PRINT STRING$(3, "*")
PRINT STRING$(5, "AB")
' FIELD$ - CSV parsing
DIM csv$
csv$ = "alpha,beta,gamma,delta"
PRINT FIELD$(csv$, 1, ",")
PRINT FIELD$(csv$, 2, ",")
PRINT FIELD$(csv$, 3, ",")
PRINT FIELD$(csv$, 4, ",")
' FIELD$ with different delimiter
DIM path$
path$ = "usr/local/bin/test"
PRINT FIELD$(path$, 1, "/")
PRINT FIELD$(path$, 4, "/")
' FIELD$ with variable arguments
DIM idx%
idx% = 2
PRINT FIELD$(csv$, idx%, ",")
idx% = 3
PRINT FIELD$(csv$, idx%, ",")
