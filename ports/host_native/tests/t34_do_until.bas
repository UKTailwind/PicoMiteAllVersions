' Test DO/LOOP variations
DIM n%
' DO WHILE...LOOP (check at top)
n% = 1
DO WHILE n% <= 5
  PRINT n%;
  n% = n% + 1
LOOP
PRINT ""
' DO...LOOP UNTIL (check at bottom, always runs once)
n% = 10
DO
  PRINT n%;
  n% = n% + 1
LOOP UNTIL n% > 12
PRINT ""
' DO...LOOP WHILE (check at bottom)
n% = 1
DO
  PRINT n%;
  n% = n% + 1
LOOP WHILE n% <= 3
PRINT ""
' Nested DO loops
DIM i%, j%, total%
total% = 0
i% = 1
DO WHILE i% <= 3
  j% = 1
  DO WHILE j% <= 3
    total% = total% + i% * j%
    j% = j% + 1
  LOOP
  i% = i% + 1
LOOP
PRINT "Nested total:"; total%
' EXIT DO from inner loop
n% = 0
i% = 0
DO WHILE i% < 100
  i% = i% + 1
  IF i% MOD 7 = 0 THEN EXIT DO
  n% = n% + 1
LOOP
PRINT "Exit at i="; i%; " n="; n%
