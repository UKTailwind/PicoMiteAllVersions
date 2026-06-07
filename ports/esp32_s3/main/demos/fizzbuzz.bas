' fizzbuzz.bas - classic FizzBuzz, 1..50
FOR i = 1 TO 50
  IF i MOD 15 = 0 THEN
    PRINT "FizzBuzz"
  ELSEIF i MOD 3 = 0 THEN
    PRINT "Fizz"
  ELSEIF i MOD 5 = 0 THEN
    PRINT "Buzz"
  ELSE
    PRINT i
  END IF
NEXT i
END
