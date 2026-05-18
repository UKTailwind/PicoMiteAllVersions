' Test RANDOMIZE, ERROR, SPACE$, STRING$, RND
' RANDOMIZE + RND reproducibility
RANDOMIZE 42
DIM r1!, r2!, r3!
r1! = RND
r2! = RND
r3! = RND
RANDOMIZE 42
PRINT r1! = RND
PRINT r2! = RND
PRINT r3! = RND
' RND range check (should be 0 <= x < 1)
RANDOMIZE 99
DIM ok%, rv!
ok% = 1
FOR i% = 1 TO 20
  rv! = RND
  IF rv! < 0 OR rv! >= 1 THEN ok% = 0
NEXT
PRINT ok%
' SPACE$
PRINT "|"; SPACE$(0); "|"
PRINT "|"; SPACE$(5); "|"
PRINT LEN(SPACE$(10))
' STRING$
PRINT STRING$(3, 65)
PRINT STRING$(5, "*")
PRINT LEN(STRING$(0, "x"))
' INKEY$ (returns empty string in test mode - no keyboard)
DIM k$
k$ = INKEY$
PRINT LEN(k$)
