' Phase 11: STRUCT SAVE / STRUCT LOAD.
' Covers acceptance tests 41, 64, 65, 66, 67, 75 (random-access via SEEK).

Type Point
  x As INTEGER
  y As INTEGER
End Type

Type Person
  name As STRING LENGTH 16
  age As INTEGER
  height As FLOAT
End Type

Dim i%, ok%

' --- Test 41: save whole array, clear, load back ---
Dim saveArr(2) As Person
saveArr(0).name = "Alice"   : saveArr(0).age = 25 : saveArr(0).height = 1.65
saveArr(1).name = "Bob"     : saveArr(1).age = 30 : saveArr(1).height = 1.75
saveArr(2).name = "Charlie" : saveArr(2).age = 35 : saveArr(2).height = 1.85

Open "ph11_all.dat" For Output As #1
Struct Save #1, saveArr()
Close #1

Struct Clear saveArr()

ok% = 1
For i% = 0 To 2
  If saveArr(i%).name <> "" Or saveArr(i%).age <> 0 Then ok% = 0
Next i%
Print "cleared:";ok%

Open "ph11_all.dat" For Input As #1
Struct Load #1, saveArr()
Close #1
Kill "ph11_all.dat"

Print "41a:";saveArr(0).name;",";saveArr(0).age;",";saveArr(0).height
Print "41b:";saveArr(1).name;",";saveArr(1).age;",";saveArr(1).height
Print "41c:";saveArr(2).name;",";saveArr(2).age;",";saveArr(2).height

' --- Test 64: save/load individual element ---
Dim ea(4) As Point
ea(0).x = 10 : ea(0).y = 11
ea(1).x = 20 : ea(1).y = 21
ea(2).x = 30 : ea(2).y = 31
ea(3).x = 40 : ea(3).y = 41
ea(4).x = 50 : ea(4).y = 51

Open "ph11_one.dat" For Output As #1
Struct Save #1, ea(2)
Close #1
ea(4).x = 0 : ea(4).y = 0
Open "ph11_one.dat" For Input As #1
Struct Load #1, ea(4)
Close #1
Kill "ph11_one.dat"
Print "64:";ea(4).x;",";ea(4).y

' --- Test 65: save three elements sequentially, load back ---
Open "ph11_multi.dat" For Output As #1
Struct Save #1, ea(0)
Struct Save #1, ea(2)
Struct Save #1, ea(4)
Close #1

Dim la(2) As Point
Open "ph11_multi.dat" For Input As #1
Struct Load #1, la(0)
Struct Load #1, la(1)
Struct Load #1, la(2)
Close #1
Kill "ph11_multi.dat"
Print "65:";la(0).x;",";la(0).y;"|";la(1).x;",";la(1).y;"|";la(2).x;",";la(2).y

' --- Test 66: save single, load into array element ---
Dim sp As Point
sp.x = 999 : sp.y = 888
Open "ph11_s2a.dat" For Output As #1
Struct Save #1, sp
Close #1

Dim ta(3) As Point
Open "ph11_s2a.dat" For Input As #1
Struct Load #1, ta(2)
Close #1
Kill "ph11_s2a.dat"
Print "66:";ta(2).x;",";ta(2).y

' --- Test 67: save array element, load into single ---
Open "ph11_a2s.dat" For Output As #1
Struct Save #1, ea(1)
Close #1

Dim ls As Point
Open "ph11_a2s.dat" For Input As #1
Struct Load #1, ls
Close #1
Kill "ph11_a2s.dat"
Print "67:";ls.x;",";ls.y

' --- Test 75: random-access write/read using SEEK ---
Type FileRecord
  id As INTEGER
  label As STRING
End Type

Dim fa(4) As FileRecord
For i% = 0 To 4
  fa(i%).id = (i% + 1) * 100
  fa(i%).label = "Record" + Str$((i% + 1) * 100)
Next i%

Open "ph11_rand.dat" For Output As #1
For i% = 0 To 4
  Struct Save #1, fa(i%)
Next i%
Close #1

' SIZEOF is Phase 16; for now hard-code: INTEGER(8) + STRING(256) = 264.
Dim ss% = 264

Dim ra(4) As FileRecord
Open "ph11_rand.dat" For Random As #1
For i% = 4 To 0 Step -1
  Seek #1, i% * ss% + 1
  Struct Load #1, ra(4 - i%)
Next i%
Close #1
Kill "ph11_rand.dat"

For i% = 0 To 4
  Print "75[";i%;"]:";ra(i%).id;",";ra(i%).label
Next i%
