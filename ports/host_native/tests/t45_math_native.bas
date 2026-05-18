' Test all native math functions thoroughly
' SQR
PRINT SQR(0)
PRINT SQR(1)
PRINT SQR(4)
PRINT SQR(2)
' LOG/EXP
PRINT LOG(1)
PRINT INT(EXP(1) * 10000 + 0.5) / 10000
PRINT INT(LOG(EXP(3)) * 10000 + 0.5) / 10000
PRINT INT(EXP(LOG(5)) * 10000 + 0.5) / 10000
' Trig identities: sin^2 + cos^2 = 1
DIM x!
x! = 0.7
PRINT INT((SIN(x!) * SIN(x!) + COS(x!) * COS(x!)) * 10000 + 0.5) / 10000
' TAN = SIN/COS
PRINT INT(TAN(0.5) * 10000 + 0.5) / 10000
PRINT INT((SIN(0.5) / COS(0.5)) * 10000 + 0.5) / 10000
' ATN is inverse of TAN
PRINT INT(ATN(TAN(0.5)) * 10000 + 0.5) / 10000
' RAD/DEG conversions
PRINT INT(RAD(180) * 10000 + 0.5) / 10000
PRINT INT(DEG(PI) * 10000 + 0.5) / 10000
PRINT INT(RAD(90) * 10000 + 0.5) / 10000
PRINT INT(DEG(PI / 2) * 10000 + 0.5) / 10000
' PI value check
PRINT INT(PI * 10000 + 0.5) / 10000
' Power operator
PRINT 2 ^ 10
PRINT 3 ^ 3
PRINT 10 ^ 0
PRINT 2 ^ -1
