' Test RESTORE <label> — seek DATA pointer to the labelled block.
' Used by Picovaders.bas (Restore sr1 / xpld / ufo) to switch between
' independent DATA blocks of sprite bitmaps.
'
' Interpreter accepts; VM source frontend must too.

Restore block_b
Read x%
Print x%
Read x%
Print x%

Restore block_a
Read x%
Print x%

Restore block_c
Read x%
Print x%
Read x%
Print x%

' RESTORE with no label = rewind to first DATA in source order.
Restore
Read x%
Print x%

End

block_a:
Data 11, 22, 33

block_b:
Data 100, 200, 300

block_c:
Data 7000, 8000, 9000
