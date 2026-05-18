' Test bitwise operations
DIM a% = &HFF, b% = &H0F, c%
c% = a% AND b%
PRINT HEX$(c%)
c% = a% OR &HF00
PRINT HEX$(c%)
c% = a% XOR b%
PRINT HEX$(c%)
c% = NOT 0
PRINT c%
c% = a% << 4
PRINT HEX$(c%)
c% = a% >> 4
PRINT HEX$(c%)
' Hex/Oct/Bin literals
PRINT &HFF
PRINT &O77
PRINT &B1010
