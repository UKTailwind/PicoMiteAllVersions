' RUN_ARGS: --interp
' Large-file round-trip — exercises the 512-byte SDbuffer boundary in
' FileIO.c's FileGetChar read buffering. Interp-only: the VM uses a
' different native file path (vm_sys_file) whose handle table doesn't
' sync with FileTable[] used by the bridged interp primitives.
OPEN "B:/big.tmp" FOR OUTPUT AS #1
Dim i%
For i% = 1 To 800
  PRINT #1, "line " + Str$(i%)
Next i%
CLOSE #1

OPEN "B:/big.tmp" FOR INPUT AS #1
Dim line$
For i% = 1 To 800
  LINE INPUT #1, line$
  ' Spot-check line 1, boundary lines (every ~40 to catch the 512-byte
  ' refills), and the last line.
  If (i% Mod 40) = 0 Or i% = 1 Or i% = 800 Then
    If line$ <> "line " + Str$(i%) Then
      ERROR "mismatch at line " + Str$(i%) + ": got '" + line$ + "'"
    EndIf
  EndIf
Next i%
IF EOF(#1) = 0 THEN ERROR "EOF not set after reading all lines"
CLOSE #1

KILL "B:/big.tmp"
PRINT "large file ok"
