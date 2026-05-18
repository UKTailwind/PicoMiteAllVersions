' Const-inlined globals must stay visible to bridged interpreter calls.
'
' Regression for: FRUN of `Const W = 40 : Dim p(W)` errored with
' "Dimensions" after PRINT (and other statements) started always bridging
' to the interpreter. Root cause: source_compile_const stores literal
' values in cs->slots[i].const_ival and rolls back the emit — so
' vm->globals[i].i stays 0. bc_bridge.c's sync_vm_to_mmbasic then
' copied that 0 into g_vartbl and the bridged DIM / getint / etc. saw
' W = 0, tripping the "Dimensions" bounds check.
Const W% = 40
Const PI_CONST! = 3.14
Const NAME$ = "HELLO"

' PRINT always bridges — these reads must see the compile-time const
' values, not the zero-initialised vm->globals slot.
Print W%
Print PI_CONST!
Print NAME$

' DIM evaluates dimension expressions via the same sync path that PRINT
' uses when we add `As Integer` to a var inside the expression. Use the
' const directly as the dimension; VM compiles it inline as 40.
Dim arr(W%)
If Bound(arr(), 1) <> W% Then Error "Bound mismatch: expected " + Str$(W%) + " got " + Str$(Bound(arr(),1))

Print "const survives bridge ok"
