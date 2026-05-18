' RUN_ARGS: --keys-after-ms 25 z --timeout-ms 500
DO
  k% = KEYDOWN(1)
LOOP UNTIL k% <> 0
PRINT CHR$(k%)
