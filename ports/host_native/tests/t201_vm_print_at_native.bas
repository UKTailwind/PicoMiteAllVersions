' VM-native `PRINT @(x,y)` — cursor-position form now compiles to a
' BC_SYS_PRINT_AT syscall instead of bridging to the interpreter.
'
' This matters because a bridged PRINT can't call user-defined SUB or
' FUNCTION (the interp's subfun[] is empty under FRUN), and matrix-
' style effects routinely do `PRINT @(x, y) rnd_chr$()` where rnd_chr$
' is user-defined. Keeping @ on the VM fast path lets OP_CALL_FUN
' dispatch the user function normally — same code path RUN uses.
'
' The third argument (PrintPixelMode) is accepted for source
' compatibility and silently discarded on the VM canvas path; the
' interpreter's fun_at uses it for inverse/underline video modes that
' aren't implemented on the host framebuffer.
function pick$(i)
  pick$ = Chr$(64 + i)
end function

PRINT @(0, 0) pick$(1)
PRINT @(8, 0) pick$(2)
PRINT @(16, 0) pick$(3)
PRINT @(24, 12) pick$(4)
PRINT @(32, 12, 0) pick$(5)

Print "print-at-native ok"
