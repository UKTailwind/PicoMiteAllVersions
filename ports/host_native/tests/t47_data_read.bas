' Test DATA/READ/RESTORE
' All DATA statements - read sequentially across them
DATA 1, 2, 3
DATA 3.14, 2.718
DATA "hello", "world"
DATA 42, "test", 99
DATA -5, -10
DATA 100, 200, 300
' Read integers
DIM x%
READ x%
PRINT x%
READ x%
PRINT x%
READ x%
PRINT x%
' Read floats
DIM f!
READ f!
PRINT f!
READ f!
PRINT f!
' Read strings
DIM s$
READ s$
PRINT s$
READ s$
PRINT s$
' Read mixed types
DIM a%, b$, c%
READ a%
READ b$
READ c%
PRINT a%
PRINT b$
PRINT c%
' Read negative numbers
READ x%
PRINT x%
READ x%
PRINT x%
' Read multiple per READ statement
DIM p%, q%, r%
READ p%, q%, r%
PRINT p%
PRINT q%
PRINT r%
' Test RESTORE - reset to beginning
RESTORE
READ x%
PRINT x%
