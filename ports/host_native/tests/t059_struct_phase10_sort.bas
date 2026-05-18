' Phase 10: STRUCT SORT
' Covers acceptance tests 23, 24, 25, 26, 27, 28.
' flags: bit0=reverse, bit1=case-insensitive, bit2=empty-at-end.

Type Point
  x As INTEGER
  y As INTEGER
End Type

Type NamedItem
  id As INTEGER
  name As STRING
End Type

Type Measurement
  value As FLOAT
  label As STRING
End Type

' --- Test 23: SORT by integer field (ascending) ---
Dim sa(4) As Point
sa(0).x = 50 : sa(0).y = 5
sa(1).x = 20 : sa(1).y = 2
sa(2).x = 40 : sa(2).y = 4
sa(3).x = 10 : sa(3).y = 1
sa(4).x = 30 : sa(4).y = 3
Struct Sort sa().x
Print "int:"; sa(0).x; ",";sa(1).x; ",";sa(2).x; ",";sa(3).x; ",";sa(4).x

' y values follow x's sort (companion check)
Print "int_y:"; sa(0).y; ",";sa(1).y; ",";sa(2).y; ",";sa(3).y; ",";sa(4).y

' --- Test 24: SORT reverse ---
Struct Sort sa().x, 1
Print "rev:"; sa(0).x; ",";sa(1).x; ",";sa(2).x; ",";sa(3).x; ",";sa(4).x

' --- Test 25: SORT by string field ---
Dim items(4) As NamedItem
items(0).id = 1 : items(0).name = "Cherry"
items(1).id = 2 : items(1).name = "Apple"
items(2).id = 3 : items(2).name = "Elderberry"
items(3).id = 4 : items(3).name = "Banana"
items(4).id = 5 : items(4).name = "Date"
Struct Sort items().name
Print "str:"; items(0).name; ",";items(1).name; ",";items(2).name; ",";items(3).name; ",";items(4).name
Print "str_id:"; items(0).id; ",";items(1).id; ",";items(2).id; ",";items(3).id; ",";items(4).id

' --- Test 26: SORT case-insensitive ---
items(0).name = "cherry"
items(1).name = "APPLE"
items(2).name = "Elderberry"
items(3).name = "banana"
items(4).name = "DATE"
Struct Sort items().name, 2
Print "ci:"; items(0).name; ",";items(1).name; ",";items(2).name; ",";items(3).name; ",";items(4).name

' --- Test 27: SORT by float ---
Dim ms(3) As Measurement
ms(0).value = 3.14 : ms(0).label = "pi"
ms(1).value = 1.41 : ms(1).label = "sqrt2"
ms(2).value = 2.71 : ms(2).label = "e"
ms(3).value = 0.57 : ms(3).label = "euler"
Struct Sort ms().value
Print "flt:"; ms(0).value; ",";ms(1).value; ",";ms(2).value; ",";ms(3).value

' --- Test 28: SORT empty strings at end ---
items(0).name = "Zebra"
items(1).name = ""
items(2).name = "Apple"
items(3).name = ""
items(4).name = "Mango"
Struct Sort items().name, 4
Print "end0:";"["+items(0).name+"]"
Print "end3:";"["+items(3).name+"]"
Print "end4:";"["+items(4).name+"]"
