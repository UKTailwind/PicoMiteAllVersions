FUNCTION F(x%) AS INTEGER
  F = x% + 1
END FUNCTION

DIM INTEGER a%(1,1)
DIM v%
a%(0,0)=41
v% = F(a%(0,0))
PRINT v%
