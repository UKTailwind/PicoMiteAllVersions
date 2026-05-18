' Test: bridge dispatch for unsupported functions
' BOUND() is not natively compiled by the VM — goes through OP_BRIDGE_FUN_I
DIM a%(10)
PRINT BOUND(a%())
DIM b!(3, 5)
PRINT BOUND(b!(), 1)
PRINT BOUND(b!(), 2)
' Verify variables survive function bridge calls
x% = 99
PRINT BOUND(a%())
PRINT x%
' Test bridged function in expression
y% = BOUND(a%()) + 5
PRINT y%
