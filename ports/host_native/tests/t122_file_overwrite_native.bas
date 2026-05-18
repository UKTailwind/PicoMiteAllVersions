OPEN "B:/overwrite.tmp" FOR OUTPUT AS #1
PRINT #1, "old"
CLOSE #1
OPEN "B:/overwrite.tmp" FOR OUTPUT AS #1
PRINT #1, "new"
CLOSE #1
OPEN "B:/overwrite.tmp" FOR INPUT AS #1
LINE INPUT #1, a$
LINE INPUT #1, b$
CLOSE #1
IF a$ <> "new" THEN ERROR "overwrite did not replace first line"
IF b$ <> "" THEN ERROR "overwrite did not truncate"
PRINT a$
