OPEN "B:/seek.tmp" FOR OUTPUT AS #1
PRINT #1, "abcdef"
CLOSE #1
OPEN "B:/seek.tmp" FOR INPUT AS #1
SEEK #1, 4
LINE INPUT #1, a$
CLOSE #1
KILL "B:/seek.tmp"
PRINT a$
