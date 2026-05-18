' Basic REDIM: grow an integer array, existing contents discarded
DIM a%(3)
a%(0) = 10 : a%(1) = 20 : a%(2) = 30 : a%(3) = 40
REDIM a%(5)
a%(5) = 99
PRINT a%(5)

' REDIM PRESERVE: only last dim may change, existing data survives
DIM b%(5)
b%(0) = 1 : b%(1) = 2 : b%(2) = 3 : b%(3) = 4 : b%(4) = 5 : b%(5) = 6
REDIM PRESERVE b%(8)
PRINT b%(0), b%(3), b%(5)
b%(7) = 77
PRINT b%(7)

' Shrink with PRESERVE: keep what fits
DIM c%(10)
FOR i% = 0 TO 10 : c%(i%) = i% * 10 : NEXT
REDIM PRESERVE c%(4)
PRINT c%(0), c%(4)

' Float array
DIM f!(2)
f!(0) = 1.5 : f!(1) = 2.5 : f!(2) = 3.5
REDIM PRESERVE f!(4)
PRINT f!(1)
