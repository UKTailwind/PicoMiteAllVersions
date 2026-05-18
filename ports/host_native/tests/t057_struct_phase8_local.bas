' Phase 8 LOCAL struct in SUB/FUN (acceptance tests 19, 20, 21, 22).
' - LOCAL scalar struct
' - LOCAL struct array
' - LOCAL struct with initializer
' - Many calls with LOCAL structs (memory cleanup)

Type Point
  x As INTEGER
  y As INTEGER
End Type

' --- Test 19: LOCAL scalar struct ---
Sub TestLocalStruct
  Local localPt As Point
  localPt.x = 555
  localPt.y = 666
  Print "scalar:"; localPt.x; ","; localPt.y
  If localPt.x = 555 And localPt.y = 666 Then
    Print "local_scalar_ok"
  Else
    Print "local_scalar_fail"
  EndIf
End Sub

TestLocalStruct

' --- Test 20: LOCAL struct array ---
Sub TestLocalStructArray
  Local localArr(2) As Point
  localArr(0).x = 1 : localArr(0).y = 2
  localArr(1).x = 3 : localArr(1).y = 4
  localArr(2).x = 5 : localArr(2).y = 6

  Local ok% = 1
  If localArr(0).x <> 1 Or localArr(0).y <> 2 Then ok% = 0
  If localArr(1).x <> 3 Or localArr(1).y <> 4 Then ok% = 0
  If localArr(2).x <> 5 Or localArr(2).y <> 6 Then ok% = 0

  If ok% Then
    Print "local_array_ok"
  Else
    Print "local_array_fail"
  EndIf
End Sub

TestLocalStructArray

' --- Test 22: Multiple LOCAL struct calls, simple memory stress ---
Sub MemTest
  Local tmp As Point
  tmp.x = 123
  tmp.y = 456
End Sub

Dim i%
For i% = 1 To 10
  MemTest
Next i%
Print "memstress_ok"

' --- LOCAL struct shadowing global ---
Dim g As Point
g.x = 999 : g.y = 888

Sub ShadowTest
  Local g As Point
  g.x = 1 : g.y = 2
  Print "inside_shadow:"; g.x; ","; g.y
End Sub

ShadowTest
Print "outside_shadow:"; g.x; ","; g.y

' --- FUNCTION with LOCAL struct (exit through function return) ---
Function Mag%(sx%, sy%)
  Local p As Point
  p.x = sx% : p.y = sy%
  Mag% = p.x * p.x + p.y * p.y
End Function

Print "mag=";Mag%(3, 4)

' --- STATIC struct retains across calls ---
Sub CounterPoint
  Static sp As Point
  sp.x = sp.x + 1
  sp.y = sp.y + 10
  Print "static:"; sp.x; ","; sp.y
End Sub

CounterPoint
CounterPoint
CounterPoint
