' Sieve of Eratosthenes -- text-only, timed.
' Set MaxN at the top to change how much work it does.
'   1000   -> tiny
'   6000   -> default (fits both RUN and FRUN under the simulated
'            128 KB RP2040 heap; each MMFLOAT is 8 bytes)
'   15000  -> runs under RUN (interpreter) but OOMs under FRUN (VM),
'            because the VM keeps extra compile/runtime state on the
'            same heap.
' Prints the elapsed time at the end instead of every prime, so it
' actually exercises the interpreter/VM rather than the console.

MaxN = 6000
DIM composite(MaxN)

t0 = TIMER

FOR i = 2 TO MaxN
  IF composite(i) = 0 THEN
    FOR j = i * i TO MaxN STEP i
      composite(j) = 1
    NEXT j
  END IF
NEXT i

count = 0
largest = 0
FOR i = 2 TO MaxN
  IF composite(i) = 0 THEN
    count = count + 1
    largest = i
  END IF
NEXT i

t1 = TIMER

PRINT count; " primes up to"; MaxN
PRINT "largest prime ="; largest
PRINT "elapsed"; (t1 - t0); "ms"
