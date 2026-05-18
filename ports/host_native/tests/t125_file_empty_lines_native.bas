OPEN "B:/empty_lines.tmp" FOR OUTPUT AS #1
PRINT #1, "top"
PRINT #1, ""
PRINT #1, "bottom"
CLOSE #1
OPEN "B:/empty_lines.tmp" FOR INPUT AS #1
LINE INPUT #1, a$
LINE INPUT #1, b$
LINE INPUT #1, c$
CLOSE #1
IF a$ <> "top" THEN ERROR "empty top"
IF b$ <> "" THEN ERROR "empty middle"
IF c$ <> "bottom" THEN ERROR "empty bottom"
PRINT a$ + "/" + b$ + "/" + c$
