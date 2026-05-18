FUNCTION Double(x%) AS INTEGER
  Double = x% * 2
END FUNCTION
FUNCTION Half(x!) AS FLOAT
  Half = x! / 2
END FUNCTION
FUNCTION Greet(n$) AS STRING
  Greet = "Hi " + n$
END FUNCTION
DIM r%, f!, s$
r% = Double(5)
PRINT r%
r% = Double(3) + 1
PRINT r%
f! = Half(7.0)
PRINT f!
s$ = Greet("Bob")
PRINT s$
' Test suffix-less CONST assigned to int var (store coercion)
CONST RATE = 3.14
DIM n%
n% = RATE
PRINT n%
' Test local store coercion (float expr into int local)
SUB TestLocal()
  LOCAL a%
  a% = 7.9
  PRINT a%
END SUB
TestLocal
