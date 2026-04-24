FUNCTION square(n)
  square = n * n
END FUNCTION

SUB shout
  PRINT "called"
END SUB

PRINT square(5)
shout

' EXPECT:  25
' EXPECT: called
