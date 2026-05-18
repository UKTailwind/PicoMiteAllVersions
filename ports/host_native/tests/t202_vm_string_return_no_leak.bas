' User-defined FUNCTION that returns a string must not leak its local
' buffer across calls. Regression for: op_store_local_s lazily allocs
' STRINGSIZE bytes the first time a slot holds a string, and
' op_leave_frame used to SKIP freeing the slot when the stack still
' referenced it (i.e. the return value). The caller then consumed the
' return value, leaving the heap buffer orphaned. Tight FUNCTION loops
' burned through the 128 KB VM heap in a few hundred iterations.
'
' The fix lives in op_leave_frame: copy any local string still
' referenced by the stack into the VM's str_temp ring, rewrite the
' stack reference, then free the local buffer. This test calls
' rnd_chr$ enough times that the old leak would have blown the heap.
function rnd_chr$()
  rnd_chr$ = Chr$(Int(Rnd() * 26 + 65))
end function

Dim i%, last$
For i% = 1 To 2000
  last$ = rnd_chr$()
Next i%

' Fresh call to confirm the function still works after the loop.
Print Len(rnd_chr$())   ' expect 1
Print "no leak"
