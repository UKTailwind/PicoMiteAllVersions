' smokes/extras.bas — MM.INFO + PEEK/POKE memory + port I/O + RTC SET.
' Breadcrumb PRINTs before each potentially-crashy step so when the
' device resets mid-smoke we know exactly which line did it.

MODE 1
PRINT "BC_after_mode1"

' --- MM.INFO ----------------------------------------------------------
IF MM.INFO(HRES) = 320 AND MM.INFO(VRES) = 200 THEN PRINT "OK_info_dims" ELSE PRINT "FAIL_info_dims " + STR$(MM.INFO(HRES)) + "x" + STR$(MM.INFO(VRES))
DIM ver$ = MM.INFO(VERSION)
IF LEN(ver$) > 5 THEN PRINT "OK_info_version" ELSE PRINT "FAIL_info_version " + ver$
IF MM.INFO(FONTWIDTH) > 0 AND MM.INFO(FONTHEIGHT) > 0 THEN PRINT "OK_info_font" ELSE PRINT "FAIL_info_font"

' --- PEEK on read-only BIOS data area (safe, always mapped) ----------
PRINT "BC_before_peek_bios"
DIM base_kb% = PEEK(SHORT &H413)
IF base_kb% > 0 AND base_kb% < 65536 THEN PRINT "OK_peek_bios" ELSE PRINT "FAIL_peek_bios kb=" + STR$(base_kb%)

' --- Port I/O round-trip via CMOS user RAM (regs 0x37-0x3F unused) ---
PRINT "BC_before_port_io"
POKE PORT &H70, &H37
POKE PORT &H71, &H5A
POKE PORT &H70, &H37
DIM port_v% = PEEK(PORT &H71)
IF port_v% = &H5A THEN PRINT "OK_port_io" ELSE PRINT "FAIL_port_io " + STR$(port_v%)

' --- Memory POKE/PEEK round-trip in known-free RAM -------------------
' Pick an address well above the BSS end (linker map shows ~0x6be300)
' and below the 8 MB free-RAM ceiling. 0x700000 sits in the middle of
' the unused zone.
PRINT "BC_before_poke_byte_700000"
POKE BYTE &H700000, &HA5
PRINT "BC_after_poke_byte"
DIM v_byte% = PEEK(BYTE &H700000)
IF v_byte% = &HA5 THEN PRINT "OK_poke_byte" ELSE PRINT "FAIL_poke_byte " + STR$(v_byte%)

PRINT "BC_before_poke_short"
POKE SHORT &H700004, &HBEEF
DIM v_short% = PEEK(SHORT &H700004)
IF v_short% = &HBEEF THEN PRINT "OK_poke_short" ELSE PRINT "FAIL_poke_short " + STR$(v_short%)

PRINT "BC_before_poke_word"
POKE WORD &H700008, &HDEADBEEF
DIM v_word% = PEEK(WORD &H700008)
IF v_word% = &HDEADBEEF THEN PRINT "OK_poke_word" ELSE PRINT "FAIL_poke_word " + STR$(v_word%)

' --- RTC SET + DATE$/TIME$ verify -------------------------------------
PRINT "BC_before_rtc_set"
RTC SET 2026, 5, 20, 14, 30, 0
PAUSE 50
PRINT "BC_after_rtc_set"
DIM new_d$ = DATE$
DIM new_t$ = TIME$
IF new_d$ = "20-05-2026" THEN PRINT "OK_rtc_date" ELSE PRINT "FAIL_rtc_date " + new_d$
IF LEFT$(new_t$, 5) = "14:30" THEN PRINT "OK_rtc_time" ELSE PRINT "FAIL_rtc_time " + new_t$

PRINT "SMOKE_DONE"
END
