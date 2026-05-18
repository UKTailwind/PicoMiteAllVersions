' Extended string function tests
DIM a$ = "Hello World"
PRINT LEN(a$)
PRINT LEFT$(a$, 5)
PRINT RIGHT$(a$, 5)
PRINT MID$(a$, 7, 5)
PRINT MID$(a$, 7)
PRINT UCASE$(a$)
PRINT LCASE$(a$)
' VAL and STR$
DIM v! = VAL("3.14")
PRINT v!
DIM s$ = STR$(42)
PRINT s$
' CHR$ and ASC
DIM c$ = CHR$(65)
PRINT c$
PRINT ASC("A")
PRINT ASC("Hello")
' INSTR
PRINT INSTR(a$, "World")
PRINT INSTR(a$, "xyz")
PRINT INSTR(a$, "lo")
' String comparison
DIM x$ = "abc", y$ = "def"
IF x$ < y$ THEN PRINT "lt" ELSE PRINT "ge"
IF x$ = "abc" THEN PRINT "eq" ELSE PRINT "ne"
IF x$ <> y$ THEN PRINT "ne" ELSE PRINT "eq"
