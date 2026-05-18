' Test native math functions (ASIN, ACOS, ATAN2, RND)
' Inverse trig
PRINT ASIN(0)
PRINT ASIN(0.5)
PRINT ASIN(1)
PRINT ACOS(0)
PRINT ACOS(0.5)
PRINT ACOS(1)
PRINT ATAN2(0, 1)
PRINT ATAN2(1, 0)
PRINT ATAN2(1, 1)
PRINT ATAN2(-1, -1)
' Verify inverse relationships: ASIN(SIN(x)) = x
DIM x!, y!
x! = 0.3
y! = SIN(x!)
PRINT INT(ASIN(y!) * 10000 + 0.5) / 10000
x! = 0.7
y! = COS(x!)
PRINT INT(ACOS(y!) * 10000 + 0.5) / 10000
' RND reproducibility with seed
RANDOMIZE 12345
DIM r1!, r2!, r3!
r1! = RND
r2! = RND
r3! = RND
RANDOMIZE 12345
PRINT r1! = RND
PRINT r2! = RND
PRINT r3! = RND
