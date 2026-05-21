' ports/pc386/demos/smoke_disk.bas — comprehensive on-device disk smoke
' for the pc386 ATA-PIO + FatFs path. Prints OK_<name> / FAIL_<name>
' tags that ports/pc386/tests/hw_smoke.py greps for over serial.
'
' Run from BASIC:   RUN "SMOKE.BAS"
' Run from harness: python3 ports/pc386/tests/hw_smoke.py smoke_disk_run

CHDIR "C:/"

' ---------- P1: small write+readback ----------------------------------
OPEN "C:/sk_a.txt" FOR OUTPUT AS #1
PRINT #1, "alpha"
PRINT #1, "beta"
CLOSE #1
OPEN "C:/sk_a.txt" FOR INPUT AS #1
LINE INPUT #1, a$
LINE INPUT #1, b$
CLOSE #1
IF a$ = "alpha" AND b$ = "beta" THEN PRINT "OK_small" ELSE PRINT "FAIL_small a=" + a$ + " b=" + b$

' ---------- P2: multi-sector write+readback ---------------------------
OPEN "C:/sk_big.txt" FOR OUTPUT AS #1
FOR i% = 1 TO 200
  PRINT #1, "line " + STR$(i%)
NEXT i%
CLOSE #1
OPEN "C:/sk_big.txt" FOR INPUT AS #1
ok% = 1
FOR i% = 1 TO 200
  LINE INPUT #1, c$
  IF c$ <> "line " + STR$(i%) THEN ok% = 0
NEXT i%
IF EOF(#1) = 0 THEN ok% = 0
CLOSE #1
IF ok% THEN PRINT "OK_multi" ELSE PRINT "FAIL_multi"

' ---------- P3: append -------------------------------------------------
OPEN "C:/sk_app.txt" FOR OUTPUT AS #1
PRINT #1, "first"
CLOSE #1
OPEN "C:/sk_app.txt" FOR APPEND AS #1
PRINT #1, "second"
CLOSE #1
OPEN "C:/sk_app.txt" FOR INPUT AS #1
LINE INPUT #1, a$
LINE INPUT #1, b$
CLOSE #1
IF a$ = "first" AND b$ = "second" THEN PRINT "OK_append" ELSE PRINT "FAIL_append"

' ---------- P4: multi-handle concurrent --------------------------------
OPEN "C:/sk_h1.txt" FOR OUTPUT AS #1
OPEN "C:/sk_h2.txt" FOR OUTPUT AS #2
PRINT #1, "h1"
PRINT #2, "h2"
CLOSE #1 : CLOSE #2
OPEN "C:/sk_h1.txt" FOR INPUT AS #1
OPEN "C:/sk_h2.txt" FOR INPUT AS #2
LINE INPUT #1, a$
LINE INPUT #2, b$
CLOSE #1 : CLOSE #2
IF a$ = "h1" AND b$ = "h2" THEN PRINT "OK_multihandle" ELSE PRINT "FAIL_multihandle"

' ---------- P5: RENAME -------------------------------------------------
OPEN "C:/sk_r1.txt" FOR OUTPUT AS #1
PRINT #1, "renamed"
CLOSE #1
RENAME "C:/sk_r1.txt" AS "C:/sk_r2.txt"
OPEN "C:/sk_r2.txt" FOR INPUT AS #1
LINE INPUT #1, a$
CLOSE #1
IF a$ = "renamed" THEN PRINT "OK_rename" ELSE PRINT "FAIL_rename"

' ---------- P6: COPY ---------------------------------------------------
OPEN "C:/sk_c1.txt" FOR OUTPUT AS #1
PRINT #1, "copied"
CLOSE #1
COPY "C:/sk_c1.txt" TO "C:/sk_c2.txt"
OPEN "C:/sk_c2.txt" FOR INPUT AS #1
LINE INPUT #1, a$
CLOSE #1
IF a$ = "copied" THEN PRINT "OK_copy" ELSE PRINT "FAIL_copy"

' ---------- P7: MKDIR / subdir / RMDIR ---------------------------------
MKDIR "C:/sk_dir"
OPEN "C:/sk_dir/inside.txt" FOR OUTPUT AS #1
PRINT #1, "in_subdir"
CLOSE #1
OPEN "C:/sk_dir/inside.txt" FOR INPUT AS #1
LINE INPUT #1, a$
CLOSE #1
KILL "C:/sk_dir/inside.txt"
RMDIR "C:/sk_dir"
IF a$ = "in_subdir" THEN PRINT "OK_subdir" ELSE PRINT "FAIL_subdir"

' ---------- P8: SEEK ---------------------------------------------------
OPEN "C:/sk_seek.bin" FOR OUTPUT AS #1
FOR i% = 0 TO 99
  PRINT #1, "X" + STR$(i%)
NEXT i%
CLOSE #1
OPEN "C:/sk_seek.bin" FOR INPUT AS #1
SEEK #1, 1
LINE INPUT #1, a$
CLOSE #1
KILL "C:/sk_seek.bin"
IF a$ = "X0" THEN PRINT "OK_seek_begin" ELSE PRINT "FAIL_seek_begin a=" + a$

' ---------- P9: KILL leftovers + verify --------------------------------
KILL "C:/sk_a.txt"
KILL "C:/sk_big.txt"
KILL "C:/sk_app.txt"
KILL "C:/sk_h1.txt"
KILL "C:/sk_h2.txt"
KILL "C:/sk_r2.txt"
KILL "C:/sk_c1.txt"
KILL "C:/sk_c2.txt"
PRINT "OK_kill"

' ---------- P10: SAVE IMAGE (the ATA CACHE_FLUSH regression test) ------
MODE 1
CLS RGB(YELLOW)
SAVE IMAGE "C:/sk_img.bmp", 0, 0, 320, 200
KILL "C:/sk_img.bmp"
PRINT "OK_save_image"

' ---------- P11: binary pattern + checksum -----------------------------
p$ = ""
FOR i% = 0 TO 199
  p$ = p$ + CHR$((i% * 17 + 31) AND 255)
NEXT i%
OPEN "C:/sk_bin.dat" FOR OUTPUT AS #1
FOR i% = 1 TO 20 : PRINT #1, p$; : NEXT i%
CLOSE #1
OPEN "C:/sk_bin.dat" FOR INPUT AS #1
lof% = LOF(1)
first$ = INPUT$(200, #1)
CLOSE #1
IF lof% = 4000 AND first$ = p$ THEN PRINT "OK_binary_head" ELSE PRINT "FAIL_binary_head lof=" + STR$(lof%)
OPEN "C:/sk_bin.dat" FOR INPUT AS #1
SEEK #1, 2001
mid$ = INPUT$(200, #1)
CLOSE #1
IF mid$ = p$ THEN PRINT "OK_binary_mid" ELSE PRINT "FAIL_binary_mid"
KILL "C:/sk_bin.dat"

' ---------- P12: large file (100KB) ------------------------------------
OPEN "C:/sk_large.dat" FOR OUTPUT AS #1
FOR i% = 1 TO 500 : PRINT #1, p$; : NEXT i%
CLOSE #1
OPEN "C:/sk_large.dat" FOR INPUT AS #1
lof% = LOF(1)
SEEK #1, 1
a$ = INPUT$(200, #1)
SEEK #1, 50001
m$ = INPUT$(200, #1)
SEEK #1, 99801
z$ = INPUT$(200, #1)
CLOSE #1
KILL "C:/sk_large.dat"
IF lof% = 100000 AND a$ = p$ AND m$ = p$ AND z$ = p$ THEN PRINT "OK_large" ELSE PRINT "FAIL_large lof=" + STR$(lof%)

' ---------- P13: stress loop (20 open/write/read/kill cycles) ----------
ok% = 1
FOR i% = 1 TO 20
  OPEN "C:/sk_s.tmp" FOR OUTPUT AS #1
  PRINT #1, "iter" + STR$(i%)
  CLOSE #1
  OPEN "C:/sk_s.tmp" FOR INPUT AS #1
  LINE INPUT #1, x$
  CLOSE #1
  KILL "C:/sk_s.tmp"
  IF x$ <> "iter" + STR$(i%) THEN ok% = 0
NEXT i%
IF ok% THEN PRINT "OK_stress" ELSE PRINT "FAIL_stress"

PRINT "SMOKE_DONE"
END
