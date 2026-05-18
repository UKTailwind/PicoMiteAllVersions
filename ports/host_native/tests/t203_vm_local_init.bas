' VM compiler should accept LOCAL with single-var initialiser, the same
' way the interpreter does. Compare-mode test (interp vs VM).
OPTION EXPLICIT

SUB Probe()
  LOCAL INTEGER a = 7
  LOCAL FLOAT b = 1.5
  LOCAL STRING s = "hi"
  PRINT a, b, s
END SUB

Probe
END
