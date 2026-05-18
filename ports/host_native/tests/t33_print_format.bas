' Test PRINT formatting options
' Comma separator (tabs to next zone)
PRINT 1, 2, 3
PRINT "A", "B", "C"
' Semicolon (no space between strings)
PRINT "Hello"; " "; "World"
' Mixed number and string
PRINT "x="; 42
PRINT "pi="; 3.14159
' Multiple PRINT on same line
PRINT 1;
PRINT 2;
PRINT 3
' Empty PRINT (just newline)
PRINT "before"
PRINT
PRINT "after"
' Number formatting
PRINT 0
PRINT -1
PRINT 1000000
PRINT 3.14159
PRINT -0.001
