DIM INTEGER arr%(4, 7)
arr%(0, 0) = 100
arr%(0, 1) = 200
arr%(0, 2) = 300

DIM c%
FOR c% = 0 TO 2
  PRINT "c%="; c%; " arr%(0,c%)="; arr%(0, c%)
NEXT
PRINT "done"
END
