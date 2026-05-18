' RUN_ARGS: --interp
' LOC, LOF, EOF as direct queries. Interp-only because INPUT$ isn't
' natively compiled by bc_source.c and the bridge for file-reading
' functions doesn't sync handle state between VM and interpreter.
' VM-side file I/O is covered by t119-t152.
OPEN "B:/locq.tmp" FOR OUTPUT AS #1
PRINT #1, "abcdef"
CLOSE #1

OPEN "B:/locq.tmp" FOR INPUT AS #1
IF LOF(#1) <> 8 THEN ERROR "LOF after write: expected 8 (6+CRLF), got " + Str$(LOF(#1))
IF LOC(#1) <> 1 THEN ERROR "LOC at start: expected 1, got " + Str$(LOC(#1))
IF EOF(#1) <> 0 THEN ERROR "EOF at start"
Dim chk$ = Input$(3, #1)
IF chk$ <> "abc" THEN ERROR "INPUT$ read mismatch: " + chk$
IF LOC(#1) <> 4 THEN ERROR "LOC after 3 bytes: expected 4, got " + Str$(LOC(#1))
IF EOF(#1) <> 0 THEN ERROR "EOF mid-file"
Dim rest$ = Input$(8, #1)
IF EOF(#1) = 0 THEN ERROR "EOF after reading past end"
CLOSE #1

KILL "B:/locq.tmp"
PRINT "loc/lof/eof ok"
