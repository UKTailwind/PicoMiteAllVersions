' RUN_ARGS: --interp
ON ERROR SKIP : KILL "B:/chain_target.bas"
OPEN "B:/chain_target.bas" FOR OUTPUT AS #1
PRINT #1, "IF MM.CMDLINE$ <> " + CHR$(34) + "payload" + CHR$(34) + " THEN ERROR " + CHR$(34) + "cmdline" + CHR$(34)
PRINT #1, "PRINT " + CHR$(34) + "chain ok " + CHR$(34) + " + MM.CMDLINE$"
PRINT #1, "END"
CLOSE #1
CHAIN "B:/chain_target.bas", "payload"
