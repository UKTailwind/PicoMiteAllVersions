' Test arrays inside functions — bubble sort and binary search
DIM a%(9)
SUB FillArray(n%)
  LOCAL i%
  ' Fill with pseudo-random values using simple LCG
  a%(0) = 37
  FOR i% = 1 TO n% - 1
    a%(i%) = (a%(i% - 1) * 31 + 17) MOD 100
  NEXT i%
END SUB
SUB PrintArray(n%)
  LOCAL i%
  FOR i% = 0 TO n% - 1
    PRINT a%(i%);
    IF i% < n% - 1 THEN PRINT ",";
  NEXT i%
  PRINT ""
END SUB
SUB BubbleSort(n%)
  LOCAL i%, j%, t%
  FOR i% = 0 TO n% - 2
    FOR j% = 0 TO n% - 2 - i%
      IF a%(j%) > a%(j% + 1) THEN
        t% = a%(j%)
        a%(j%) = a%(j% + 1)
        a%(j% + 1) = t%
      ENDIF
    NEXT j%
  NEXT i%
END SUB
FUNCTION BinSearch%(n%, target%)
  LOCAL lo%, hi%, mid%
  lo% = 0
  hi% = n% - 1
  BinSearch% = -1
  DO WHILE lo% <= hi%
    mid% = (lo% + hi%) \ 2
    IF a%(mid%) = target% THEN
      BinSearch% = mid%
      EXIT DO
    ELSEIF a%(mid%) < target% THEN
      lo% = mid% + 1
    ELSE
      hi% = mid% - 1
    ENDIF
  LOOP
END FUNCTION
FillArray 10
PRINT "Before:"
PrintArray 10
BubbleSort 10
PRINT "After:"
PrintArray 10
' Search for known values
DIM idx%
idx% = BinSearch%(10, a%(0))
PRINT "Found a(0) at index"; idx%
idx% = BinSearch%(10, a%(9))
PRINT "Found a(9) at index"; idx%
idx% = BinSearch%(10, 999)
PRINT "Search 999:"; idx%
