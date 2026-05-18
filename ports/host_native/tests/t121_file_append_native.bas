OPEN "B:/append.tmp" FOR OUTPUT AS #1
PRINT #1, "first"
CLOSE #1
OPEN "B:/append.tmp" FOR APPEND AS #1
PRINT #1, "second"
CLOSE #1
OPEN "B:/append.tmp" FOR INPUT AS #1
LINE INPUT #1, a$
LINE INPUT #1, b$
CLOSE #1
IF a$ <> "first" THEN ERROR "append first line"
IF b$ <> "second" THEN ERROR "append second line"
PRINT a$ + "," + b$
