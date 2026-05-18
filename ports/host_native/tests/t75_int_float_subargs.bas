OPTION EXPLICIT

DIM FLOAT px!, py!

SUB ShowPos(x%, y%)
  PRINT x%; ","; y%
END SUB

px! = 131.5
py! = 221.25

ShowPos INT(px!), INT(py!)
