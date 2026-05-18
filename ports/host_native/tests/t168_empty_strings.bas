' Empty string operations — functions, comparisons, concatenation
' LEN of empty
DIM a$
PRINT LEN("")
PRINT LEN(a$)
' LEFT$, MID$, RIGHT$ on empty
PRINT "[" + LEFT$("", 5) + "]"
PRINT "[" + MID$("", 1, 1) + "]"
PRINT "[" + RIGHT$("", 3) + "]"
' LEFT$, MID$, RIGHT$ with zero length
DIM s$
s$ = "hello"
PRINT "[" + LEFT$(s$, 0) + "]"
PRINT "[" + MID$(s$, 1, 0) + "]"
PRINT "[" + RIGHT$(s$, 0) + "]"
' Concatenation with empties
PRINT "[" + "" + "" + "x" + "" + "]"
a$ = ""
PRINT "[" + a$ + "y" + a$ + "]"
' INSTR with empty strings
PRINT INSTR("", "x")
PRINT INSTR("abc", "")
' Comparison
IF "" = "" THEN PRINT "empty=empty"
a$ = ""
DIM b$
b$ = ""
IF a$ = b$ THEN PRINT "vars equal"
IF a$ = "" THEN PRINT "var=literal"
IF "" <> "x" THEN PRINT "empty<>x"
' UCASE$/LCASE$ of empty
PRINT "[" + UCASE$("") + "]"
PRINT "[" + LCASE$("") + "]"
' STR$ and VAL edge cases
PRINT "[" + STR$(0) + "]"
' String multiplication of empty
a$ = ""
DIM i%
FOR i% = 1 TO 3
  a$ = a$ + ""
NEXT i%
PRINT LEN(a$)
