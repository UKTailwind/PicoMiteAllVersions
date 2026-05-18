OPTION EXPLICIT

CONST SCALE = 1073741824
CONST HALF_SCALE = 536870912

FUNCTION ShrTowardZero(v%, bits%) AS INTEGER
  IF v% >= 0 THEN
    ShrTowardZero = v% >> bits%
  ELSE
    ShrTowardZero = -((-v%) >> bits%)
  ENDIF
END FUNCTION

IF ShrTowardZero(-1, 1) <> (-1 \ 2) THEN ERROR "shift mismatch 1"
IF ShrTowardZero(-2, 1) <> (-2 \ 2) THEN ERROR "shift mismatch 2"
IF ShrTowardZero(-3, 1) <> (-3 \ 2) THEN ERROR "shift mismatch 3"
IF ShrTowardZero(-SCALE, 30) <> (-SCALE \ SCALE) THEN ERROR "shift mismatch 4"
IF ShrTowardZero(-HALF_SCALE, 29) <> (-HALF_SCALE \ HALF_SCALE) THEN ERROR "shift mismatch 5"
IF ShrTowardZero((3 * SCALE) \ 2, 30) <> (((3 * SCALE) \ 2) \ SCALE) THEN ERROR "shift mismatch 6"

PRINT "ok"
