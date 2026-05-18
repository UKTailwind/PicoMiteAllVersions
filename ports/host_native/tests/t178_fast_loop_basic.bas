' RUN_ARGS: --vm
' Basic '!FAST loop tests
OPTION EXPLICIT

Sub TestCounter()
  Local x%, total%
  x% = 0
  total% = 0
  '!FAST
  Do While x% < 100
    total% = total% + x%
    x% = x% + 1
  Loop
  If x% <> 100 Then ERROR "counter: x=" + Str$(x%)
  If total% <> 4950 Then ERROR "counter: total=" + Str$(total%)
  PRINT "counter ok"
End Sub

Sub TestNestedIf()
  ' Test nested IF inside fast loop using simple comparison
  Local i%, a%, b%
  i% = 0 : a% = 0 : b% = 0
  '!FAST
  Do While i% < 50
    If i% < 25 Then
      a% = a% + 1
    Else
      b% = b% + 1
    EndIf
    i% = i% + 1
  Loop
  If a% <> 25 Then ERROR "nested: a=" + Str$(a%)
  If b% <> 25 Then ERROR "nested: b=" + Str$(b%)
  PRINT "nested ok"
End Sub

Sub TestExitDo()
  Local i%
  i% = 0
  '!FAST
  Do While i% < 1000
    If i% = 42 Then Exit Do
    i% = i% + 1
  Loop
  If i% <> 42 Then ERROR "exit: i=" + Str$(i%)
  PRINT "exit ok"
End Sub

Sub TestBitwise()
  ' Test shift and bitwise ops in fast loop
  Local v%, i%, mask%
  v% = 255
  i% = 0
  '!FAST
  Do While i% < 8
    mask% = 1 << i%
    v% = v% - mask%
    i% = i% + 1
  Loop
  If v% <> 0 Then ERROR "bitwise: v=" + Str$(v%)
  PRINT "bitwise ok"
End Sub

Sub TestMulshr()
  ' Test fused SQRSHR/MULSHR in fast loop
  Const SCALE = 1073741824
  Local a%, r%, i%
  a% = 1073741824
  i% = 0
  r% = 0
  '!FAST
  Do While i% < 10
    r% = (a% * a%) \ SCALE
    a% = r%
    i% = i% + 1
  Loop
  PRINT "mulshr ok: r=" + Str$(r%)
End Sub

TestCounter
TestNestedIf
TestExitDo
TestBitwise
TestMulshr
