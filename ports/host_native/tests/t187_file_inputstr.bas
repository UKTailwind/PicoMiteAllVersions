' RUN_ARGS: --interp
' INPUT$(n, #fnbr) — fixed-byte-count reader. INPUT$ isn't natively
' compiled by bc_source.c (would bridge as a function call), so this
' test pins the interpreter path.
OPEN "B:/inpstr.tmp" FOR OUTPUT AS #1
PRINT #1, "1234567890ABCDEF"
CLOSE #1

OPEN "B:/inpstr.tmp" FOR INPUT AS #1
Dim a$ = Input$(4, #1)
IF a$ <> "1234" THEN ERROR "INPUT$ first 4: " + a$
Dim b$ = Input$(6, #1)
IF b$ <> "567890" THEN ERROR "INPUT$ next 6: " + b$
' Request 99 bytes; reader short-returns with whatever is left.
Dim c$ = Input$(99, #1)
IF Left$(c$, 6) <> "ABCDEF" THEN ERROR "INPUT$ tail: " + c$
CLOSE #1

KILL "B:/inpstr.tmp"
PRINT "input$ ok"
