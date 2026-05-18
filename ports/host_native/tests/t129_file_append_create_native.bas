OPEN "B:/append_create.tmp" FOR APPEND AS #1
PRINT #1, "created"
CLOSE #1
OPEN "B:/append_create.tmp" FOR INPUT AS #1
LINE INPUT #1, a$
CLOSE #1
IF a$ <> "created" THEN ERROR "append create"
PRINT a$
