' Test INC and CONST statements
' INC with no amount (default +1)
DIM x%
x% = 10
INC x%
PRINT x%
' INC with amount
INC x%, 5
PRINT x%
' INC float
DIM f!
f! = 1.5
INC f!
PRINT f!
INC f!, 0.5
PRINT f!
' INC in a loop
DIM i%
i% = 0
FOR j% = 1 TO 5
  INC i%, j%
NEXT
PRINT i%
' CONST
CONST PI2! = 6.283185
PRINT INT(PI2! * 1000) / 1000
CONST GREETING$ = "hi"
PRINT GREETING$
CONST MAX_VAL% = 100
PRINT MAX_VAL%
' Multiple CONST
CONST A% = 1, B% = 2, C% = 3
PRINT A% + B% + C%
