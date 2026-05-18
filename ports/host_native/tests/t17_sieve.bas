' Sieve of Eratosthenes benchmark
DIM sieve%(1000)
DIM i%, j%, count%
FOR i% = 0 TO 999
  sieve%(i%) = 1
NEXT i%
count% = 0
FOR i% = 2 TO 999
  IF sieve%(i%) THEN
    count% = count% + 1
    FOR j% = i% + i% TO 999 STEP i%
      sieve%(j%) = 0
    NEXT j%
  ENDIF
NEXT i%
PRINT "Primes:"; count%
