' Stress test EXIT FOR fixup limit (array is 16 entries)
' First test: exits 1-16 are always-false, exit 17 and 18 are real
' If fixup truncation happens at 16, the real exit won't be patched
DIM x%
x% = 0
FOR i% = 1 TO 1000
  x% = i%
  IF x% = 900 THEN EXIT FOR
  IF x% = 901 THEN EXIT FOR
  IF x% = 902 THEN EXIT FOR
  IF x% = 903 THEN EXIT FOR
  IF x% = 904 THEN EXIT FOR
  IF x% = 905 THEN EXIT FOR
  IF x% = 906 THEN EXIT FOR
  IF x% = 907 THEN EXIT FOR
  IF x% = 908 THEN EXIT FOR
  IF x% = 909 THEN EXIT FOR
  IF x% = 910 THEN EXIT FOR
  IF x% = 911 THEN EXIT FOR
  IF x% = 912 THEN EXIT FOR
  IF x% = 913 THEN EXIT FOR
  IF x% = 914 THEN EXIT FOR
  IF x% = 915 THEN EXIT FOR
  IF x% = 916 THEN EXIT FOR
  IF x% = 5 THEN EXIT FOR
NEXT i%
PRINT "x="; x%
' Second test: only 15 exits (within limit) — should always work
DIM y%
y% = 0
FOR j% = 1 TO 1000
  y% = j%
  IF y% = 900 THEN EXIT FOR
  IF y% = 901 THEN EXIT FOR
  IF y% = 902 THEN EXIT FOR
  IF y% = 903 THEN EXIT FOR
  IF y% = 904 THEN EXIT FOR
  IF y% = 905 THEN EXIT FOR
  IF y% = 906 THEN EXIT FOR
  IF y% = 907 THEN EXIT FOR
  IF y% = 908 THEN EXIT FOR
  IF y% = 909 THEN EXIT FOR
  IF y% = 910 THEN EXIT FOR
  IF y% = 911 THEN EXIT FOR
  IF y% = 912 THEN EXIT FOR
  IF y% = 913 THEN EXIT FOR
  IF y% = 914 THEN EXIT FOR
  IF y% = 3 THEN EXIT FOR
NEXT j%
PRINT "y="; y%
