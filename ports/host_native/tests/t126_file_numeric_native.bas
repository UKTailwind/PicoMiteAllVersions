OPEN "B:/numeric.tmp" FOR OUTPUT AS #1
PRINT #1, 42
PRINT #1, -7
PRINT #1, 1.5
CLOSE #1
OPEN "B:/numeric.tmp" FOR INPUT AS #1
LINE INPUT #1, a$
LINE INPUT #1, b$
LINE INPUT #1, c$
CLOSE #1
IF a$ <> " 42" THEN ERROR "positive integer file print"
IF b$ <> "-7" THEN ERROR "negative integer file print"
IF c$ <> " 1.5" THEN ERROR "float file print"
PRINT a$
PRINT b$
PRINT c$
