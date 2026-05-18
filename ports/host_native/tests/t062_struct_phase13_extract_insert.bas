' Phase 13: STRUCT EXTRACT / STRUCT INSERT — int and float members.
' Covers acceptance tests 77, 78, 80, 81, 82, 84 (string cases are in t063).

Type RoomData
  temperature As FLOAT
  humidity As FLOAT
End Type

Type DataSample
  timestamp As INTEGER
  value As INTEGER
End Type

Dim i%, ok%

' --- Test 77: EXTRACT float member ---
Dim rooms(4) As RoomData
Dim temps!(4)
rooms(0).temperature = 20.5 : rooms(0).humidity = 45.0
rooms(1).temperature = 21.3 : rooms(1).humidity = 50.2
rooms(2).temperature = 19.8 : rooms(2).humidity = 55.5
rooms(3).temperature = 22.1 : rooms(3).humidity = 48.3
rooms(4).temperature = 18.9 : rooms(4).humidity = 60.0
Struct Extract rooms().temperature, temps!()
Print "77:";temps!(0);",";temps!(1);",";temps!(2);",";temps!(3);",";temps!(4)

' --- Test 78: EXTRACT integer member ---
Dim samples(3) As DataSample
Dim vals%(3)
samples(0).timestamp = 1000 : samples(0).value = 100
samples(1).timestamp = 2000 : samples(1).value = 200
samples(2).timestamp = 3000 : samples(2).value = 300
samples(3).timestamp = 4000 : samples(3).value = 400
Struct Extract samples().value, vals%()
Print "78:";vals%(0);",";vals%(1);",";vals%(2);",";vals%(3)

' --- Test 80: EXTRACT second member (non-zero offset) ---
Dim humid!(4)
Struct Extract rooms().humidity, humid!()
Print "80:";humid!(0);",";humid!(1);",";humid!(2);",";humid!(3);",";humid!(4)

' --- Test 81: INSERT float member ---
temps!(0) = 25.0
temps!(1) = 26.0
temps!(2) = 27.0
temps!(3) = 28.0
temps!(4) = 29.0
Struct Insert temps!(), rooms().temperature
Print "81:";rooms(0).temperature;",";rooms(1).temperature;",";rooms(4).temperature

' --- Test 82: INSERT integer member ---
vals%(0) = 1000
vals%(1) = 2000
vals%(2) = 3000
vals%(3) = 4000
Struct Insert vals%(), samples().value
Print "82:";samples(0).value;",";samples(1).value;",";samples(3).value

' --- Test 84: INSERT preserves other members ---
Print "84:";rooms(0).humidity;",";rooms(4).humidity;"|";samples(0).timestamp;",";samples(3).timestamp
