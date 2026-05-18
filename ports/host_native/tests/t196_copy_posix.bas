' RUN_ARGS: --interp --sd-root=/tmp/mmbasic-t196
' COPY via POSIX routing. FileIO.c's B2B/B2A/A2B/A2A helpers call raw
' f_read/f_write on the host pseudo-FIL, which fails validate() silently
' and produces 0-byte copies. t152 hides this by running on vm_host_fat.
OPEN "src.txt" FOR OUTPUT AS #1
PRINT #1, "hello world"
PRINT #1, "line two"
CLOSE #1

OPEN "src.txt" FOR INPUT AS #1
Dim srcSz% = LOF(#1)
CLOSE #1
If srcSz% = 0 Then Error "setup: source file is empty"

COPY "src.txt" TO "dst.txt"

OPEN "dst.txt" FOR INPUT AS #1
Dim dstSz% = LOF(#1)
Dim first$
LINE INPUT #1, first$
CLOSE #1

If dstSz% = 0 Then Error "COPY produced a 0-byte file"
If dstSz% <> srcSz% Then Error "COPY size mismatch: src=" + Str$(srcSz%) + " dst=" + Str$(dstSz%)
If first$ <> "hello world" Then Error "COPY content mismatch: got '" + first$ + "'"

KILL "src.txt"
KILL "dst.txt"
Print "copy posix ok (" + Str$(dstSz%) + " bytes)"
