' Shift edge cases — extremes, sign extension
DIM a%
' Shift by zero (identity)
a% = 42
PRINT a% << 0
PRINT a% >> 0
' Shift by 1
a% = 1
PRINT a% << 1
PRINT a% >> 1
' Shift to max bit
a% = 1
PRINT a% << 62
' Large shift of negative (sign extension)
a% = -1
PRINT a% >> 1
PRINT a% >> 31
PRINT a% >> 62
' Shift of zero
a% = 0
PRINT a% << 10
PRINT a% >> 10
' Shift positive values right
a% = 256
PRINT a% >> 1
PRINT a% >> 4
PRINT a% >> 8
' Left shift then right shift round-trip
a% = 7
PRINT (a% << 10) >> 10
