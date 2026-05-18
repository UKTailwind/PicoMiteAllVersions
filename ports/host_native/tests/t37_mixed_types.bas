' Test mixed type operations and coercions
' Float function taking int args
FUNCTION Hypotenuse!(a%, b%)
  Hypotenuse! = SQR(a% * a% + b% * b%)
END FUNCTION
PRINT Hypotenuse!(3, 4)
PRINT Hypotenuse!(5, 12)
PRINT Hypotenuse!(8, 15)
' String function returning computed results
FUNCTION Describe$(n%)
  IF n% < 0 THEN
    Describe$ = "negative"
  ELSEIF n% = 0 THEN
    Describe$ = "zero"
  ELSEIF n% < 10 THEN
    Describe$ = "small"
  ELSEIF n% < 100 THEN
    Describe$ = "medium"
  ELSE
    Describe$ = "large"
  ENDIF
END FUNCTION
PRINT Describe$(-5)
PRINT Describe$(0)
PRINT Describe$(7)
PRINT Describe$(42)
PRINT Describe$(999)
' Int function with float intermediate
FUNCTION RoundToNearest%(v!, s%)
  RoundToNearest% = INT(v! / s% + 0.5) * s%
END FUNCTION
PRINT RoundToNearest%(17.3, 5)
PRINT RoundToNearest%(22.7, 5)
PRINT RoundToNearest%(99.1, 10)
' Mixed arithmetic in expressions
DIM x%, f!
x% = 10
f! = 3.5
PRINT x% + f!
PRINT x% * f!
PRINT x% / 3
PRINT x% \ 3
PRINT x% MOD 3
' Chained function calls with different return types
PRINT LEN(Describe$(RoundToNearest%(47.8, 10)))

