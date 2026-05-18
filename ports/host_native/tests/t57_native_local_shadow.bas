' VM native call regression:
' A native command should see the current VM local bx%,
' not collide with the global bx! in the interpreter vartable.

DIM FLOAT bx!

SUB DrawIt()
  LOCAL bx%
  bx% = 3
  LINE bx%, 0, bx%, 0
  PRINT "ok"
END SUB

DrawIt
