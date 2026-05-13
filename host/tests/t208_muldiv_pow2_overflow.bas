OPTION EXPLICIT

DIM INTEGER a%, b%, r%

a% = 5368709120
b% = 3221225472
r% = (a% * b%) \ 1073741824
IF r% <> -1073741824 THEN ERROR "overflow muldiv: " + STR$(r%)

OverflowFast
PRINT "ok"
END

SUB OverflowFast()
  LOCAL INTEGER a%, b%, r%, i%
  a% = 5368709120
  b% = 3221225472
  i% = 0
  '!FAST
  DO WHILE i% < 1
    r% = (a% * b%) \ 1073741824
    i% = i% + 1
  LOOP
  IF r% <> -1073741824 THEN ERROR "fast overflow muldiv: " + STR$(r%)
END SUB
