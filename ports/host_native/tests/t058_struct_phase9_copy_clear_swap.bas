' Phase 9: STRUCT COPY / STRUCT CLEAR / STRUCT SWAP
' Covers acceptance tests 8, 9, 33, 34, 35, 36, 49, 50.

Type Point
  x As INTEGER
  y As INTEGER
End Type

Type Person
  name As STRING LENGTH 16
  age As INTEGER
  height As FLOAT
End Type

' --- Test 8: STRUCT COPY single ---
Dim src As Point, dst As Point
src.x = 999 : src.y = 888
dst.x = 0   : dst.y = 0
Struct Copy src To dst
Print "copy1:"; dst.x; ","; dst.y

' --- Test 9: STRUCT COPY mixed types ---
Dim srcP As Person, dstP As Person
srcP.age = 25
srcP.height = 1.75
srcP.name = "Alice"
Struct Copy srcP To dstP
Print "copy2:"; dstP.age; ",";dstP.height;",";dstP.name

' --- Test 33: STRUCT CLEAR single ---
Dim cp As Person
cp.age = 42 : cp.height = 1.85 : cp.name = "TestName"
Struct Clear cp
Print "clear1:"; cp.age; ",";cp.height;",";"["+cp.name+"]"

' --- Test 34: STRUCT CLEAR array ---
Dim carr(2) As Point
carr(0).x = 1 : carr(0).y = 2
carr(1).x = 3 : carr(1).y = 4
carr(2).x = 5 : carr(2).y = 6
Struct Clear carr()
Dim ok% = 1, i%
For i% = 0 To 2
  If carr(i%).x <> 0 Or carr(i%).y <> 0 Then ok% = 0
Next i%
Print "clear2:"; ok%

' --- Test 35: STRUCT SWAP ---
Dim swapA As Point, swapB As Point
swapA.x = 100 : swapA.y = 200
swapB.x = 300 : swapB.y = 400
Struct Swap swapA, swapB
Print "swap1:"; swapA.x; ","; swapA.y; "|"; swapB.x; ","; swapB.y

' --- Test 36: STRUCT SWAP array elements ---
Dim swarr(1) As Person
swarr(0).name = "Alice" : swarr(0).age = 25
swarr(1).name = "Bob"   : swarr(1).age = 30
Struct Swap swarr(0), swarr(1)
Print "swap2:"; swarr(0).name;",";swarr(0).age;"|";swarr(1).name;",";swarr(1).age

' --- Test 49: STRUCT COPY array() TO array() (dst larger) ---
Dim srcArr(2) As Point, dstArr(4) As Point
srcArr(0).x = 10 : srcArr(0).y = 11
srcArr(1).x = 20 : srcArr(1).y = 21
srcArr(2).x = 30 : srcArr(2).y = 31
Struct Copy srcArr() To dstArr()
Print "arr1:";dstArr(0).x;",";dstArr(0).y;"|";dstArr(1).x;",";dstArr(1).y;"|";dstArr(2).x;",";dstArr(2).y

' --- Test 50: STRUCT COPY preserves extra elements ---
Dim srcS(1) As Point, dstS(3) As Point
srcS(0).x = 100 : srcS(0).y = 101
srcS(1).x = 200 : srcS(1).y = 201
dstS(2).x = 999 : dstS(2).y = 998
dstS(3).x = 888 : dstS(3).y = 887
Struct Copy srcS() To dstS()
Print "arr2:";dstS(2).x;",";dstS(2).y;"|";dstS(3).x;",";dstS(3).y
