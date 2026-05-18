OPTION EXPLICIT

FUNCTION Mix(a AS FLOAT, b AS FLOAT) AS FLOAT
  LOCAL FLOAT s, l, c
  s = a + b
  l = a * b
  c = s + l
  Mix = c
END FUNCTION

PRINT INT(Mix(1.5, 2.0) * 10)
