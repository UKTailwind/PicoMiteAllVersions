' RUN_ARGS: --interp
' Phase 13 — string EXTRACT/INSERT (acceptance tests 79, 83).
'
' Interp-only because the VM stores `DIM s$(n)` as a BCValue array
' (pointer-per-element) while the interpreter uses a contiguous
' `(len+1)*N` buffer.  cmd_struct EXTRACT/INSERT is written against the
' contiguous layout, so bridging this case against a VM-native string
' array corrupts memory.  Tracked as a follow-up — unifying the two
' layouts or adding bridge-aware opcodes is a larger change than
' Phase 13's scope.  Int/float EXTRACT/INSERT run in compare mode
' (t062_struct_phase13_extract_insert.bas).

Type NamedItem
  id As INTEGER
  name As STRING
End Type

Dim items(2) As NamedItem
Dim names$(2)
items(0).id = 1 : items(0).name = "Apple"
items(1).id = 2 : items(1).name = "Banana"
items(2).id = 3 : items(2).name = "Cherry"

Struct Extract items().name, names$()
Print "79:";names$(0);",";names$(1);",";names$(2)

names$(0) = "Xylophone"
names$(1) = "Yacht"
names$(2) = "Zebra"
Struct Insert names$(), items().name
Print "83:";items(0).name;",";items(1).name;",";items(2).name
