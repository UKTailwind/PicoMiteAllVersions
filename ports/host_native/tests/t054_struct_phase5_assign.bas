' Phase 5 struct direct-assignment coverage (acceptance tests 69, 70, 71, 72,
' 74, 74A, 74B).  Test 73 — assign from function return — is Phase 6.

Type Point
  x As INTEGER
  y As INTEGER
End Type

Type Person
  name As STRING LENGTH 20
  age As INTEGER
  height As FLOAT
End Type

' --- Test 69: scalar = scalar ---
Dim assignSrc As Point, assignDst As Point
assignSrc.x = 111 : assignSrc.y = 222
assignDst = assignSrc
Print assignDst.x; ","; assignDst.y

' --- Test 70: array element = array element (same array) ---
Dim assignArr(5) As Point
assignArr(0).x = 10 : assignArr(0).y = 20
assignArr(1).x = 30 : assignArr(1).y = 40
assignArr(3) = assignArr(0)
assignArr(4) = assignArr(1)
Print assignArr(3).x; ","; assignArr(3).y
Print assignArr(4).x; ","; assignArr(4).y

' --- Test 71: cross-array element assignment ---
Dim srcArr(3) As Point, dstArr(3) As Point
srcArr(1).x = 500 : srcArr(1).y = 600
dstArr(2) = srcArr(1)
Print dstArr(2).x; ","; dstArr(2).y

' --- Test 72: mixed-types whole-struct assign ---
Dim persSrc As Person, persDst As Person
persSrc.name = "TestPerson"
persSrc.age = 42
persSrc.height = 1.85
persDst = persSrc
Print persDst.name; "/"; persDst.age; "/"; persDst.height

' --- Test 74: chained assignment a = b then c = a ---
Dim chainA As Point, chainB As Point, chainC As Point
chainA.x = 1000 : chainA.y = 2000
chainB = chainA
chainC = chainB
Print chainC.x; ","; chainC.y

' --- Test 74A / 74B: scalar = nested struct array member, const + variable index ---
Type KeyLink
  key As INTEGER
  filename As STRING LENGTH 15
  position As INTEGER
End Type
Type Tree
  keyLinks(20) As KeyLink
End Type

Dim treeNode As Tree
Dim keyNode As KeyLink
Dim i% As INTEGER
For i% = 0 To 20
  treeNode.keyLinks(i%).key = i%
  treeNode.keyLinks(i%).filename = "FILE" + Str$(i%)
  treeNode.keyLinks(i%).position = i% * 100
Next i%

keyNode = treeNode.keyLinks(15)
Print keyNode.key; ","; keyNode.filename; ","; keyNode.position

Dim idx% As INTEGER
idx% = 17
keyNode = treeNode.keyLinks(idx%)
Print keyNode.key; ","; keyNode.filename; ","; keyNode.position
