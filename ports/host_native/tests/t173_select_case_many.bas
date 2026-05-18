' Stress test SELECT CASE — many CASE clauses (case_end_fixups[32])
' Uses PRINT with numbers to avoid exhausting the string constant pool
DIM x%
x% = 25
SELECT CASE x%
  CASE 1
    PRINT 1
  CASE 2
    PRINT 2
  CASE 3
    PRINT 3
  CASE 4
    PRINT 4
  CASE 5
    PRINT 5
  CASE 6
    PRINT 6
  CASE 7
    PRINT 7
  CASE 8
    PRINT 8
  CASE 9
    PRINT 9
  CASE 10
    PRINT 10
  CASE 11
    PRINT 11
  CASE 12
    PRINT 12
  CASE 13
    PRINT 13
  CASE 14
    PRINT 14
  CASE 15
    PRINT 15
  CASE 16
    PRINT 16
  CASE 17
    PRINT 17
  CASE 18
    PRINT 18
  CASE 19
    PRINT 19
  CASE 20
    PRINT 20
  CASE 21
    PRINT 21
  CASE 22
    PRINT 22
  CASE 23
    PRINT 23
  CASE 24
    PRINT 24
  CASE 25
    PRINT 25
  CASE 26
    PRINT 26
  CASE 27
    PRINT 27
  CASE 28
    PRINT 28
  CASE 29
    PRINT 29
  CASE 30
    PRINT 30
  CASE ELSE
    PRINT "other"
END SELECT
' Test first case
x% = 1
SELECT CASE x%
  CASE 1
    PRINT "first"
  CASE 2
    PRINT "second"
  CASE 3
    PRINT "third"
  CASE ELSE
    PRINT "high"
END SELECT
' Test CASE ELSE
x% = 999
SELECT CASE x%
  CASE 1
    PRINT "a"
  CASE 2
    PRINT "b"
  CASE 3
    PRINT "c"
  CASE ELSE
    PRINT "else"
END SELECT
