' RUN_ARGS: --interp --sd-root=/tmp/mmbasic-t195
' SAVE IMAGE via POSIX routing (the path web host / --sim / --repl take).
' Regression test for: FileIO.c's SAVE IMAGE called raw f_write() on the
' host pseudo-FIL — f_write's validate() failed and the file stayed 0
' bytes. Default test mode uses vm_host_fat (a real FatFs volume) and
' hides the bug; --sd-root routes through host_fs_posix.
'
' VM doesn't compile SAVE IMAGE (bc_source.c skips it), so --interp only.
Const WIDTH% = 32
Const HEIGHT% = 32
CLS RGB(BLACK)
BOX 0, 0, WIDTH%, HEIGHT%, 1, RGB(RED), RGB(GREEN)

SAVE IMAGE "out.bmp", 0, 0, WIDTH%, HEIGHT%

OPEN "out.bmp" FOR INPUT AS #1
Dim sz% = LOF(#1)
CLOSE #1

' Uncompressed 24-bpp BMP of 32x32: 54 header + 3072 pixel = 3126 bytes.
If sz% = 0 Then Error "SAVE IMAGE wrote a 0-byte file"
If sz% < 100 Then Error "SAVE IMAGE truncated; size = " + Str$(sz%)

' Header check: first two bytes must be "BM".
OPEN "out.bmp" FOR INPUT AS #1
Dim hdr$ = Input$(2, #1)
CLOSE #1
If hdr$ <> "BM" Then Error "BMP magic missing, got " + Chr$(Asc(Left$(hdr$, 1))) + Chr$(Asc(Mid$(hdr$, 2, 1)))

KILL "out.bmp"
Print "save image posix ok (" + Str$(sz%) + " bytes)"
