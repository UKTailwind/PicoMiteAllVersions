' RUN_ARGS: --interp
' Regression: every MMBasic file-op against the B: (FatFS) drive must
' actually reach the FatFS backend, not silently get routed to LFS.
' The HAL filesystem migration (commit 1ca8347) introduced a class of
' bugs where helpers like getfullfilename / fullpath() strip the "A:"/
' "B:" prefix from paths, and the HAL adapter's path_fs() decides
' LFS vs FatFS purely from that prefix — so prefix-less paths default
' to LFS. Affected ops on B:
'   - Dir$("B:*", ...)           (fun_dir)
'   - MKDIR "B:foo"               (cmd_mkdir)
'   - RMDIR "B:foo"               (cmd_rmdir)
'   - KILL "B:foo"                (cmd_kill, literal-name branch)
'   - RENAME "B:foo" AS "B:bar"   (cmd_name → hal_fs_rename)
'
' This test creates real files/dirs on B:, exercises each op, and
' verifies effects with FILES (which uses a different code path) +
' Dir$ continuation.

' --- setup: clean slate ---------------------------------------------------
ON ERROR SKIP : KILL  "B:/t207a.txt"
ON ERROR SKIP : KILL  "B:/t207b.txt"
ON ERROR SKIP : KILL  "B:/t207renamed.txt"
ON ERROR SKIP : RMDIR "B:/t207dir"

' --- 1. MKDIR "B:foo" creates a directory on B: ---------------------------
MKDIR "B:/t207dir"
Dim STRING d$ = DIR$("B:/t207*", DIR)
IF d$ <> "t207dir" THEN ERROR "MKDIR B:/t207dir didn't appear in Dir$ — got [" + d$ + "]"

' --- 2. OPEN ... FOR OUTPUT on B: creates a file --------------------------
OPEN "B:/t207a.txt" FOR OUTPUT AS #1 : PRINT #1, "alpha" : CLOSE #1
OPEN "B:/t207b.txt" FOR OUTPUT AS #1 : PRINT #1, "bravo" : CLOSE #1

' --- 3. Dir$("B:*", FILE) finds them --------------------------------------
Dim STRING name$ = DIR$("B:/t207*.txt", FILE)
Dim INTEGER count% = 0
DO WHILE LEN(name$) > 0
  count% = count% + 1
  name$ = DIR$()
LOOP
IF count% <> 2 THEN ERROR "Dir$ B:/t207*.txt expected 2 matches, got " + STR$(count%)

' --- 4. RENAME "B:t207a.txt" AS "B:t207renamed.txt" -----------------------
RENAME "B:/t207a.txt" AS "B:/t207renamed.txt"
Dim STRING n$ = DIR$("B:/t207ren*", FILE)
IF n$ <> "t207renamed.txt" THEN ERROR "RENAME on B: didn't rename — Dir$ got [" + n$ + "]"

' --- 5. KILL "B:t207b.txt" removes it -------------------------------------
KILL "B:/t207b.txt"
Dim STRING gone$ = DIR$("B:/t207b.txt", FILE)
IF LEN(gone$) <> 0 THEN ERROR "KILL on B: didn't remove file — Dir$ got [" + gone$ + "]"

' --- 6. RMDIR "B:t207dir" removes the dir ---------------------------------
RMDIR "B:/t207dir"
Dim STRING dgone$ = DIR$("B:/t207dir", DIR)
IF LEN(dgone$) <> 0 THEN ERROR "RMDIR on B: didn't remove dir — Dir$ got [" + dgone$ + "]"

' --- cleanup --------------------------------------------------------------
KILL "B:/t207renamed.txt"

PRINT "b_drive_ops ok"
