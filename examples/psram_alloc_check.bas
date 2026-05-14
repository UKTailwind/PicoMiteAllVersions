' psram_alloc_check.bas
'
' Paste this into the REPL on a port that exposes PSRAM (e.g. a
' dvi_wifi_rp2350 Pico variant or the ESP32-S3 Metro). It allocates a
' >24 KB INTEGER array and checks that:
'
'   1. MM.INFO(PSRAM FREE) drops by at least the array byte count.
'   2. MM.INFO(HEAP) (SRAM heap free) drops only by an insignificant
'      bookkeeping amount.
'
' Both invariants must hold on RP2350 *and* ESP32 — they share the
' shared/cmd_psram.c + Memory.c PSRAM routing path, so behaviour from
' BASIC must be identical. Prints OK at the end if it passes.

OPTION EXPLICIT

' 4096 INTEGER cells = 32768 bytes, comfortably above the 24 KB threshold
' from the plan and bigger than half the smallest SRAM heap we ship
' (48 KB on ESP32), so Memory.c routes the allocation to PSRAM.
CONST N% = 4096
CONST ARRAY_BYTES% = N% * 8
CONST SRAM_TOLERANCE% = 8192

DIM INTEGER psram_before%, psram_after%, psram_delta%
DIM INTEGER sram_before%, sram_after%, sram_delta%

psram_before% = MM.INFO(PSRAM FREE)
sram_before% = MM.INFO(HEAP)
PRINT "PSRAM free before: " + STR$(psram_before%)
PRINT "SRAM  heap before: " + STR$(sram_before%)

DIM INTEGER big%(N%)

psram_after% = MM.INFO(PSRAM FREE)
sram_after% = MM.INFO(HEAP)
psram_delta% = psram_before% - psram_after%
sram_delta% = sram_before% - sram_after%
PRINT "PSRAM free after : " + STR$(psram_after%) + "  (delta " + STR$(psram_delta%) + ")"
PRINT "SRAM  heap after : " + STR$(sram_after%) + "  (delta " + STR$(sram_delta%) + ")"

IF psram_delta% < ARRAY_BYTES% THEN
  PRINT "FAIL: PSRAM free dropped by " + STR$(psram_delta%) + ", expected >= " + STR$(ARRAY_BYTES%)
  END
ENDIF

IF sram_delta% > SRAM_TOLERANCE% THEN
  PRINT "FAIL: SRAM heap dropped by " + STR$(sram_delta%) + " (tolerance " + STR$(SRAM_TOLERANCE%) + "); array did not land in PSRAM"
  END
ENDIF

ERASE big%
PRINT "OK"
END
