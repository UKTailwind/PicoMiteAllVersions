' PRINT @(x, y) "text" must compile and run under VM. Regression for:
' bc_source.c's VM-native PRINT handler rejected the `@(x,y)` cursor-
' positioning form with "Unsupported expression". Fix: PRINT now
' always falls through to OP_BRIDGE_CMD so the interpreter's fun_at +
' cmd_print handles every form (matrix.bas's effect needs this).
'
' Exercises both forms: plain PRINT (bridges) and PRINT @(x,y) (also
' bridges, but via the same path so the cursor move works).
Print "line1"
Print @(0, 0) "at00"
Print @(16, 0) "atXY"
Print "done"
