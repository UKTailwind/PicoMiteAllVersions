' RUN_ARGS: --interp
' String-array REDIM — interp-only because bc_source.c doesn't yet compile
' the `DIM s$(n) LENGTH n` clause.  Once VM DIM handles LENGTH, fold into
' t049_redim.bas.

DIM s$(2) LENGTH 8
s$(0) = "hello" : s$(1) = "world" : s$(2) = "!"
REDIM PRESERVE s$(4)
PRINT s$(0) + "-" + s$(1)
