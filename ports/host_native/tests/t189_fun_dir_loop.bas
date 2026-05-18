' RUN_ARGS: --interp
' DIR$() function — iterated alternative to the FILES command. Interp-
' only because DIR$ isn't natively compiled by bc_source.c; it would
' need the function bridge, and the bridge doesn't sync file-side
' state between VM and interpreter. Functional coverage of the FatFS
' layer is in t120 / t152.
OPEN "B:/dira.di" FOR OUTPUT AS #1 : CLOSE #1
OPEN "B:/dirb.di" FOR OUTPUT AS #1 : CLOSE #1
OPEN "B:/dirc.di" FOR OUTPUT AS #1 : CLOSE #1
OPEN "B:/other.xx" FOR OUTPUT AS #1 : CLOSE #1

Dim count% = 0
Dim name$ = DIR$("B:/*.di", FILE)
Do While Len(name$) > 0
  count% = count% + 1
  name$ = DIR$()
Loop
IF count% <> 3 THEN ERROR "DIR$ expected 3 matches, got " + Str$(count%)

KILL "B:/dira.di"
KILL "B:/dirb.di"
KILL "B:/dirc.di"
KILL "B:/other.xx"
PRINT "dir$ ok"
