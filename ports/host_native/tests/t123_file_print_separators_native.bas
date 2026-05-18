OPEN "B:/separators.tmp" FOR OUTPUT AS #1
PRINT #1, "ab"; "cd"
PRINT #1, "left", "right"
PRINT #1, "tail";
CLOSE #1
OPEN "B:/separators.tmp" FOR INPUT AS #1
LINE INPUT #1, a$
LINE INPUT #1, b$
LINE INPUT #1, c$
CLOSE #1
IF a$ <> "abcd" THEN ERROR "semicolon separator"
IF b$ <> "left    right" THEN ERROR "comma separator"
IF c$ <> "tail" THEN ERROR "semicolon newline suppression"
PRINT a$
PRINT b$
PRINT c$
