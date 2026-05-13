' MMBasic tokenizes "" inside a string literal as end-of-string +
' start-of-next-string, NOT as an escape. Adjacent string literals
' auto-concatenate inside PRINT. The bytecode source compiler currently
' rejects this; same pattern hello.bas uses for the RUN hint lines.
PRINT "He said ""hello"""
PRINT "  RUN ""fizzbuzz.bas"""
PRINT "a""b""c"
