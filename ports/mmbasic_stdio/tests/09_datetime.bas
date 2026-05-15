' EPOCH / DATETIME$ / DAY$ smoke for the stdio port. Mirrors the
' on-device gate in porttools/device_datetime_smoke.py — exercises the
' shared hal_calendar driver (drivers/calendar/calendar_bare.c) from a
' libc-backed build so a host-side validate_all run catches algorithmic
' drift in the same battery the PicoCalc + ESP32 boards run.
PRINT EPOCH("01-01-2024 00:00:00")
PRINT EPOCH("01-01-2024 12:34:56") - EPOCH("01-01-2024 00:00:00")
PRINT DATETIME$(1704067200)
PRINT DATETIME$(951868800)
PRINT DAY$("01-01-2024")
PRINT DAY$("15-06-2024")
PRINT DAY$("29-02-2000")
PRINT DATETIME$(EPOCH("04-07-1980 18:30:15"))
PRINT DATETIME$(EPOCH("31-12-2099 23:59:59"))

' EXPECT:  1704067200
' EXPECT:  45296
' EXPECT: 01-01-2024 00:00:00
' EXPECT: 01-03-2000 00:00:00
' EXPECT: Monday
' EXPECT: Saturday
' EXPECT: Tuesday
' EXPECT: 04-07-1980 18:30:15
' EXPECT: 31-12-2099 23:59:59
