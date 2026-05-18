' DAY$ / DATETIME$ / EPOCH — previously no-op stubs on host.
' Uses explicit date strings to keep results deterministic (avoids
' wall-clock dependencies that would diverge between runs).
OPTION EXPLICIT
PRINT DAY$("01-01-2024")
PRINT DAY$("15-06-2024")
PRINT DATETIME$(1704067200)
PRINT EPOCH("01-01-2024 00:00:00")
PRINT EPOCH("01-01-2024 12:34:56") - EPOCH("01-01-2024 00:00:00")
