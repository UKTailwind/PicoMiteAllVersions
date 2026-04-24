' probe_vars.bas - checks bx!/by! assignment in FRUN.
' Expect non-zero values for bx/by.  If they're 0, variable store is
' misdispatching.
CONST W% = MM.HRES
CONST H% = MM.VRES
DIM FLOAT bx!, by!
bx! = W% \ 2
by! = H% \ 2
PRINT "W=" ; W%
PRINT "H=" ; H%
PRINT "bx=" ; bx!
PRINT "by=" ; by!
PAUSE 5000
