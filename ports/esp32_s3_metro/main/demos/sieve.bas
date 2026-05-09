' sieve.bas - Sieve of Eratosthenes, prints elapsed time
MaxN = 6000
DIM composite(MaxN)
t0 = TIMER
FOR i = 2 TO MaxN
  IF composite(i) = 0 THEN
    FOR j = i * i TO MaxN STEP i
      composite(j) = 1
    NEXT j
  END IF
NEXT i
PRINT "Sieve(" + STR$(MaxN) + ") in", TIMER - t0, "ms"
END
