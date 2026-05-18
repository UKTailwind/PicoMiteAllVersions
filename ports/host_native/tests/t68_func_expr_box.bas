CONST A% = 30
CONST B% = 12

FUNCTION F(x%) AS INTEGER
  PRINT "F("; x%; ") A%="; A%; " B%="; B%
  IF x% = A% THEN
    F = 111
  ELSE IF x% = B% THEN
    F = 222
  ELSE
    F = 999
  END IF
  PRINT "F="; F
END FUNCTION

CLS
DIM INTEGER arr%(3)
arr%(0) = 30
arr%(1) = 12
arr%(2) = 30

DIM c%
FOR c% = 0 TO 2
  BOX c%*40+4, 10, 35, 12, 0, , F(arr%(c%))
NEXT
PRINT "done"
END
