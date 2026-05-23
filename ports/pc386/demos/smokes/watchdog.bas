' smokes/watchdog.bas — software watchdog. Canonical MMBasic syntax:
'   WATCHDOG ms   — arm/refresh countdown
'   WATCHDOG OFF  — disarm
' To stay alive past the timeout, call `WATCHDOG ms` again periodically.
'
' Reset behaviour (countdown reaching 0 → SoftReset) can't be verified
' automatically — the device reboots and the test runner loses the prompt.
' Manual reset test: at REPL, run `WATCHDOG 1000` and don't touch the
' device for 1.5s; it should reboot.

WATCHDOG OFF
PRINT "OK_watchdog_off_idle"

' Arm long, refresh by re-arming, disarm.
WATCHDOG 10000
PAUSE 50
WATCHDOG 10000   ' refresh (canonical "kick" pattern)
PAUSE 50
WATCHDOG OFF
PRINT "OK_watchdog_refresh"

' Arm long, disarm before any chance of fire.
WATCHDOG 60000
PAUSE 50
WATCHDOG OFF
PRINT "OK_watchdog_long_then_off"

PRINT "SMOKE_DONE"
END
