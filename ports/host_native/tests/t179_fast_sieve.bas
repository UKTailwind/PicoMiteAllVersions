' RUN_ARGS: --vm
' Sieve of Eratosthenes with '!FAST inner loop
OPTION EXPLICIT

Dim sieve%(1000)

Sub ClearSieve(sieve%(), n%)
  Local i%
  i% = 0
  Do While i% <= n%
    sieve%(i%) = 1
    i% = i% + 1
  Loop
End Sub

Sub RunSieve(sieve%(), n%)
  Local i%, j%, limit%
  limit% = n%
  i% = 2
  Do While i% * i% <= limit%
    If sieve%(i%) = 1 Then
      j% = i% * i%
      '!FAST
      Do While j% <= limit%
        sieve%(j%) = 0
        j% = j% + i%
      Loop
    EndIf
    i% = i% + 1
  Loop
End Sub

Function CountPrimes%(sieve%(), n%)
  Local i%, count%
  count% = 0
  i% = 2
  Do While i% <= n%
    If sieve%(i%) = 1 Then count% = count% + 1
    i% = i% + 1
  Loop
  CountPrimes% = count%
End Function

ClearSieve sieve%(), 1000
RunSieve sieve%(), 1000
Dim count%
count% = CountPrimes%(sieve%(), 1000)
If count% <> 168 Then ERROR "Expected 168 primes, got " + Str$(count%)
PRINT "sieve ok: " + Str$(count%) + " primes"
