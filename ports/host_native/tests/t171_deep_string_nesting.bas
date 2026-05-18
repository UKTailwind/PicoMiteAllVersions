' Deep string nesting — stress test the 4-slot str_temp ring buffer
' 2-deep nesting (well within 4 temps)
DIM a$
a$ = "Hello World"
PRINT LEFT$(UCASE$(a$), 5)
PRINT RIGHT$(LCASE$(a$), 5)
' 3-deep nesting
PRINT MID$(UCASE$(LEFT$(a$, 8)), 1, 5)
' Concatenation with multiple temps
DIM b$, c$, d$
b$ = "AAA"
c$ = "BBB"
d$ = "CCC"
PRINT LEFT$(b$, 2) + MID$(c$, 1, 2) + RIGHT$(d$, 2)
' More complex: each function call uses a temp
PRINT LEFT$(b$, 1) + LEFT$(c$, 1) + LEFT$(d$, 1) + LEFT$(a$, 1)
' Nested with concatenation
PRINT UCASE$(LEFT$(a$, 3)) + " " + LCASE$(RIGHT$(a$, 3))
' Deep nesting in PRINT expression
DIM e$
e$ = "ABCDEFGHIJ"
PRINT LEFT$(e$, 3) + MID$(e$, 4, 3) + RIGHT$(e$, 4)
' Chained operations
PRINT LEN(LEFT$(UCASE$(a$), 5))
PRINT LEN(MID$(a$, 3, 4))
' Multiple string function results in one expression
DIM x$
x$ = "TestString"
PRINT LEFT$(x$, 2) + MID$(x$, 3, 2) + RIGHT$(x$, 2) + UCASE$(LEFT$(x$, 1))
