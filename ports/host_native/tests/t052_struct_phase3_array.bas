' Phase 3 TYPE/STRUCT — array-of-struct + BOUND() + 2D
' Covers acceptance tests 3, 12, 46, 47, 48 of struct_full.bas.

TYPE Point
  x AS INTEGER
  y AS INTEGER
END TYPE

' --- 1D struct array, constant index ---
DIM points(5) AS Point
points(0).x = 10 : points(0).y = 11
points(1).x = 20 : points(1).y = 21
points(5).x = 60 : points(5).y = 61

PRINT points(0).x, points(0).y
PRINT points(1).x, points(1).y
PRINT points(5).x, points(5).y

' --- Variable index ---
DIM dynArr(9) AS Point
DIM i AS INTEGER
FOR i = 0 TO 9
  dynArr(i).x = i * 2
  dynArr(i).y = i * 3
NEXT i

DIM ok AS INTEGER
ok = 1
FOR i = 0 TO 9
  IF dynArr(i).x <> i * 2 OR dynArr(i).y <> i * 3 THEN ok = 0
NEXT i
IF ok THEN PRINT "1D variable index: PASS" ELSE PRINT "1D variable index: FAIL"

' --- 2D struct array ---
DIM grid(3, 2) AS Point
DIM j AS INTEGER
FOR i = 0 TO 3
  FOR j = 0 TO 2
    grid(i, j).x = i * 10 + j
    grid(i, j).y = (i + 1) * (j + 1)
  NEXT j
NEXT i

PRINT grid(2, 1).x, grid(2, 1).y
PRINT grid(3, 0).x, grid(3, 0).y
PRINT grid(0, 2).x, grid(0, 2).y

' --- BOUND() on struct arrays ---
PRINT "BOUND(dynArr()):", Bound(dynArr())
PRINT "BOUND(grid(), 1):", Bound(grid(), 1)
PRINT "BOUND(grid(), 2):", Bound(grid(), 2)
PRINT "BOUND(grid()):", Bound(grid())
