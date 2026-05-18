OPEN "B:/many_lines.tmp" FOR OUTPUT AS #1
FOR i% = 1 TO 150
  PRINT #1, i%
NEXT i%
CLOSE #1
OPEN "B:/many_lines.tmp" FOR INPUT AS #1
FOR i% = 1 TO 150
  LINE INPUT #1, a$
  IF i% = 1 AND a$ <> " 1" THEN ERROR "many first"
  IF i% = 75 AND a$ <> " 75" THEN ERROR "many middle"
  IF i% = 150 AND a$ <> " 150" THEN ERROR "many last"
NEXT i%
CLOSE #1
PRINT "many lines ok"
