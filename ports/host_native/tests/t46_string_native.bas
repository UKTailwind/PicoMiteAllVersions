' Test all native string functions thoroughly
' LEN edge cases
PRINT LEN("")
PRINT LEN("x")
PRINT LEN("hello world")
' LEFT$/RIGHT$ edge cases
PRINT "|"; LEFT$("hello", 0); "|"
PRINT LEFT$("hello", 3)
PRINT LEFT$("hello", 10)
PRINT "|"; RIGHT$("hello", 0); "|"
PRINT RIGHT$("hello", 3)
PRINT RIGHT$("hello", 10)
' MID$ 2-arg and 3-arg
PRINT MID$("abcdef", 3)
PRINT MID$("abcdef", 3, 2)
PRINT MID$("abcdef", 1, 1)
PRINT MID$("abcdef", 6, 1)
' UCASE$/LCASE$
PRINT UCASE$("Hello World 123!")
PRINT LCASE$("Hello World 123!")
PRINT UCASE$("")
PRINT LCASE$("")
' INSTR
PRINT INSTR("hello world", "world")
PRINT INSTR("hello world", "xyz")
PRINT INSTR("aababab", "ab")
PRINT INSTR(3, "aababab", "ab")
' CHR$/ASC
PRINT CHR$(65); CHR$(66); CHR$(67)
PRINT ASC("A")
PRINT ASC("Z")
PRINT ASC("0")
' VAL/STR$
PRINT VAL("123")
PRINT VAL("-45.6")
PRINT VAL("0")
PRINT STR$(42)
PRINT STR$(-7.5)
' HEX$/OCT$/BIN$
PRINT HEX$(255)
PRINT HEX$(0)
PRINT OCT$(8)
PRINT OCT$(255)
PRINT BIN$(10)
PRINT BIN$(255)
' String concatenation in expressions
DIM a$, b$
a$ = "hello"
b$ = " world"
PRINT a$ + b$
PRINT LEN(a$ + b$)
