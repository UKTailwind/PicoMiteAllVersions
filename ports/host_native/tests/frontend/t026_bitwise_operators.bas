DIM a% = &HFF, b% = &H0F, c%
c% = a% AND b%
PRINT HEX$(c%)
c% = a% OR &HF00
PRINT HEX$(c%)
c% = a% XOR b%
PRINT HEX$(c%)
PRINT NOT 0
PRINT NOT 1
PRINT &HFF << 4
PRINT &HFF >> 4
IF (1 AND NOT 0) THEN PRINT "and"
IF (0 OR 1) THEN PRINT "or"
IF (1 XOR 0) THEN PRINT "xor"
