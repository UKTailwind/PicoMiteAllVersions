' RUN_ARGS: --vm
' Test that (a * b) \ Const_power_of_2 gets fused via constant inlining
OPTION EXPLICIT

Const SCALE = 1073741824
Const HALF_SCALE = 536870912

Dim a%, b%, r%
a% = 1073741824
b% = 536870912

' Should fuse to SQRSHR (same var * same var \ const power_of_2)
r% = (a% * a%) \ SCALE
IF r% <> 1073741824 THEN ERROR "sqrshr failed"

' Should fuse to MULSHR (different vars)
r% = (a% * b%) \ SCALE
IF r% <> 536870912 THEN ERROR "mulshr failed"

' Should fuse to MULSHRADD (mulshr + add)
r% = (a% * b%) \ SCALE + 100
IF r% <> 536870912 + 100 THEN ERROR "mulshradd failed"

' Shift 29: divide by HALF_SCALE
r% = (a% * b%) \ HALF_SCALE
IF r% <> 1073741824 THEN ERROR "mulshr 29 failed"

PRINT "ok"
