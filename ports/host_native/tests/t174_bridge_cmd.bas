' Test: bridge dispatch for unsupported commands
' SORT is not natively compiled by the VM — goes through OP_BRIDGE_CMD
DIM a%(4)
a%(0) = 50
a%(1) = 30
a%(2) = 10
a%(3) = 40
a%(4) = 20
SORT a%()
PRINT a%(0); a%(1); a%(2); a%(3); a%(4)
' Verify scalar variables survive bridge calls
x% = 42
SORT a%()
PRINT x%
