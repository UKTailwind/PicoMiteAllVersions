' User-defined functions called inside bridged commands / functions.
'
' Regression: VM runs via bc_run_source_string_ex which never tokenises
' the source into ProgMemory, so PrepareProgram() never populates
' subfun[]. When the VM bridges a command like SORT back to the
' interpreter and the command's argument list contains a user function
' call (e.g. Flag%()), the interpreter's expression evaluator can't
' resolve the name via FindSubFun(), treats it as an array reference,
' and errors "Dimensions".
'
' Fix: populate ProgMemory + subfun[] before VM execution so bridged
' interpreter paths can dispatch user functions normally.

Function Flag%()
  Flag% = 0
End Function

Function Msg$(n%)
  Msg$ = "value=" + Str$(n%)
End Function

Function Double%(x%)
  Double% = x% * 2
End Function

' SORT with a user-function supplied reverse/indicator argument.
Dim a%(4)
a%(0) = 50
a%(1) = 30
a%(2) = 10
a%(3) = 40
a%(4) = 20
Sort a%(), , Flag%()
Print a%(0); a%(1); a%(2); a%(3); a%(4)

' Bridged function (BOUND) whose arg list uses a user function result
' as the dimension index. BOUND always bridges (OP_BRIDGE_FUN_I).
Dim b!(6, 8)
Print Bound(b!(), Double%(1))
Print Bound(b!(), Double%(0) + 1)

' User function in a bridged expression with a string literal.
Print Msg$(Double%(21))
