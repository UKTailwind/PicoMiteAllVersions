' Test all variable types: integer (%), float (!), string ($)
DIM i% = 42
DIM f! = 3.14
DIM s$ = "hello"
PRINT i%
PRINT f!
PRINT s$
' Integer operations
DIM a% = 100, b% = 7
PRINT a% + b%
PRINT a% - b%
PRINT a% * b%
PRINT a% \ b%
PRINT a% MOD b%
' Float operations
DIM x! = 10.5, y! = 3.2
PRINT x! + y!
PRINT x! - y!
PRINT x! * y!
PRINT x! / y!
' Mixed int/float
DIM m! = i% + f!
PRINT m!
DIM n! = i% * f!
PRINT n!
