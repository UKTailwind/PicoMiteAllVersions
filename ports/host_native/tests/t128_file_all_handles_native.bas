OPEN "B:/h1.tmp" FOR OUTPUT AS #1
OPEN "B:/h2.tmp" FOR OUTPUT AS #2
OPEN "B:/h3.tmp" FOR OUTPUT AS #3
OPEN "B:/h4.tmp" FOR OUTPUT AS #4
OPEN "B:/h5.tmp" FOR OUTPUT AS #5
OPEN "B:/h6.tmp" FOR OUTPUT AS #6
OPEN "B:/h7.tmp" FOR OUTPUT AS #7
OPEN "B:/h8.tmp" FOR OUTPUT AS #8
OPEN "B:/h9.tmp" FOR OUTPUT AS #9
OPEN "B:/h10.tmp" FOR OUTPUT AS #10
PRINT #1, "one"
PRINT #5, "five"
PRINT #10, "ten"
CLOSE #1, #2, #3, #4, #5, #6, #7, #8, #9, #10
OPEN "B:/h1.tmp" FOR INPUT AS #1
OPEN "B:/h5.tmp" FOR INPUT AS #5
OPEN "B:/h10.tmp" FOR INPUT AS #10
LINE INPUT #1, a$
LINE INPUT #5, b$
LINE INPUT #10, c$
CLOSE #1, #5, #10
IF a$ <> "one" THEN ERROR "handle one"
IF b$ <> "five" THEN ERROR "handle five"
IF c$ <> "ten" THEN ERROR "handle ten"
PRINT a$ + "/" + b$ + "/" + c$
