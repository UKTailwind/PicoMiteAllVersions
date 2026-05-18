' Phase 7 struct as SUB/FUNCTION argument (acceptance tests 4, 6, 10, 11, 14;
' 5 and 7 cover array-element and array BYREF).

Type Point
  x As INTEGER
  y As INTEGER
End Type

' --- Test 4: pass scalar struct BYREF, sub modifies it ---
Sub DoublePoint(pt As Point)
  pt.x = pt.x * 2
  pt.y = pt.y * 2
End Sub

Dim p2 As Point
p2.x = 5 : p2.y = 7
DoublePoint p2
Print p2.x; ","; p2.y

' --- Test 5: pass array element BYREF ---
Dim arr(3) As Point
arr(1).x = 100 : arr(1).y = 200
DoublePoint arr(1)
Print arr(1).x; ","; arr(1).y

' --- Test 6: pass struct array BYREF, read it ---
Sub SumPoints(pts() As Point, count%)
  Local sum_x% = 0, sum_y% = 0, j%
  For j% = 0 To count% - 1
    sum_x% = sum_x% + pts(j%).x
    sum_y% = sum_y% + pts(j%).y
  Next j%
  Print "sumx=";sum_x%;" sumy=";sum_y%
End Sub

Dim testArr(2) As Point
testArr(0).x = 1 : testArr(0).y = 2
testArr(1).x = 3 : testArr(1).y = 4
testArr(2).x = 5 : testArr(2).y = 6
SumPoints testArr(), 3

' --- Test 7: sub mutates struct array ---
Sub ZeroPoints(pts() As Point, count%)
  Local k%
  For k% = 0 To count% - 1
    pts(k%).x = 0
    pts(k%).y = 0
  Next k%
End Sub

ZeroPoints testArr(), 3
Print testArr(0).x; ",";testArr(1).y; ",";testArr(2).x

' --- Test 10: function takes struct, returns scalar ---
Function GetDistance(pt As Point) As FLOAT
  GetDistance = Sqr(pt.x * pt.x + pt.y * pt.y)
End Function

Dim p3 As Point
p3.x = 3 : p3.y = 4
Print GetDistance(p3)

' --- Test 11: struct members in expressions ---
Dim pA As Point, pB As Point
pA.x = 10 : pA.y = 20
pB.x = 30 : pB.y = 40
Print pA.x + pB.x + pA.y + pB.y

' --- Test 14: read-only access, caller unchanged ---
Sub ReadOnlyTest(pt As Point)
  Local v% = pt.x + pt.y
  Print "ro=";v%
End Sub

Dim p4 As Point
p4.x = 111 : p4.y = 222
ReadOnlyTest p4
Print p4.x; ","; p4.y
