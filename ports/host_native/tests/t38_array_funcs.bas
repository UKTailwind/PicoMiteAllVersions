' Test array operations with functions using global arrays
DIM nums%(19)
DIM i%
FOR i% = 0 TO 19
  nums%(i%) = (i% * 7 + 3) MOD 20
NEXT i%
' Find min using global array
FUNCTION FindMin%(n%)
  LOCAL i%, m%
  m% = nums%(0)
  FOR i% = 1 TO n% - 1
    IF nums%(i%) < m% THEN m% = nums%(i%)
  NEXT i%
  FindMin% = m%
END FUNCTION
FUNCTION FindMax%(n%)
  LOCAL i%, m%
  m% = nums%(0)
  FOR i% = 1 TO n% - 1
    IF nums%(i%) > m% THEN m% = nums%(i%)
  NEXT i%
  FindMax% = m%
END FUNCTION
FUNCTION ArraySum%(n%)
  LOCAL i%, s%
  s% = 0
  FOR i% = 0 TO n% - 1
    s% = s% + nums%(i%)
  NEXT i%
  ArraySum% = s%
END FUNCTION
PRINT "Min:"; FindMin%(20)
PRINT "Max:"; FindMax%(20)
PRINT "Sum:"; ArraySum%(20)
' Modify array in SUB using global
SUB ReverseArray(n%)
  LOCAL i%, t%
  FOR i% = 0 TO n% \ 2 - 1
    t% = nums%(i%)
    nums%(i%) = nums%(n% - 1 - i%)
    nums%(n% - 1 - i%) = t%
  NEXT i%
END SUB
PRINT "Before:"; nums%(0); nums%(1); nums%(2)
ReverseArray 20
PRINT "After:"; nums%(0); nums%(1); nums%(2)
PRINT "Sum after reverse:"; ArraySum%(20)
