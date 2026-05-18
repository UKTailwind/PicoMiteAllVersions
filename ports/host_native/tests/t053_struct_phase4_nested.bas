' Phase 4 nested struct coverage mirroring acceptance tests 51-54
Option Explicit

Type Point
  x As INTEGER
  y As INTEGER
End Type

Type Line
  startPt As Point
  endPt As Point
  thickness As INTEGER
End Type

' -- Test 51 analog: scalar nested
Dim testLine As Line
testLine.startPt.x = 10
testLine.startPt.y = 20
testLine.endPt.x   = 100
testLine.endPt.y   = 200
testLine.thickness = 3

Print testLine.startPt.x; ","; testLine.startPt.y
Print testLine.endPt.x;   ","; testLine.endPt.y
Print testLine.thickness

' -- Test 52 analog: array-of-struct with nested scalar
Dim lines(2) As Line
lines(0).startPt.x = 1
lines(0).startPt.y = 2
lines(0).endPt.x   = 3
lines(0).endPt.y   = 4
lines(1).startPt.x = 10
lines(1).startPt.y = 20
lines(1).endPt.x   = 30
lines(1).endPt.y   = 40

Print lines(0).startPt.x; ","; lines(0).endPt.y
Print lines(1).startPt.x; ","; lines(1).endPt.y

' -- Test 53 analog: scalar struct with array-of-struct member, array-of-scalar leaf
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

Print mydata.points(0).values(0)
Print mydata.points(0).values(2)
Print mydata.points(1).values(1)
Print mydata.points(2).values(4)

' -- Test 54 analog: full nested arr(i).member(j).member(k)
Dim mydatasets(1) As DataSet
mydatasets(0).name = "Set0"
mydatasets(0).points(1).values(3) = 42.5
mydatasets(1).name = "Set1"
mydatasets(1).points(2).values(0) = 99.9

Print mydatasets(0).points(1).values(3)
Print mydatasets(1).points(2).values(0)
Print mydatasets(0).name
Print mydatasets(1).name
