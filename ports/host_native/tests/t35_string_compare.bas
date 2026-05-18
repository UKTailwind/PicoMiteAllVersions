' Test string comparisons and operations in depth
DIM a$, b$
a$ = "Apple"
b$ = "Banana"
' Comparison operators
PRINT a$ < b$
PRINT a$ > b$
PRINT a$ = a$
PRINT a$ <> b$
PRINT a$ <= a$
PRINT a$ >= b$
' Case sensitivity
PRINT "abc" = "ABC"
PRINT "abc" <> "ABC"
' Empty string comparisons
PRINT "" = ""
PRINT "" < "A"
PRINT "A" > ""
' String in IF
IF a$ < b$ THEN PRINT "Apple before Banana"
IF a$ = "Apple" THEN PRINT "Found Apple"
' String in SELECT CASE
DIM fruit$
fruit$ = "Banana"
SELECT CASE fruit$
  CASE "Apple"
    PRINT "Got apple"
  CASE "Banana"
    PRINT "Got banana"
  CASE "Cherry"
    PRINT "Got cherry"
  CASE ELSE
    PRINT "Unknown fruit"
END SELECT
' String building
DIM result$
result$ = ""
DIM i%
FOR i% = 65 TO 74
  result$ = result$ + CHR$(i%)
NEXT i%
PRINT result$
PRINT LEN(result$)
