' Array dimension boundary tests
' Smallest array (dimension 1 means 2 elements with OPTION BASE 0)
DIM a%(1)
a%(0) = 99
a%(1) = 77
PRINT a%(0); a%(1)
' Moderate single dimension
DIM b%(50)
b%(0) = 1
b%(25) = 2
b%(50) = 3
PRINT b%(0); b%(25); b%(50)
' Multi-dimensional array (2D)
DIM c%(5, 5)
c%(0, 0) = 10
c%(5, 5) = 20
c%(2, 3) = 30
PRINT c%(0, 0); c%(5, 5); c%(2, 3)
' 3D array
DIM d%(3, 3, 3)
d%(0, 0, 0) = 100
d%(3, 3, 3) = 200
d%(1, 2, 3) = 300
PRINT d%(0, 0, 0); d%(3, 3, 3); d%(1, 2, 3)
' 4D array
DIM e%(2, 2, 2, 2)
e%(0, 0, 0, 0) = 1000
e%(2, 2, 2, 2) = 2000
e%(1, 1, 1, 1) = 3000
PRINT e%(0, 0, 0, 0); e%(2, 2, 2, 2); e%(1, 1, 1, 1)
' String array
DIM f$(10)
f$(0) = "first"
f$(5) = "middle"
f$(10) = "last"
PRINT f$(0); " "; f$(5); " "; f$(10)
' Float array
DIM g!(5)
g!(0) = 1.5
g!(5) = 9.9
PRINT g!(0); g!(5)
' Verify array is zeroed on DIM
DIM h%(5)
PRINT h%(0); h%(3); h%(5)
DIM k$(3)
PRINT LEN(k$(0)); LEN(k$(1)); LEN(k$(2))
