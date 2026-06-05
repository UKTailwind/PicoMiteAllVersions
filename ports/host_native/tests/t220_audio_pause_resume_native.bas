' RUN_ARGS: --interp
' Interpreter-only: the current VM path is unstable for shared PLAY SOUND/NOTE audio tests.
PLAY STOP
ExpectNotPlaying "initial"

PLAY TONE 440, 440
ExpectCanPause "tone"

PLAY PAUSE
ExpectCanResume "paused tone"

PLAY RESUME
ExpectCanPause "resumed tone"

PLAY STOP
ExpectNotPlaying "stopped tone"

PLAY SOUND 1, B, S, 440, 20
ExpectCanPause "sound"

PLAY PAUSE
ExpectCanResume "paused sound"

PLAY RESUME
ExpectCanPause "resumed sound"

PLAY STOP
ExpectNotPlaying "stopped sound"
PRINT "audio pause/resume ok"
END

SUB ExpectCanPause label$
  ON ERROR IGNORE
  PLAY PAUSE
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN ERROR label$ + " did not accept pause"
  ON ERROR CLEAR
  PLAY RESUME
END SUB

SUB ExpectCanResume label$
  ON ERROR IGNORE
  PLAY RESUME
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN ERROR label$ + " did not accept resume"
  ON ERROR CLEAR
  PLAY PAUSE
END SUB

SUB ExpectNotPlaying label$
  ON ERROR IGNORE
  PLAY PAUSE
  ON ERROR ABORT
  IF MM.ERRNO = 0 THEN ERROR label$ + " unexpectedly accepted pause"
  ON ERROR CLEAR
END SUB
