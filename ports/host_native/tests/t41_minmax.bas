' Test MIN/MAX functions (native compiled)
PRINT MIN(3, 7)
PRINT MAX(3, 7)
PRINT MIN(-5, 5)
PRINT MAX(-5, 5)
PRINT MIN(3.5, 3.4)
PRINT MAX(3.5, 3.4)
PRINT MIN(0, 0)
PRINT MAX(0, 0)
' MIN/MAX with expressions
DIM a%, b%
a% = 10
b% = 20
PRINT MIN(a%, b%)
PRINT MAX(a%, b%)
PRINT MIN(a% + 5, b% - 5)
PRINT MAX(a% * 2, b%)
' Nested MIN/MAX
PRINT MIN(MIN(3, 5), MIN(2, 4))
PRINT MAX(MAX(3, 5), MAX(2, 4))
PRINT MIN(MAX(1, 3), MAX(2, 4))
' MIN/MAX in expressions
PRINT MIN(10, 20) + MAX(1, 2)
PRINT MIN(10, 20) * MAX(1, 2)
