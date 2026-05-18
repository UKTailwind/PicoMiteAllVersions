' Comprehensive string function test
' INSTR - find substring
PRINT INSTR("Hello World", "World")
PRINT INSTR("Hello World", "world")
PRINT INSTR("Hello World", "l")
PRINT INSTR("Hello", "Hello World")
PRINT INSTR("", "x")
' LEFT$, RIGHT$, MID$
DIM s$
s$ = "ABCDEFGHIJ"
PRINT LEFT$(s$, 3)
PRINT RIGHT$(s$, 3)
PRINT MID$(s$, 4, 3)
PRINT MID$(s$, 8)
' String comparison
PRINT "ABC" = "ABC"
PRINT "ABC" <> "DEF"
PRINT "ABC" < "DEF"
PRINT "DEF" > "ABC"
PRINT "abc" = "ABC"
' String concatenation with different types
DIM i%
i% = 42
PRINT "Value=" + STR$(i%)
PRINT "PI=" + STR$(3.14159)
' CHR$ and ASC edge cases
PRINT ASC("A")
PRINT ASC("a")
PRINT ASC("0")
PRINT CHR$(48); CHR$(49); CHR$(50)
' HEX$, OCT$, BIN$
PRINT HEX$(0)
PRINT HEX$(16)
PRINT HEX$(65535)
PRINT OCT$(8)
PRINT OCT$(64)
PRINT BIN$(0)
PRINT BIN$(1)
PRINT BIN$(255)
' UCASE$/LCASE$ edge cases
PRINT UCASE$("")
PRINT LCASE$("")
PRINT UCASE$("123!@#")
PRINT LCASE$("123!@#")
PRINT UCASE$("mixed Case 123")
PRINT LCASE$("MIXED CASE 123")
