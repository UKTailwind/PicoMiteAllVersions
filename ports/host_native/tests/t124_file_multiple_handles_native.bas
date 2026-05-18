OPEN "B:/multi_a.tmp" FOR OUTPUT AS #1
OPEN "B:/multi_b.tmp" FOR OUTPUT AS #2
PRINT #1, "a1"
PRINT #2, "b1"
PRINT #1, "a2"
PRINT #2, "b2"
CLOSE #1
CLOSE #2
OPEN "B:/multi_a.tmp" FOR INPUT AS #1
OPEN "B:/multi_b.tmp" FOR INPUT AS #2
LINE INPUT #1, a1$
LINE INPUT #2, b1$
LINE INPUT #1, a2$
LINE INPUT #2, b2$
CLOSE #1
CLOSE #2
IF a1$ <> "a1" THEN ERROR "multi a1"
IF a2$ <> "a2" THEN ERROR "multi a2"
IF b1$ <> "b1" THEN ERROR "multi b1"
IF b2$ <> "b2" THEN ERROR "multi b2"
PRINT a1$ + a2$ + b1$ + b2$
