' Comprehensive Structure Test for PicoMite
' Tests all implemented struct features

Option Explicit
Option Default None

Print "=== Structure Tests ==="
Print

' ============================================
' TEST 1: Basic TYPE definition and simple struct
' ============================================
Print "TEST 1: Basic struct definition and access"

Type Point
  x As INTEGER
  y As INTEGER
End Type

Dim p1 As Point
p1.x = 100
p1.y = 200

If p1.x = 100 And p1.y = 200 Then
  Print "  PASS: Simple struct assignment and read"
Else
  Print "  FAIL: Expected 100,200 got"; p1.x; ","; p1.y
EndIf

' ============================================
' TEST 2: Struct with different member types
' ============================================
Print "TEST 2: Struct with mixed types"

Type Person
  age As INTEGER
  height As FLOAT
  name As STRING
End Type

Dim person1 As Person
person1.age = 42
person1.height = 1.85
person1.name = "John"

If person1.age = 42 And person1.height = 1.85 And person1.name = "John" Then
  Print "  PASS: Mixed type struct"
Else
  Print "  FAIL: Mixed type struct failed"
EndIf

' ============================================
' TEST 3: Array of structures
' ============================================
Print "TEST 3: Array of structures"

Dim points(5) As Point
points(0).x = 10 : points(0).y = 11
points(1).x = 20 : points(1).y = 21
points(2).x = 30 : points(2).y = 31
points(3).x = 40 : points(3).y = 41
points(4).x = 50 : points(4).y = 51
points(5).x = 60 : points(5).y = 61

Dim ok% = 1
Dim i%
For i% = 0 To 5
  If points(i%).x <> (i% + 1) * 10 Or points(i%).y <> (i% + 1) * 10 + 1 Then
    ok% = 0
  EndIf
Next i%

If ok% Then
  Print "  PASS: Array of structs"
Else
  Print "  FAIL: Array of structs"
EndIf

' ============================================
' TEST 4: Pass struct to SUB by reference
' ============================================
Print "TEST 4: Pass struct to SUB by reference"

Sub DoublePoint(pt As Point)
  pt.x = pt.x * 2
  pt.y = pt.y * 2
End Sub

Dim p2 As Point
p2.x = 5
p2.y = 7
DoublePoint p2

If p2.x = 10 And p2.y = 14 Then
  Print "  PASS: Struct passed by reference modified caller"
Else
  Print "  FAIL: Expected 10,14 got"; p2.x; ","; p2.y
EndIf

' ============================================
' TEST 5: Pass array element to SUB
' ============================================
Print "TEST 5: Pass array element to SUB"

Dim arr(3) As Point
arr(1).x = 100
arr(1).y = 200
DoublePoint arr(1)

If arr(1).x = 200 And arr(1).y = 400 Then
  Print "  PASS: Array element passed by reference"
Else
  Print "  FAIL: Expected 200,400 got"; arr(1).x; ","; arr(1).y
EndIf

' ============================================
' TEST 6: Pass struct array to SUB
' ============================================
Print "TEST 6: Pass struct array to SUB"

Sub SumPoints(pts() As Point, count%)
  Local sum_x% = 0, sum_y% = 0
  Local j%
  For j% = 0 To count% - 1
    sum_x% = sum_x% + pts(j%).x
    sum_y% = sum_y% + pts(j%).y
  Next j%
  Print "    Sum X ="; sum_x%; ", Sum Y ="; sum_y%
End Sub

Dim testArr(2) As Point
testArr(0).x = 1 : testArr(0).y = 2
testArr(1).x = 3 : testArr(1).y = 4
testArr(2).x = 5 : testArr(2).y = 6

Print "  Calling SumPoints (expect Sum X=9, Sum Y=12):"
SumPoints testArr(), 3
Print "  PASS: Array of structs passed to SUB (verify output above)"

' ============================================
' TEST 7: Modify struct array in SUB
' ============================================
Print "TEST 7: Modify struct array elements in SUB"

Sub ZeroPoints(pts() As Point, count%)
  Local k%
  For k% = 0 To count% - 1
    pts(k%).x = 0
    pts(k%).y = 0
  Next k%
End Sub

ZeroPoints testArr(), 3

ok% = 1
For i% = 0 To 2
  If testArr(i%).x <> 0 Or testArr(i%).y <> 0 Then ok% = 0
Next i%

If ok% Then
  Print "  PASS: SUB modified caller's struct array"
Else
  Print "  FAIL: Array not properly zeroed"
EndIf

' ============================================
' TEST 8: STRUCT COPY command
' ============================================
Print "TEST 8: STRUCT COPY command"

Dim src As Point, dst As Point
src.x = 999
src.y = 888
dst.x = 0
dst.y = 0

Struct Copy src To dst

If dst.x = 999 And dst.y = 888 Then
  Print "  PASS: STRUCT COPY works"
Else
  Print "  FAIL: Expected 999,888 got"; dst.x; ","; dst.y
EndIf

' ============================================
' TEST 9: STRUCT COPY with mixed types
' ============================================
Print "TEST 9: STRUCT COPY with mixed types"

Dim src_person As Person, dst_person As Person
src_person.age = 25
src_person.height = 1.75
src_person.name = "Alice"

Struct Copy src_person To dst_person

If dst_person.age = 25 And dst_person.height = 1.75 And dst_person.name = "Alice" Then
  Print "  PASS: STRUCT COPY with mixed types"
Else
  Print "  FAIL: STRUCT COPY mixed types failed"
EndIf

' ============================================
' TEST 10: Function returning value from struct member
' ============================================
Print "TEST 10: Function using struct member"

Function GetDistance(pt As Point) As FLOAT
  GetDistance = Sqr(pt.x * pt.x + pt.y * pt.y)
End Function

Dim p3 As Point
p3.x = 3
p3.y = 4
Dim dist! = GetDistance(p3)

If dist! = 5 Then
  Print "  PASS: Function with struct parameter"
Else
  Print "  FAIL: Expected 5 got"; dist!
EndIf

' ============================================
' TEST 11: Nested struct access in expressions
' ============================================
Print "TEST 11: Struct members in expressions"

Dim pA As Point, pB As Point
pA.x = 10 : pA.y = 20
pB.x = 30 : pB.y = 40

Dim sum_expr% = pA.x + pB.x + pA.y + pB.y

If sum_expr% = 100 Then
  Print "  PASS: Struct members in expressions"
Else
  Print "  FAIL: Expected 100 got"; sum_expr%
EndIf

' ============================================
' TEST 12: Array index with variable
' ============================================
Print "TEST 12: Array of structs with variable index"

Dim dynArr(9) As Point
For i% = 0 To 9
  dynArr(i%).x = i% * 2
  dynArr(i%).y = i% * 3
Next i%

ok% = 1
For i% = 0 To 9
  If dynArr(i%).x <> i% * 2 Or dynArr(i%).y <> i% * 3 Then
    ok% = 0
  EndIf
Next i%

If ok% Then
  Print "  PASS: Variable index into struct array"
Else
  Print "  FAIL: Variable index failed"
EndIf

' ============================================
' TEST 13: Multiple struct types
' ============================================
Print "TEST 13: Multiple struct type definitions"

Type Rectangle
  left As INTEGER
  top As INTEGER
  width As INTEGER
  height As INTEGER
End Type

Type Circle
  cx As INTEGER
  cy As INTEGER
  radius As FLOAT
End Type

Dim rect As Rectangle
Dim circ As Circle

rect.left = 10 : rect.top = 20 : rect.width = 100 : rect.height = 50
circ.cx = 50 : circ.cy = 50 : circ.radius = 25.5

If rect.left = 10 And rect.width = 100 And circ.radius = 25.5 Then
  Print "  PASS: Multiple struct types"
Else
  Print "  FAIL: Multiple struct types"
EndIf

' ============================================
' TEST 14: Struct parameter not modified without assignment
' ============================================
Print "TEST 14: Read-only access in SUB"

Sub ReadOnlyTest(pt As Point)
  Local val% = pt.x + pt.y
  Print "    Read value:"; val%
End Sub

Dim p4 As Point
p4.x = 111
p4.y = 222
ReadOnlyTest p4

If p4.x = 111 And p4.y = 222 Then
  Print "  PASS: Struct unchanged after read-only access"
Else
  Print "  FAIL: Struct was unexpectedly modified"
EndIf

' ============================================
' TEST 15: Simple struct initialization
' ============================================
Print "TEST 15: Simple struct initialization"

Dim initPoint As Point = (42, 99)

If initPoint.x = 42 And initPoint.y = 99 Then
  Print "  PASS: Simple struct initialization"
Else
  Print "  FAIL: Expected 42,99 got"; initPoint.x; ","; initPoint.y
EndIf

' ============================================
' TEST 16: Struct with string initialization
' ============================================
Print "TEST 16: Struct with string initialization"

Dim initPerson As Person = (30, 1.80, "Bob")

If initPerson.age = 30 And initPerson.height = 1.80 And initPerson.name = "Bob" Then
  Print "  PASS: Struct with string initialization"
Else
  Print "  FAIL: Struct with string init failed"
EndIf

' ============================================
' TEST 17: Array of structs initialization
' ============================================
Print "TEST 17: Array of structs initialization"

Dim initArr(1) As Point = (10, 20, 30, 40)

If initArr(0).x = 10 And initArr(0).y = 20 And initArr(1).x = 30 And initArr(1).y = 40 Then
  Print "  PASS: Array of structs initialization"
Else
  Print "  FAIL: Array init got"; initArr(0).x; ","; initArr(0).y; ","; initArr(1).x; ","; initArr(1).y
EndIf

' ============================================
' TEST 18: Mixed type struct array initialization
' ============================================
Print "TEST 18: Mixed type struct array initialization"

Type TestStruct
  num As INTEGER
  txt As STRING
End Type

Dim mixArr(1) As TestStruct = (100, "first", 200, "second")

If mixArr(0).num = 100 And mixArr(0).txt = "first" And mixArr(1).num = 200 And mixArr(1).txt = "second" Then
  Print "  PASS: Mixed type struct array initialization"
Else
  Print "  FAIL: Mixed init failed"
EndIf

' ============================================
' TEST 19: LOCAL struct in subroutine
' ============================================
Print "TEST 19: LOCAL struct in subroutine"

Sub TestLocalStruct
  Local localPt As Point
  localPt.x = 555
  localPt.y = 666
  Print "    Local struct values:"; localPt.x; ","; localPt.y
  If localPt.x = 555 And localPt.y = 666 Then
    Print "  PASS: LOCAL struct works"
  Else
    Print "  FAIL: LOCAL struct failed"
  EndIf
End Sub

TestLocalStruct

' ============================================
' TEST 20: LOCAL struct array in subroutine
' ============================================
Print "TEST 20: LOCAL struct array in subroutine"

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
    Print "  PASS: LOCAL struct array works"
  Else
    Print "  FAIL: LOCAL struct array failed"
  EndIf
End Sub

TestLocalStructArray

' ============================================
' TEST 21: LOCAL struct with initialization
' ============================================
Print "TEST 21: LOCAL struct with initialization"

Sub TestLocalStructInit
  Local initPt As Point = (777, 888)
  If initPt.x = 777 And initPt.y = 888 Then
    Print "  PASS: LOCAL struct initialization works"
  Else
    Print "  FAIL: Expected 777,888 got"; initPt.x; ","; initPt.y
  EndIf
End Sub

TestLocalStructInit

' ============================================
' TEST 22: Multiple LOCAL struct calls (memory test)
' ============================================
Print "TEST 22: Multiple LOCAL struct calls (memory test)"

Sub MemTest
  Local tmp As Point
  tmp.x = 123
  tmp.y = 456
End Sub

' Call multiple times to verify memory is properly freed
MemTest
MemTest
MemTest
MemTest
MemTest
Print "  PASS: Multiple LOCAL struct calls completed (no memory error)"

' ============================================
' TEST 23: STRUCT SORT by integer field
' ============================================
Print "TEST 23: STRUCT SORT by integer field"

Dim sortArr(4) As Point
sortArr(0).x = 50 : sortArr(0).y = 5
sortArr(1).x = 20 : sortArr(1).y = 2
sortArr(2).x = 40 : sortArr(2).y = 4
sortArr(3).x = 10 : sortArr(3).y = 1
sortArr(4).x = 30 : sortArr(4).y = 3

Struct Sort sortArr().x

If sortArr(0).x = 10 And sortArr(1).x = 20 And sortArr(2).x = 30 And sortArr(3).x = 40 And sortArr(4).x = 50 Then
  Print "  PASS: STRUCT SORT by integer ascending"
Else
  Print "  FAIL: Sort order wrong"
EndIf

' ============================================
' TEST 24: STRUCT SORT reverse
' ============================================
Print "TEST 24: STRUCT SORT reverse"

Struct Sort sortArr().x, 1

If sortArr(0).x = 50 And sortArr(1).x = 40 And sortArr(2).x = 30 And sortArr(3).x = 20 And sortArr(4).x = 10 Then
  Print "  PASS: STRUCT SORT reverse"
Else
  Print "  FAIL: Reverse sort order wrong"
EndIf

' ============================================
' TEST 25: STRUCT SORT by string field
' ============================================
Print "TEST 25: STRUCT SORT by string field"

Type NamedItem
  id As INTEGER
  name As STRING
End Type

Dim items(4) As NamedItem
items(0).id = 1 : items(0).name = "Cherry"
items(1).id = 2 : items(1).name = "Apple"
items(2).id = 3 : items(2).name = "Elderberry"
items(3).id = 4 : items(3).name = "Banana"
items(4).id = 5 : items(4).name = "Date"

Struct Sort items().name

If items(0).name = "Apple" And items(1).name = "Banana" And items(2).name = "Cherry" And items(3).name = "Date" And items(4).name = "Elderberry" Then
  Print "  PASS: STRUCT SORT by string"
Else
  Print "  FAIL: String sort wrong:"; items(0).name; ","; items(1).name; ","; items(2).name
EndIf

' Verify ID followed the sort
If items(0).id = 2 And items(1).id = 4 And items(2).id = 1 Then
  Print "  PASS: Other fields preserved during sort"
Else
  Print "  FAIL: ID values not preserved"
EndIf

' ============================================
' TEST 26: STRUCT SORT case insensitive
' ============================================
Print "TEST 26: STRUCT SORT case insensitive"

items(0).name = "cherry"
items(1).name = "APPLE"
items(2).name = "Elderberry"
items(3).name = "banana"
items(4).name = "DATE"

Struct Sort items().name, 2

If Left$(items(0).name, 1) = "A" Or Left$(items(0).name, 1) = "a" Then
  Print "  PASS: STRUCT SORT case insensitive (Apple/APPLE first)"
Else
  Print "  FAIL: Case insensitive sort wrong, first="; items(0).name
EndIf

' ============================================
' TEST 27: STRUCT SORT by float field
' ============================================
Print "TEST 27: STRUCT SORT by float field"

Type Measurement
  value As FLOAT
  label As STRING
End Type

Dim measurements(3) As Measurement
measurements(0).value = 3.14 : measurements(0).label = "pi"
measurements(1).value = 1.41 : measurements(1).label = "sqrt2"
measurements(2).value = 2.71 : measurements(2).label = "e"
measurements(3).value = 0.57 : measurements(3).label = "euler"

Struct Sort measurements().value

If measurements(0).value < measurements(1).value And measurements(1).value < measurements(2).value And measurements(2).value < measurements(3).value Then
  Print "  PASS: STRUCT SORT by float"
Else
  Print "  FAIL: Float sort wrong"
EndIf

' ============================================
' TEST 28: STRUCT SORT empty strings at end
' ============================================
Print "TEST 28: STRUCT SORT empty strings at end"

items(0).name = "Zebra"
items(1).name = ""
items(2).name = "Apple"
items(3).name = ""
items(4).name = "Mango"

Struct Sort items().name, 4

If items(3).name = "" And items(4).name = "" And items(0).name <> "" Then
  Print "  PASS: STRUCT SORT empty strings at end"
Else
  Print "  FAIL: Empty strings not at end"
EndIf

' ============================================
' TEST 29: Function returning struct
' ============================================
Print "TEST 29: Function returning struct"

Dim p29 As Point
p29 = MakePoint(123, 456)

If p29.x = 123 And p29.y = 456 Then
  Print "  PASS: Function returning struct"
Else
  Print "  FAIL: Expected 123,456 got"; p29.x; ","; p29.y
EndIf

' ============================================
' TEST 30: Function returning struct with Person (multiple types)
' ============================================
Print "TEST 30: Function returning struct with mixed types"

Dim p30 As Person
p30 = MakePerson("Bob", 35, 1.75)

If p30.name = "Bob" And p30.age = 35 And Abs(p30.height - 1.75) < 0.01 Then
  Print "  PASS: Function returning struct with mixed types"
Else
  Print "  FAIL: Expected Bob,35,1.75 got"; p30.name; ","; p30.age; ","; p30.height
EndIf

' ============================================
' TEST 31: Struct packing - string followed by int/float (non-8 aligned)
' ============================================
Print "TEST 31: Struct packing with short string before int/float"

Type PackTest
  label As STRING LENGTH 5
  value As INTEGER
  ratio As FLOAT
End Type

Dim pk As PackTest
pk.label = "Test"
pk.value = 12345
pk.ratio = 3.14

If pk.label = "Test" And pk.value = 12345 And Abs(pk.ratio - 3.14) < 0.01 Then
  Print "  PASS: Struct packing with short string"
Else
  Print "  FAIL: Got label="; pk.label; " value="; pk.value; " ratio="; pk.ratio
EndIf

' ============================================
' TEST 32: Function returning struct with array member
' ============================================
Print "TEST 32: Function returning struct with array member"

Type WithArray
  id As INTEGER
  values(3) As INTEGER
End Type

Dim wa As WithArray
wa = MakeWithArray(99, 10, 20, 30, 40)

If wa.id = 99 And wa.values(0) = 10 And wa.values(1) = 20 And wa.values(2) = 30 And wa.values(3) = 40 Then
  Print "  PASS: Function returning struct with array member"
Else
  Print "  FAIL: Got id="; wa.id; " values="; wa.values(0); ","; wa.values(1); ","; wa.values(2); ","; wa.values(3)
EndIf

' ============================================
' TEST 33: STRUCT CLEAR - single struct
' ============================================
Print "TEST 33: STRUCT CLEAR - single struct"

Dim clearTest As Person
clearTest.age = 42
clearTest.height = 1.85
clearTest.name = "TestName"

Struct Clear clearTest

If clearTest.age = 0 And clearTest.height = 0 And clearTest.name = "" Then
  Print "  PASS: STRUCT CLEAR single struct"
Else
  Print "  FAIL: Got age="; clearTest.age; " height="; clearTest.height; " name="; clearTest.name
EndIf

' ============================================
' TEST 34: STRUCT CLEAR - array of structs
' ============================================
Print "TEST 34: STRUCT CLEAR - array of structs"

Dim clearArr(2) As Point
clearArr(0).x = 1 : clearArr(0).y = 2
clearArr(1).x = 3 : clearArr(1).y = 4
clearArr(2).x = 5 : clearArr(2).y = 6

Struct Clear clearArr()

ok% = 1
For i% = 0 To 2
  If clearArr(i%).x <> 0 Or clearArr(i%).y <> 0 Then ok% = 0
Next i%

If ok% Then
  Print "  PASS: STRUCT CLEAR array of structs"
Else
  Print "  FAIL: Array not properly cleared"
EndIf

' ============================================
' TEST 35: STRUCT SWAP
' ============================================
Print "TEST 35: STRUCT SWAP"

Dim swapA As Point, swapB As Point
swapA.x = 100 : swapA.y = 200
swapB.x = 300 : swapB.y = 400

Struct Swap swapA, swapB

If swapA.x = 300 And swapA.y = 400 And swapB.x = 100 And swapB.y = 200 Then
  Print "  PASS: STRUCT SWAP"
Else
  Print "  FAIL: Got A="; swapA.x; ","; swapA.y; " B="; swapB.x; ","; swapB.y
EndIf

' ============================================
' TEST 36: STRUCT SWAP with array elements
' ============================================
Print "TEST 36: STRUCT SWAP with array elements"

Dim swapArr(1) As Person
swapArr(0).name = "Alice" : swapArr(0).age = 25 : swapArr(0).height = 1.60
swapArr(1).name = "Bob" : swapArr(1).age = 30 : swapArr(1).height = 1.80

Struct Swap swapArr(0), swapArr(1)

If swapArr(0).name = "Bob" And swapArr(0).age = 30 And swapArr(1).name = "Alice" And swapArr(1).age = 25 Then
  Print "  PASS: STRUCT SWAP array elements"
Else
  Print "  FAIL: Swap array elements failed"
EndIf

' ============================================
' TEST 37: STRUCT(FIND) - find by integer member
' ============================================
Print "TEST 37: STRUCT(FIND) - find by integer"

Dim findArr(4) As Person
findArr(0).name = "Alice" : findArr(0).age = 25
findArr(1).name = "Bob" : findArr(1).age = 30
findArr(2).name = "Charlie" : findArr(2).age = 35
findArr(3).name = "Diana" : findArr(3).age = 40
findArr(4).name = "Eve" : findArr(4).age = 45

Dim foundIdx% = Struct(FIND findArr().age, 35)

If foundIdx% = 2 Then
  Print "  PASS: STRUCT(FIND) by integer found at index 2"
Else
  Print "  FAIL: Expected index 2, got"; foundIdx%
EndIf

' ============================================
' TEST 38: STRUCT(FIND) - find by string member
' ============================================
Print "TEST 38: STRUCT(FIND) - find by string"

foundIdx% = Struct(FIND findArr().name, "Diana")

If foundIdx% = 3 Then
  Print "  PASS: STRUCT(FIND) by string found at index 3"
Else
  Print "  FAIL: Expected index 3, got"; foundIdx%
EndIf

' ============================================
' TEST 39: STRUCT(FIND) - not found returns -1
' ============================================
Print "TEST 39: STRUCT(FIND) - not found"

foundIdx% = Struct(FIND findArr().name, "Zara")

If foundIdx% = -1 Then
  Print "  PASS: STRUCT(FIND) returns -1 when not found"
Else
  Print "  FAIL: Expected -1, got"; foundIdx%
EndIf

' ============================================
' TEST 40: STRUCT(FIND) - find by float member
' ============================================
Print "TEST 40: STRUCT(FIND) - find by float"

findArr(2).height = 1.77
foundIdx% = Struct(FIND findArr().height, 1.77)

If foundIdx% = 2 Then
  Print "  PASS: STRUCT(FIND) by float"
Else
  Print "  FAIL: Expected index 2, got"; foundIdx%
EndIf

' ============================================
' TEST 42: STRUCT(FIND) with start parameter - iterate through duplicates
' ============================================
Print "TEST 42: STRUCT(FIND) with start parameter"

' Add duplicate ages
findArr(0).age = 30  ' Alice now 30
findArr(1).age = 30  ' Bob already 30
findArr(4).age = 30  ' Eve now 30

' Find first person with age 30
foundIdx% = Struct(FIND findArr().age, 30)
If foundIdx% <> 0 Then
  Print "  FAIL: First match should be index 0, got"; foundIdx%
Else
  ' Find next person with age 30 starting after first match
  foundIdx% = Struct(FIND findArr().age, 30, foundIdx% + 1)
  If foundIdx% <> 1 Then
    Print "  FAIL: Second match should be index 1, got"; foundIdx%
  Else
    ' Find next person with age 30 starting after second match
    foundIdx% = Struct(FIND findArr().age, 30, foundIdx% + 1)
    If foundIdx% <> 4 Then
      Print "  FAIL: Third match should be index 4, got"; foundIdx%
    Else
      ' Try to find another - should return -1
      foundIdx% = Struct(FIND findArr().age, 30, foundIdx% + 1)
      If foundIdx% = -1 Then
        Print "  PASS: STRUCT(FIND) with start parameter iterates correctly"
      Else
        Print "  FAIL: Should return -1 after last match, got"; foundIdx%
      EndIf
    EndIf
  EndIf
EndIf

' Restore original ages for remaining tests
findArr(0).age = 25 : findArr(1).age = 30 : findArr(4).age = 45

' ============================================
' TEST 42A: STRUCT(FIND) with regex - basic pattern match
' ============================================
Print "TEST 42A: STRUCT(FIND) with regex - basic pattern"

' Set up test names for regex searching
findArr(0).name = "Alice Smith"
findArr(1).name = "Bob Jones"
findArr(2).name = "Charlie Brown"
findArr(3).name = "Diana Prince"
findArr(4).name = "Eve Wilson"

Dim matchLen%

' Find name starting with "Bob" (note: empty start parameter to trigger regex mode)
foundIdx% = Struct(FIND findArr().name, "^Bob", , matchLen%)
If foundIdx% = 1 And matchLen% = 3 Then
  Print "  PASS: Found 'Bob' at index 1, match length ="; matchLen%
Else
  Print "  FAIL: Expected index 1 with length 3, got"; foundIdx%; "length"; matchLen%
EndIf

' ============================================
' TEST 42B: STRUCT(FIND) with regex - pattern with wildcards
' ============================================
Print "TEST 42B: STRUCT(FIND) with regex - wildcards"

' Find name containing "own" anywhere
foundIdx% = Struct(FIND findArr().name, "own", , matchLen%)
If foundIdx% = 2 And matchLen% = 3 Then
  Print "  PASS: Found 'own' in Charlie Brown at index 2"
Else
  Print "  FAIL: Expected index 2, got"; foundIdx%
EndIf

' ============================================
' TEST 42C: STRUCT(FIND) with regex - character class
' ============================================
Print "TEST 42C: STRUCT(FIND) with regex - character class"

' Find name with pattern [A-D].* (starting with A-D)
foundIdx% = Struct(FIND findArr().name, "^[A-D]", , matchLen%)
If foundIdx% = 0 Then
  Print "  PASS: Found name starting with A-D at index 0 (Alice)"
Else
  Print "  FAIL: Expected index 0, got"; foundIdx%
EndIf

' ============================================
' TEST 42D: STRUCT(FIND) with regex - start parameter
' ============================================
Print "TEST 42D: STRUCT(FIND) with regex - with start parameter"

' Find name starting with A-D, starting from index 2
foundIdx% = Struct(FIND findArr().name, "^[A-D]", 2, matchLen%)
If foundIdx% = 2 Then
  Print "  PASS: Found name starting with A-D at index 2 (Charlie)"
Else
  Print "  FAIL: Expected index 2, got"; foundIdx%
EndIf

' Find next match starting from index 3
foundIdx% = Struct(FIND findArr().name, "^[A-D]", 3, matchLen%)
If foundIdx% = 3 Then
  Print "  PASS: Found name starting with A-D at index 3 (Diana)"
Else
  Print "  FAIL: Expected index 3, got"; foundIdx%
EndIf

' ============================================
' TEST 42E: STRUCT(FIND) with regex - no match
' ============================================
Print "TEST 42E: STRUCT(FIND) with regex - no match"

' Search for pattern that doesn't exist
matchLen% = 99  ' Set to non-zero to verify it gets set to 0
foundIdx% = Struct(FIND findArr().name, "^Zara", , matchLen%)
If foundIdx% = -1 And matchLen% = 0 Then
  Print "  PASS: No match returns -1, length = 0"
Else
  Print "  FAIL: Expected -1 with length 0, got"; foundIdx%; "length"; matchLen%
EndIf

' ============================================
' TEST 42F: STRUCT(FIND) with regex - digit pattern
' ============================================
Print "TEST 42F: STRUCT(FIND) with regex - digit pattern"

' Add names with numbers
findArr(0).name = "Agent007"
findArr(1).name = "User123"
findArr(2).name = "Test456"

' Find name containing digits
foundIdx% = Struct(FIND findArr().name, "\d+", , matchLen%)
If foundIdx% = 0 And matchLen% = 3 Then
  Print "  PASS: Found digits at index 0, match length ="; matchLen%
Else
  Print "  FAIL: Expected index 0 with length 3, got"; foundIdx%; "length"; matchLen%
EndIf

' Restore names for other tests
findArr(0).name = "Alice" : findArr(1).name = "Bob" : findArr(2).name = "Charlie"
findArr(3).name = "Diana" : findArr(4).name = "Eve"

' ============================================
' TEST 41: STRUCT SAVE and LOAD
' ============================================
Print "TEST 41: STRUCT SAVE and LOAD"

' Create test data
Dim saveArr(2) As Person
saveArr(0).name = "Alice" : saveArr(0).age = 25 : saveArr(0).height = 1.65
saveArr(1).name = "Bob" : saveArr(1).age = 30 : saveArr(1).height = 1.75
saveArr(2).name = "Charlie" : saveArr(2).age = 35 : saveArr(2).height = 1.85

' Save to file
Open "structtest.dat" For Output As #1
Struct Save #1, saveArr()
Close #1

' Clear the array
Struct Clear saveArr()

' Verify cleared
ok% = 1
For i% = 0 To 2
  If saveArr(i%).name <> "" Or saveArr(i%).age <> 0 Then ok% = 0
Next i%

If ok% = 0 Then
  Print "  FAIL: Array not cleared before load test"
Else
  ' Load back from file
  Open "structtest.dat" For Input As #1
  Struct Load #1, saveArr()
  Close #1
  
  ' Verify data - check each condition separately to avoid line continuation issues
  Dim loadOk% = 1
  If saveArr(0).name <> "Alice" Or saveArr(0).age <> 25 Then loadOk% = 0
  If Abs(saveArr(0).height - 1.65) >= 0.01 Then loadOk% = 0
  If saveArr(1).name <> "Bob" Or saveArr(1).age <> 30 Then loadOk% = 0
  If Abs(saveArr(1).height - 1.75) >= 0.01 Then loadOk% = 0
  If saveArr(2).name <> "Charlie" Or saveArr(2).age <> 35 Then loadOk% = 0
  If Abs(saveArr(2).height - 1.85) >= 0.01 Then loadOk% = 0
  
  If loadOk% Then
    Print "  PASS: STRUCT SAVE/LOAD"
  Else
    Print "  FAIL: Data mismatch after load"
    Print "    [0] "; saveArr(0).name; ","; saveArr(0).age; ","; saveArr(0).height
    Print "    [1] "; saveArr(1).name; ","; saveArr(1).age; ","; saveArr(1).height
    Print "    [2] "; saveArr(2).name; ","; saveArr(2).age; ","; saveArr(2).height
  EndIf
EndIf

' Clean up test file
Kill "structtest.dat"

' ============================================
' TEST 43: STRUCT PRINT - single struct
' ============================================
Print "TEST 43: STRUCT PRINT - single struct"
Print "  Output should show Person structure with Alice, 25, 1.65:"

Dim printTest As Person
printTest.name = "Alice"
printTest.age = 25
printTest.height = 1.65

Struct Print printTest
Print "  PASS: (verify output above manually)"

' ============================================
' TEST 44: STRUCT PRINT - array element
' ============================================
Print "TEST 44: STRUCT PRINT - array element"
Print "  Output should show Point structure with x=100, y=200:"

Dim printArr(2) As Point
printArr(0).x = 100 : printArr(0).y = 200
printArr(1).x = 300 : printArr(1).y = 400

Struct Print printArr(0)
Print "  PASS: (verify output above manually)"

' ============================================
' TEST 45: STRUCT PRINT - whole array
' ============================================
Print "TEST 45: STRUCT PRINT - whole array"
Print "  Output should show 3 Point elements (indexes 0..2):"

Struct Print printArr()
Print "  PASS: (verify output above manually)"

' ============================================
' TEST 46: BOUND() - basic 1D struct array
' ============================================
Print "TEST 46: BOUND() - basic 1D struct array"

Dim boundArr1(9) As Point
Dim bound1% = Bound(boundArr1())
If bound1% = 9 Then
  Print "  PASS: BOUND() returned "; bound1%
Else
  Print "  FAIL: Expected 9, got "; bound1%
EndIf

' ============================================
' TEST 47: BOUND() - 2D struct array with dimension
' ============================================
Print "TEST 47: BOUND() - 2D struct array with dimension"

Dim boundArr2(4, 7) As Point
Dim dim1% = Bound(boundArr2(), 1)
Dim dim2% = Bound(boundArr2(), 2)
If dim1% = 4 And dim2% = 7 Then
  Print "  PASS: Dimension 1 = "; dim1%; ", Dimension 2 = "; dim2%
Else
  Print "  FAIL: Expected (4,7), got ("; dim1%; ","; dim2%; ")"
EndIf

' ============================================
' TEST 48: BOUND() - default dimension for struct array
' ============================================
Print "TEST 48: BOUND() - default dimension for struct array"

Dim dim1b% = Bound(boundArr2())
If dim1b% = 4 Then
  Print "  PASS: Default dimension returned "; dim1b%
Else
  Print "  FAIL: Expected 4, got "; dim1b%
EndIf

' ============================================
' TEST 49: STRUCT COPY array() TO array()
' ============================================
Print "TEST 49: STRUCT COPY array() TO array()"

Dim srcCopy(2) As Point
srcCopy(0).x = 10 : srcCopy(0).y = 11
srcCopy(1).x = 20 : srcCopy(1).y = 21
srcCopy(2).x = 30 : srcCopy(2).y = 31

Dim dstCopy(4) As Point  ' Larger destination
Struct Copy srcCopy() To dstCopy()

Dim copyOk% = 1
If dstCopy(0).x <> 10 Or dstCopy(0).y <> 11 Then copyOk% = 0
If dstCopy(1).x <> 20 Or dstCopy(1).y <> 21 Then copyOk% = 0
If dstCopy(2).x <> 30 Or dstCopy(2).y <> 31 Then copyOk% = 0

If copyOk% Then
  Print "  PASS: Array copy successful"
Else
  Print "  FAIL: Array copy data mismatch"
  Print "    [0] "; dstCopy(0).x; ","; dstCopy(0).y
  Print "    [1] "; dstCopy(1).x; ","; dstCopy(1).y
  Print "    [2] "; dstCopy(2).x; ","; dstCopy(2).y
EndIf

' ============================================
' TEST 50: STRUCT COPY array preserves extra elements
' ============================================
Print "TEST 50: STRUCT COPY array preserves extra dest elements"

' Set destination element 3 and 4 before copy
Dim srcCopy2(1) As Point
srcCopy2(0).x = 100 : srcCopy2(0).y = 101
srcCopy2(1).x = 200 : srcCopy2(1).y = 201

Dim dstCopy2(3) As Point
dstCopy2(2).x = 999 : dstCopy2(2).y = 998
dstCopy2(3).x = 888 : dstCopy2(3).y = 887

Struct Copy srcCopy2() To dstCopy2()

Dim copy2Ok% = 1
If dstCopy2(0).x <> 100 Or dstCopy2(0).y <> 101 Then copy2Ok% = 0
If dstCopy2(1).x <> 200 Or dstCopy2(1).y <> 201 Then copy2Ok% = 0
If dstCopy2(2).x <> 999 Or dstCopy2(2).y <> 998 Then copy2Ok% = 0
If dstCopy2(3).x <> 888 Or dstCopy2(3).y <> 887 Then copy2Ok% = 0

If copy2Ok% Then
  Print "  PASS: Extra elements preserved"
Else
  Print "  FAIL: Extra elements overwritten"
  Print "    [2] "; dstCopy2(2).x; ","; dstCopy2(2).y; " (expected 999,998)"
  Print "    [3] "; dstCopy2(3).x; ","; dstCopy2(3).y; " (expected 888,887)"
EndIf

' ============================================
' TEST 51: Nested structure - basic access
' ============================================
Print "TEST 51: Nested structure - basic access"

' Define inner struct (Point already defined above)
' Define outer struct with nested Point
Type Line
  startPt As Point
  endPt As Point
  thickness As INTEGER
End Type

Dim testLine As Line
testLine.startPt.x = 10
testLine.startPt.y = 20
testLine.endPt.x = 100
testLine.endPt.y = 200
testLine.thickness = 3

Dim nestedOk% = 1
If testLine.startPt.x <> 10 Or testLine.startPt.y <> 20 Then nestedOk% = 0
If testLine.endPt.x <> 100 Or testLine.endPt.y <> 200 Then nestedOk% = 0
If testLine.thickness <> 3 Then nestedOk% = 0

If nestedOk% Then
  Print "  PASS: Nested structure access works"
Else
  Print "  FAIL: Nested structure access failed"
  Print "    startPt: "; testLine.startPt.x; ","; testLine.startPt.y
  Print "    endPt: "; testLine.endPt.x; ","; testLine.endPt.y
  Print "    thickness: "; testLine.thickness
EndIf

' ============================================
' TEST 52: Nested structure in array
' ============================================
Print "TEST 52: Nested structure in array of structs"

Dim lines(2) As Line
lines(0).startPt.x = 1
lines(0).startPt.y = 2
lines(0).endPt.x = 3
lines(0).endPt.y = 4
lines(1).startPt.x = 10
lines(1).startPt.y = 20
lines(1).endPt.x = 30
lines(1).endPt.y = 40

Dim arrNestedOk% = 1
If lines(0).startPt.x <> 1 Or lines(0).endPt.y <> 4 Then arrNestedOk% = 0
If lines(1).startPt.x <> 10 Or lines(1).endPt.y <> 40 Then arrNestedOk% = 0

If arrNestedOk% Then
  Print "  PASS: Array of structs with nested access"
Else
  Print "  FAIL: Array nested access failed"
EndIf

' ============================================
' TEST 53: Array of nested structure members (Phase 2)
' ============================================
Print "TEST 53: Array of nested structure members"

Type DataPoint
  values(4) As FLOAT
End Type

Type DataSet
  points(2) As DataPoint
  name As STRING
End Type

Dim mydata As DataSet
mydata.name = "Test"
mydata.points(0).values(0) = 1.1
mydata.points(0).values(2) = 1.3
mydata.points(1).values(1) = 2.2
mydata.points(2).values(4) = 3.5

Dim phase2Ok% = 1
If mydata.points(0).values(0) <> 1.1 Then phase2Ok% = 0
If mydata.points(0).values(2) <> 1.3 Then phase2Ok% = 0
If mydata.points(1).values(1) <> 2.2 Then phase2Ok% = 0
If mydata.points(2).values(4) <> 3.5 Then phase2Ok% = 0

If phase2Ok% Then
  Print "  PASS: Array of nested structure members"
Else
  Print "  FAIL: Array of nested structure members failed"
EndIf

' ============================================
' TEST 54: Arrays of structs with array of nested members
' ============================================
Print "TEST 54: Full nesting: arr(i).member(j).member(k)"

Dim mydatasets(1) As DataSet
mydatasets(0).name = "Set0"
mydatasets(0).points(1).values(3) = 42.5
mydatasets(1).name = "Set1"
mydatasets(1).points(2).values(0) = 99.9

Dim fullNestOk% = 1
If mydatasets(0).points(1).values(3) <> 42.5 Then fullNestOk% = 0
If mydatasets(1).points(2).values(0) <> 99.9 Then fullNestOk% = 0
If mydatasets(0).name <> "Set0" Then fullNestOk% = 0
If mydatasets(1).name <> "Set1" Then fullNestOk% = 0

If fullNestOk% Then
  Print "  PASS: Full nesting array(i).array(j).array(k)"
Else
  Print "  FAIL: Full nesting failed"
EndIf

' ============================================
' TEST 55: Variables with dots in names (legacy MMBasic feature)
' ============================================
Print "TEST 55: Variables with dots in names (non-struct)"

Dim My.Variable% = 42
Dim Another.Dotted.Name! = 3.14159
Dim String.With.Dots$ = "Hello"

Dim dotVarOk% = 1
If My.Variable% <> 42 Then dotVarOk% = 0
If Another.Dotted.Name! <> 3.14159 Then dotVarOk% = 0
If String.With.Dots$ <> "Hello" Then dotVarOk% = 0

' Modify and check
My.Variable% = 100
Another.Dotted.Name! = 2.718
String.With.Dots$ = "World"

If My.Variable% <> 100 Then dotVarOk% = 0
If Another.Dotted.Name! <> 2.718 Then dotVarOk% = 0
If String.With.Dots$ <> "World" Then dotVarOk% = 0

If dotVarOk% Then
  Print "  PASS: Dotted variable names work correctly"
Else
  Print "  FAIL: Dotted variable names broken"
EndIf

' ============================================
' TEST 56: Subroutines with dots in names
' ============================================
Print "TEST 56: Subroutines with dots in names"

Dim subTestResult% = 0
My.Test.Sub 10, 20
If subTestResult% = 30 Then
  Print "  PASS: Dotted subroutine names work"
Else
  Print "  FAIL: Dotted subroutine call failed"
EndIf

' ============================================
' TEST 57: Functions with dots in names
' ============================================
Print "TEST 57: Functions with dots in names"

Dim funResult% = My.Test.Function%(5, 7)
If funResult% = 35 Then
  Print "  PASS: Dotted function names work"
Else
  Print "  FAIL: Dotted function call failed, got"; funResult%
EndIf

' ============================================
' TEST 58: Mix of dotted variables and struct members
' ============================================
Print "TEST 58: Dotted variables alongside struct members"

Dim coord.offset% = 50
Dim pt As Point
pt.x = 10
pt.y = 20

Dim mixOk% = 1
' Use dotted variable and struct in same expression
Dim result% = pt.x + coord.offset%
If result% <> 60 Then mixOk% = 0

' Assign from one to other
pt.x = coord.offset%
If pt.x <> 50 Then mixOk% = 0

coord.offset% = pt.y
If coord.offset% <> 20 Then mixOk% = 0

If mixOk% Then
  Print "  PASS: Mixed dotted vars and struct members"
Else
  Print "  FAIL: Mixed usage broken"
EndIf

' ============================================
' TEST 59: Arrays with dots in names
' ============================================
Print "TEST 59: Arrays with dots in names"

Dim Data.Array%(5)
Data.Array%(0) = 100
Data.Array%(3) = 300
Data.Array%(5) = 500

Dim arrDotOk% = 1
If Data.Array%(0) <> 100 Then arrDotOk% = 0
If Data.Array%(3) <> 300 Then arrDotOk% = 0
If Data.Array%(5) <> 500 Then arrDotOk% = 0

If arrDotOk% Then
  Print "  PASS: Dotted array names work"
Else
  Print "  FAIL: Dotted array names broken"
EndIf

' ============================================
' TEST 60: STRUCT(SIZEOF) - basic types
' ============================================
Print "TEST 60: STRUCT(SIZEOF) - basic types"

' Point has 2 x INTEGER (8 bytes each) = 16 bytes
Dim sizePoint% = Struct(SIZEOF "Point")
If sizePoint% = 16 Then
  Print "  PASS: SIZEOF Point = "; sizePoint%; " bytes"
Else
  Print "  FAIL: Expected 16, got "; sizePoint%
EndIf

' ============================================
' TEST 61: STRUCT(SIZEOF) - with string
' ============================================
Print "TEST 61: STRUCT(SIZEOF) - with string"

' Person has: age(8) + height(8) + name(256) = 272 bytes
Dim sizePerson% = Struct(SIZEOF "Person")
If sizePerson% = 272 Then
  Print "  PASS: SIZEOF Person = "; sizePerson%; " bytes"
Else
  Print "  FAIL: Expected 272, got "; sizePerson%
EndIf

' ============================================
' TEST 62: STRUCT(SIZEOF) - case insensitive
' ============================================
Print "TEST 62: STRUCT(SIZEOF) - case insensitive"

Dim sizeLower% = Struct(SIZEOF "point")
Dim sizeUpper% = Struct(SIZEOF "POINT")
Dim sizeMixed% = Struct(SIZEOF "PoInT")

If sizeLower% = 16 And sizeUpper% = 16 And sizeMixed% = 16 Then
  Print "  PASS: SIZEOF case insensitive"
Else
  Print "  FAIL: Case sensitivity issue"
EndIf

' ============================================
' TEST 63: STRUCT(SIZEOF) - with variable typename
' ============================================
Print "TEST 63: STRUCT(SIZEOF) - with variable typename"

Dim typename$ = "Point"
Dim sizeVar% = Struct(SIZEOF typename$)
If sizeVar% = 16 Then
  Print "  PASS: SIZEOF with variable typename"
Else
  Print "  FAIL: Expected 16, got "; sizeVar%
EndIf

' ============================================
' TEST 63a: STRUCT(OFFSET) - basic member offset
' ============================================
Print "TEST 63a: STRUCT(OFFSET) - basic member offset"

' Point has x at offset 0, y at offset 8
Dim offsetX% = Struct(OFFSET "Point", "x")
Dim offsetY% = Struct(OFFSET "Point", "y")
If offsetX% = 0 And offsetY% = 8 Then
  Print "  PASS: Point.x offset="; offsetX%; ", Point.y offset="; offsetY%
Else
  Print "  FAIL: Expected (0,8), got ("; offsetX%; ","; offsetY%; ")"
EndIf

' ============================================
' TEST 63b: STRUCT(OFFSET) - mixed type structure
' ============================================
Print "TEST 63b: STRUCT(OFFSET) - mixed type structure"

' Person has: age at 0, height at 8, name at 16
Dim offsetAge% = Struct(OFFSET "Person", "age")
Dim offsetHeight% = Struct(OFFSET "Person", "height")
Dim offsetName% = Struct(OFFSET "Person", "name")
If offsetAge% = 0 And offsetHeight% = 8 And offsetName% = 16 Then
  Print "  PASS: Person offsets correct"
Else
  Print "  FAIL: Person offsets wrong"
EndIf

' ============================================
' TEST 63c: STRUCT(OFFSET) - case insensitive
' ============================================
Print "TEST 63c: STRUCT(OFFSET) - case insensitive"

Dim offsetLower% = Struct(OFFSET "point", "x")
Dim offsetUpper% = Struct(OFFSET "POINT", "X")
Dim offsetMixed% = Struct(OFFSET "PoInT", "Y")
If offsetLower% = 0 And offsetUpper% = 0 And offsetMixed% = 8 Then
  Print "  PASS: OFFSET case insensitive"
Else
  Print "  FAIL: Case sensitivity issue"
EndIf

' ============================================
' TEST 63d: STRUCT(OFFSET) - with variable typename
' ============================================
Print "TEST 63d: STRUCT(OFFSET) - with variable names"

Dim typeName2$ = "Point"
Dim memberName$ = "y"
Dim offsetVar% = Struct(OFFSET typeName2$, memberName$)
If offsetVar% = 8 Then
  Print "  PASS: OFFSET with variable names"
Else
  Print "  FAIL: Expected 8, got "; offsetVar%
EndIf

' ============================================
' TEST 64: STRUCT SAVE/LOAD individual element
' ============================================
Print "TEST 64: STRUCT SAVE/LOAD individual element"

Dim elemArr(4) As Point
elemArr(0).x = 10 : elemArr(0).y = 11
elemArr(1).x = 20 : elemArr(1).y = 21
elemArr(2).x = 30 : elemArr(2).y = 31
elemArr(3).x = 40 : elemArr(3).y = 41
elemArr(4).x = 50 : elemArr(4).y = 51

' Save only element 2
Open "elem_test.dat" For Output As #1
Struct Save #1, elemArr(2)
Close #1

' Load into a different element
elemArr(4).x = 0 : elemArr(4).y = 0
Open "elem_test.dat" For Input As #1
Struct Load #1, elemArr(4)
Close #1

If elemArr(4).x = 30 And elemArr(4).y = 31 Then
  Print "  PASS: Save/Load individual element"
Else
  Print "  FAIL: Expected 30,31 got "; elemArr(4).x; ","; elemArr(4).y
EndIf

Kill "elem_test.dat"

' ============================================
' TEST 65: STRUCT SAVE/LOAD multiple individual elements
' ============================================
Print "TEST 65: STRUCT SAVE/LOAD multiple elements sequentially"

' Save elements 0, 2, 4 to file
Open "multi_elem.dat" For Output As #1
Struct Save #1, elemArr(0)
Struct Save #1, elemArr(2)
Struct Save #1, elemArr(4)
Close #1

' Create new array and load back
Dim loadArr(2) As Point
Open "multi_elem.dat" For Input As #1
Struct Load #1, loadArr(0)
Struct Load #1, loadArr(1)
Struct Load #1, loadArr(2)
Close #1

Dim multiOk% = 1
If loadArr(0).x <> 10 Or loadArr(0).y <> 11 Then multiOk% = 0
If loadArr(1).x <> 30 Or loadArr(1).y <> 31 Then multiOk% = 0
If loadArr(2).x <> 30 Or loadArr(2).y <> 31 Then multiOk% = 0

If multiOk% Then
  Print "  PASS: Multiple sequential element save/load"
Else
  Print "  FAIL: Data mismatch"
  Print "    [0] "; loadArr(0).x; ","; loadArr(0).y
  Print "    [1] "; loadArr(1).x; ","; loadArr(1).y
  Print "    [2] "; loadArr(2).x; ","; loadArr(2).y
EndIf

Kill "multi_elem.dat"

' ============================================
' TEST 66: STRUCT SAVE/LOAD single struct to array element
' ============================================
Print "TEST 66: STRUCT SAVE single struct, LOAD to array element"

Dim singlePt As Point
singlePt.x = 999
singlePt.y = 888

Open "single_to_arr.dat" For Output As #1
Struct Save #1, singlePt
Close #1

Dim targetArr(3) As Point
Open "single_to_arr.dat" For Input As #1
Struct Load #1, targetArr(2)
Close #1

If targetArr(2).x = 999 And targetArr(2).y = 888 Then
  Print "  PASS: Single struct to array element"
Else
  Print "  FAIL: Expected 999,888 got "; targetArr(2).x; ","; targetArr(2).y
EndIf

Kill "single_to_arr.dat"

' ============================================
' TEST 67: STRUCT SAVE array element, LOAD to single struct
' ============================================
Print "TEST 67: STRUCT SAVE array element, LOAD to single struct"

Open "arr_to_single.dat" For Output As #1
Struct Save #1, elemArr(1)
Close #1

Dim loadSingle As Point
Open "arr_to_single.dat" For Input As #1
Struct Load #1, loadSingle
Close #1

If loadSingle.x = 20 And loadSingle.y = 21 Then
  Print "  PASS: Array element to single struct"
Else
  Print "  FAIL: Expected 20,21 got "; loadSingle.x; ","; loadSingle.y
EndIf

Kill "arr_to_single.dat"

' ============================================
' TEST 68: STRUCT(SIZEOF) for memory calculation
' ============================================
Print "TEST 68: STRUCT(SIZEOF) for memory calculation"

' Calculate expected file size for array
Dim arrSize% = 5  ' 0 to 4 = 5 elements
Dim expectedBytes% = (arrSize%) * Struct(SIZEOF "Point")

' Save array and check file size
Open "size_test.dat" For Output As #1
Struct Save #1, elemArr()
Close #1

' Verify we can read it back correctly (confirms size is right)
Dim verifyArr(4) As Point
Open "size_test.dat" For Input As #1
Struct Load #1, verifyArr()
Close #1

Dim sizeOk% = 1
For i% = 0 To 4
  If verifyArr(i%).x <> elemArr(i%).x Or verifyArr(i%).y <> elemArr(i%).y Then
    sizeOk% = 0
  EndIf
Next i%

If sizeOk% Then
  Print "  PASS: Array of "; arrSize%; " elements = "; expectedBytes%; " bytes"
Else
  Print "  FAIL: Array save/load size mismatch"
EndIf

Kill "size_test.dat"

' ============================================
' TEST 69: Direct struct assignment (single variables)
' ============================================
Print "TEST 69: Direct struct assignment (single variables)"

Dim assignSrc As Point
Dim assignDst As Point
assignSrc.x = 111
assignSrc.y = 222

assignDst = assignSrc

If assignDst.x = 111 And assignDst.y = 222 Then
  Print "  PASS: Direct struct assignment"
Else
  Print "  FAIL: Expected 111,222 got "; assignDst.x; ","; assignDst.y
EndIf

' ============================================
' TEST 70: Direct struct assignment (array elements)
' ============================================
Print "TEST 70: Direct struct assignment (array elements)"

Dim assignArr(5) As Point
assignArr(0).x = 10 : assignArr(0).y = 20
assignArr(1).x = 30 : assignArr(1).y = 40

assignArr(3) = assignArr(0)
assignArr(4) = assignArr(1)

Dim arrAssignOk% = 1
If assignArr(3).x <> 10 Or assignArr(3).y <> 20 Then arrAssignOk% = 0
If assignArr(4).x <> 30 Or assignArr(4).y <> 40 Then arrAssignOk% = 0

If arrAssignOk% Then
  Print "  PASS: Direct array element assignment"
Else
  Print "  FAIL: Array element assignment failed"
EndIf

' ============================================
' TEST 71: Direct assignment between different arrays
' ============================================
Print "TEST 71: Direct assignment between different arrays"

Dim srcArr2(3) As Point
Dim dstArr2(3) As Point
srcArr2(1).x = 500 : srcArr2(1).y = 600

dstArr2(2) = srcArr2(1)

If dstArr2(2).x = 500 And dstArr2(2).y = 600 Then
  Print "  PASS: Cross-array element assignment"
Else
  Print "  FAIL: Expected 500,600 got "; dstArr2(2).x; ","; dstArr2(2).y
EndIf

' ============================================
' TEST 72: Direct assignment with mixed types struct
' ============================================
Print "TEST 72: Direct assignment with mixed types struct"

Dim persSrc As Person
Dim persDst As Person
persSrc.name = "TestPerson"
persSrc.age = 42
persSrc.height = 1.85

persDst = persSrc

Dim persOk% = 1
If persDst.name <> "TestPerson" Then persOk% = 0
If persDst.age <> 42 Then persOk% = 0
If Abs(persDst.height - 1.85) >= 0.01 Then persOk% = 0

If persOk% Then
  Print "  PASS: Mixed type struct assignment"
Else
  Print "  FAIL: Mixed type struct assignment failed"
EndIf

' ============================================
' TEST 73: Direct assignment from function return
' ============================================
Print "TEST 73: Direct assignment from function return"

Dim funcResult As Point
funcResult = MakePoint(777, 888)

If funcResult.x = 777 And funcResult.y = 888 Then
  Print "  PASS: Assignment from function return"
Else
  Print "  FAIL: Expected 777,888 got "; funcResult.x; ","; funcResult.y
EndIf

' ============================================
' TEST 74: Chained assignment (a = b, then c = a)
' ============================================
Print "TEST 74: Chained assignment"

Dim chainA As Point, chainB As Point, chainC As Point
chainA.x = 1000 : chainA.y = 2000
chainB = chainA
chainC = chainB

If chainC.x = 1000 And chainC.y = 2000 Then
  Print "  PASS: Chained struct assignment"
Else
  Print "  FAIL: Expected 1000,2000 got "; chainC.x; ","; chainC.y
EndIf

' ============================================
' TEST 74A: Direct assignment from nested struct array member
' ============================================
Print "TEST 74A: Direct assignment from nested struct array member"

Type KeyLinkCopy
  key As INTEGER
  filename As STRING LENGTH 15
  position As INTEGER
End Type

Type TreeCopy
  keyLinks(20) As KeyLinkCopy
End Type

Dim treeNode74 As TreeCopy
Dim keyNode74 As KeyLinkCopy

For i% = 0 To 20
  treeNode74.keyLinks(i%).key = i%
  treeNode74.keyLinks(i%).filename = "FILE" + Str$(i%)
  treeNode74.keyLinks(i%).position = i% * 100
Next i%

keyNode74 = treeNode74.keyLinks(15)

Dim expected74A$ = "FILE" + Str$(15)

If keyNode74.key = 15 And keyNode74.filename = expected74A$ And keyNode74.position = 1500 Then
  Print "  PASS: Nested struct array element assignment"
Else
  Print "  FAIL: Expected key=15 filename="; expected74A$; " position=1500"
  Print "    Got key="; keyNode74.key; " filename="; keyNode74.filename; " position="; keyNode74.position
EndIf

' ============================================
' TEST 74B: Direct assignment from nested struct array member (variable index)
' ============================================
Print "TEST 74B: Direct assignment from nested struct array member (variable index)"

Dim idx74B% = 17
keyNode74 = treeNode74.keyLinks(idx74B%)

Dim expected74B$ = "FILE" + Str$(idx74B%)

If keyNode74.key = 17 And keyNode74.filename = expected74B$ And keyNode74.position = 1700 Then
  Print "  PASS: Variable index nested struct array assignment"
Else
  Print "  FAIL: Expected key=17 filename="; expected74B$; " position=1700"
  Print "    Got key="; keyNode74.key; " filename="; keyNode74.filename; " position="; keyNode74.position
EndIf

' ============================================
' TEST 75: Random access file with struct array (write and read in reverse)
' ============================================
Print "TEST 75: Random access file with struct array"

' Define a struct type with integer and string for this test
Type FileRecord
  id As INTEGER
  label As STRING
End Type

' Create a struct array with test data
Dim fileArr75(4) As FileRecord
For i% = 0 To 4
  fileArr75(i%).id = (i% + 1) * 100
  fileArr75(i%).label = "Record" + Str$((i% + 1) * 100)
Next i%

' Write each struct element to file sequentially
Open "structtest.dat" For Output As #1
For i% = 0 To 4
  Struct Save #1, fileArr75(i%)
Next i%
Close #1

' Calculate struct size for seeking (INTEGER=8 bytes, STRING=256 bytes = 264 bytes per record)
Dim structSize75% = 264

' Read back in reverse order using SEEK
Dim readArr75(4) As FileRecord
Open "structtest.dat" For Random As #1
For i% = 4 To 0 Step -1
  ' Seek to byte position (1-based, so first record at position 1)
  Seek #1, i% * structSize75% + 1
  Struct Load #1, readArr75(4 - i%)
Next i%
Close #1

' Verify the data was read correctly in reverse
ok% = 1
Dim expectedId%, expectedLabel$, labelNum%
For i% = 0 To 4
  ' readArr75(0) should have fileArr75(4) data, readArr75(1) should have fileArr75(3), etc.
  expectedId% = (5 - i%) * 100
  expectedLabel$ = "Record" + Str$(expectedId%)
  If readArr75(i%).id <> expectedId% Then
    Print "    Index"; i%; ": Expected id"; expectedId%; " got"; readArr75(i%).id
    ok% = 0
  EndIf
  If readArr75(i%).label <> expectedLabel$ Then
    Print "    Index"; i%; ": Expected label '"; expectedLabel$; "' got '"; readArr75(i%).label; "'"
    ok% = 0
  EndIf
  ' Also verify using VAL() that the embedded number is correct
  labelNum% = Val(Mid$(readArr75(i%).label, 7))
  If labelNum% <> expectedId% Then
    Print "    Index"; i%; ": VAL extraction failed, expected"; expectedId%; " got"; labelNum%
    ok% = 0
  EndIf
Next i%

If ok% Then
  Print "  PASS: Random access file write and reverse read with strings"
Else
  Print "  FAIL: Random access file reverse read failed"
EndIf

' Clean up the test file
Kill "structtest.dat"

' ============================================
' TEST 76: BOX command using structure array members
' ============================================
Print "TEST 76: BOX command with structure array members"

' Define a Box type with x, y, w, h
Type BoxRect
  x As INTEGER
  y As INTEGER
  w As INTEGER
  h As INTEGER
End Type

' Create an array of box structures
Dim boxes(2) As BoxRect

' Initialize three different boxes
boxes(0).x = 10  : boxes(0).y = 10  : boxes(0).w = 100 : boxes(0).h = 50
boxes(1).x = 120 : boxes(1).y = 20  : boxes(1).w = 80  : boxes(1).h = 60
boxes(2).x = 50  : boxes(2).y = 80  : boxes(2).w = 150 : boxes(2).h = 40

' Draw three boxes using structure array members
For i% = 0 To 2
  Box boxes(i%).x, boxes(i%).y, boxes(i%).w, boxes(i%).h
Next i%

' Verify structure values are still intact after use in command
ok% = 1
If boxes(0).x <> 10 Or boxes(0).y <> 10 Or boxes(0).w <> 100 Or boxes(0).h <> 50 Then ok% = 0
If boxes(1).x <> 120 Or boxes(1).y <> 20 Or boxes(1).w <> 80 Or boxes(1).h <> 60 Then ok% = 0
If boxes(2).x <> 50 Or boxes(2).y <> 80 Or boxes(2).w <> 150 Or boxes(2).h <> 40 Then ok% = 0

If ok% Then
  Print "  PASS: BOX command with structure array members"
Else
  Print "  FAIL: Structure values corrupted after BOX command"
EndIf

' ============================================
' TEST 77: STRUCT EXTRACT - float member
' ============================================
Print "TEST 77: STRUCT EXTRACT - float member"

Type RoomData
  temperature As FLOAT
  humidity As FLOAT
End Type

Dim rooms(4) As RoomData
Dim temps!(4)

' Populate the structure array
rooms(0).temperature = 20.5 : rooms(0).humidity = 45.0
rooms(1).temperature = 21.3 : rooms(1).humidity = 50.2
rooms(2).temperature = 19.8 : rooms(2).humidity = 55.5
rooms(3).temperature = 22.1 : rooms(3).humidity = 48.3
rooms(4).temperature = 18.9 : rooms(4).humidity = 60.0

' Extract temperature values
Struct Extract rooms().temperature, temps!()

ok% = 1
If Abs(temps!(0) - 20.5) > 0.001 Then ok% = 0
If Abs(temps!(1) - 21.3) > 0.001 Then ok% = 0
If Abs(temps!(2) - 19.8) > 0.001 Then ok% = 0
If Abs(temps!(3) - 22.1) > 0.001 Then ok% = 0
If Abs(temps!(4) - 18.9) > 0.001 Then ok% = 0

If ok% Then
  Print "  PASS: STRUCT EXTRACT - float member"
Else
  Print "  FAIL: STRUCT EXTRACT float values incorrect"
EndIf

' ============================================
' TEST 78: STRUCT EXTRACT - integer member
' ============================================
Print "TEST 78: STRUCT EXTRACT - integer member"

Type DataSample
  timestamp As INTEGER
  value As INTEGER
End Type

Dim samples(3) As DataSample
Dim values%(3)

samples(0).timestamp = 1000 : samples(0).value = 100
samples(1).timestamp = 2000 : samples(1).value = 200
samples(2).timestamp = 3000 : samples(2).value = 300
samples(3).timestamp = 4000 : samples(3).value = 400

Struct Extract samples().value, values%()

ok% = 1
If values%(0) <> 100 Then ok% = 0
If values%(1) <> 200 Then ok% = 0
If values%(2) <> 300 Then ok% = 0
If values%(3) <> 400 Then ok% = 0

If ok% Then
  Print "  PASS: STRUCT EXTRACT - integer member"
Else
  Print "  FAIL: STRUCT EXTRACT integer values incorrect"
EndIf

' ============================================
' TEST 79: STRUCT EXTRACT - string member
' ============================================
Print "TEST 79: STRUCT EXTRACT - string member"

Type NamedItem79
  id As INTEGER
  name As STRING LENGTH 15
End Type

Dim items79(2) As NamedItem79
Dim names79$(2) LENGTH 15

items79(0).id = 1 : items79(0).name = "Apple"
items79(1).id = 2 : items79(1).name = "Banana"
items79(2).id = 3 : items79(2).name = "Cherry"

Struct Extract items79().name, names79$()

ok% = 1
If names79$(0) <> "Apple" Then ok% = 0
If names79$(1) <> "Banana" Then ok% = 0
If names79$(2) <> "Cherry" Then ok% = 0

If ok% Then
  Print "  PASS: STRUCT EXTRACT - string member"
Else
  Print "  FAIL: STRUCT EXTRACT string values incorrect"
EndIf

' ============================================
' TEST 80: STRUCT EXTRACT - second member (non-zero offset)
' ============================================
Print "TEST 80: STRUCT EXTRACT - second member (non-zero offset)"

' Extract humidity instead of temperature to test non-zero offset
Dim humidity!(4)

Struct Extract rooms().humidity, humidity!()

ok% = 1
If Abs(humidity!(0) - 45.0) > 0.001 Then ok% = 0
If Abs(humidity!(1) - 50.2) > 0.001 Then ok% = 0
If Abs(humidity!(2) - 55.5) > 0.001 Then ok% = 0
If Abs(humidity!(3) - 48.3) > 0.001 Then ok% = 0
If Abs(humidity!(4) - 60.0) > 0.001 Then ok% = 0

If ok% Then
  Print "  PASS: STRUCT EXTRACT - second member (non-zero offset)"
Else
  Print "  FAIL: STRUCT EXTRACT second member values incorrect"
EndIf

' ============================================
' TEST 81: STRUCT INSERT - float member
' ============================================
Print "TEST 81: STRUCT INSERT - float member"

' Modify the temps array and insert back
temps!(0) = 25.0
temps!(1) = 26.0
temps!(2) = 27.0
temps!(3) = 28.0
temps!(4) = 29.0

Struct Insert temps!(), rooms().temperature

ok% = 1
If Abs(rooms(0).temperature - 25.0) > 0.001 Then ok% = 0
If Abs(rooms(1).temperature - 26.0) > 0.001 Then ok% = 0
If Abs(rooms(2).temperature - 27.0) > 0.001 Then ok% = 0
If Abs(rooms(3).temperature - 28.0) > 0.001 Then ok% = 0
If Abs(rooms(4).temperature - 29.0) > 0.001 Then ok% = 0

If ok% Then
  Print "  PASS: STRUCT INSERT - float member"
Else
  Print "  FAIL: STRUCT INSERT float values incorrect"
EndIf

' ============================================
' TEST 82: STRUCT INSERT - integer member
' ============================================
Print "TEST 82: STRUCT INSERT - integer member"

' Modify the values array and insert back
values%(0) = 1000
values%(1) = 2000
values%(2) = 3000
values%(3) = 4000

Struct Insert values%(), samples().value

ok% = 1
If samples(0).value <> 1000 Then ok% = 0
If samples(1).value <> 2000 Then ok% = 0
If samples(2).value <> 3000 Then ok% = 0
If samples(3).value <> 4000 Then ok% = 0

If ok% Then
  Print "  PASS: STRUCT INSERT - integer member"
Else
  Print "  FAIL: STRUCT INSERT integer values incorrect"
EndIf

' ============================================
' TEST 83: STRUCT INSERT - string member
' ============================================
Print "TEST 83: STRUCT INSERT - string member"

names79$(0) = "Xylophone"
names79$(1) = "Yacht"
names79$(2) = "Zebra"

Struct Insert names79$(), items79().name

ok% = 1
If items79(0).name <> "Xylophone" Then ok% = 0
If items79(1).name <> "Yacht" Then ok% = 0
If items79(2).name <> "Zebra" Then ok% = 0

If ok% Then
  Print "  PASS: STRUCT INSERT - string member"
Else
  Print "  FAIL: STRUCT INSERT string values incorrect"
EndIf

' ============================================
' TEST 84: STRUCT INSERT preserves other members
' ============================================
Print "TEST 84: STRUCT INSERT preserves other members"

' Verify humidity values were not affected by temperature insert
ok% = 1
If Abs(rooms(0).humidity - 45.0) > 0.001 Then ok% = 0
If Abs(rooms(1).humidity - 50.2) > 0.001 Then ok% = 0
If Abs(rooms(2).humidity - 55.5) > 0.001 Then ok% = 0
If Abs(rooms(3).humidity - 48.3) > 0.001 Then ok% = 0
If Abs(rooms(4).humidity - 60.0) > 0.001 Then ok% = 0

If ok% Then
  Print "  PASS: STRUCT INSERT preserves other members"
Else
  Print "  FAIL: Other members were corrupted"
EndIf

' ============================================
' TEST 85: STRUCT(TYPE) - basic member types
' ============================================
Print "TEST 85: STRUCT(TYPE) - basic member types"

' Type constants
Const T_NBR = 1
Const T_STR = 2
Const T_INT = 4

' Test with Person type (has INTEGER, FLOAT, STRING)
Dim typeAge% = Struct(TYPE "Person", "age")
Dim typeHeight% = Struct(TYPE "Person", "height")
Dim typeNameMbr% = Struct(TYPE "Person", "name")

ok% = 1
If typeAge% <> T_INT Then ok% = 0
If typeHeight% <> T_NBR Then ok% = 0
If typeNameMbr% <> T_STR Then ok% = 0

If ok% Then
  Print "  PASS: STRUCT(TYPE) returns correct base types"
Else
  Print "  FAIL: Expected age=4, height=1, name=2"
  Print "    Got age="; typeAge%; " height="; typeHeight%; " name="; typeNameMbr%
EndIf

' ============================================
' TEST 85b: STRUCT(TYPE) - Point type (all INTEGER)
' ============================================
Print "TEST 85b: STRUCT(TYPE) - Point type"

Dim typeX% = Struct(TYPE "Point", "x")
Dim typeY% = Struct(TYPE "Point", "y")

If typeX% = T_INT And typeY% = T_INT Then
  Print "  PASS: STRUCT(TYPE) for Point members"
Else
  Print "  FAIL: Expected both 4, got x="; typeX%; " y="; typeY%
EndIf

' ============================================
' TEST 85c: STRUCT(TYPE) - case insensitive
' ============================================
Print "TEST 85c: STRUCT(TYPE) - case insensitive"

Dim typeLower% = Struct(TYPE "person", "age")
Dim typeUpper% = Struct(TYPE "PERSON", "AGE")
Dim typeMixed% = Struct(TYPE "PeRsOn", "AgE")

If typeLower% = T_INT And typeUpper% = T_INT And typeMixed% = T_INT Then
  Print "  PASS: STRUCT(TYPE) case insensitive"
Else
  Print "  FAIL: Case sensitivity issue"
EndIf

' ============================================
' TEST 85d: STRUCT(TYPE) - with variable names
' ============================================
Print "TEST 85d: STRUCT(TYPE) - with variable names"

Dim typeName3$ = "Person"
Dim memberName3$ = "height"
Dim typeVar% = Struct(TYPE typeName3$, memberName3$)

If typeVar% = T_NBR Then
  Print "  PASS: STRUCT(TYPE) with variable names"
Else
  Print "  FAIL: Expected 1, got "; typeVar%
EndIf

' ============================================
' Summary
' ============================================
Print
Print "=== All Tests Complete ==="
Print "Review output above for any FAIL messages"

End

' ============================================
' Function definitions (must be after END)
' ============================================

Function MakePoint(xval%, yval%) As Point
  MakePoint.x = xval%
  MakePoint.y = yval%
End Function

Function MakePerson(n$, a%, h!) As Person
  MakePerson.age = a%
  MakePerson.height = h!
  MakePerson.name = n$
End Function

Function MakeWithArray(id%, v0%, v1%, v2%, v3%) As WithArray
  MakeWithArray.id = id%
  MakeWithArray.values(0) = v0%
  MakeWithArray.values(1) = v1%
  MakeWithArray.values(2) = v2%
  MakeWithArray.values(3) = v3%
End Function

' Subroutine with dots in name (Test 56)
Sub My.Test.Sub(a%, b%)
  subTestResult% = a% + b%
End Sub

' Function with dots in name (Test 57)
Function My.Test.Function%(x%, y%)
  My.Test.Function% = x% * y%
End Function
