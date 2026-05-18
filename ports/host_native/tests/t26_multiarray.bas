' Test multi-dimensional arrays and array operations
DIM grid%(4, 4)
DIM i%, j%
' Fill with multiplication table
FOR i% = 0 TO 4
  FOR j% = 0 TO 4
    grid%(i%, j%) = (i% + 1) * (j% + 1)
  NEXT j%
NEXT i%
' Print multiplication table
FOR i% = 0 TO 4
  FOR j% = 0 TO 4
    PRINT grid%(i%, j%);
    IF j% < 4 THEN PRINT ",";
  NEXT j%
  PRINT ""
NEXT i%
' Sum diagonal
DIM diag%
diag% = 0
FOR i% = 0 TO 4
  diag% = diag% + grid%(i%, i%)
NEXT i%
PRINT "Diagonal sum:"; diag%
' Sum all elements
DIM total%
total% = 0
FOR i% = 0 TO 4
  FOR j% = 0 TO 4
    total% = total% + grid%(i%, j%)
  NEXT j%
NEXT i%
PRINT "Total:"; total%
' Float array
DIM f!(9)
FOR i% = 0 TO 9
  f!(i%) = SQR(i%)
NEXT i%
FOR i% = 0 TO 9
  PRINT INT(f!(i%) * 1000) / 1000;
  IF i% < 9 THEN PRINT ",";
NEXT i%
PRINT ""
' String array
DIM names$(4)
names$(0) = "Alice"
names$(1) = "Bob"
names$(2) = "Charlie"
names$(3) = "Diana"
names$(4) = "Eve"
FOR i% = 0 TO 4
  PRINT names$(i%); " ("; LEN(names$(i%)); ")"
NEXT i%
