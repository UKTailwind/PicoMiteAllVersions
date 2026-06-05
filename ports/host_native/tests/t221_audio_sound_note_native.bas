' RUN_ARGS: --interp
' Interpreter-only: the current VM path is unstable for shared PLAY SOUND/NOTE audio tests.
PLAY STOP
ExpectNotPlaying "initial"

PLAY SOUND 1, B, S, 440, 20
ExpectSoundPlaying "slot 1 on"
PLAY STOP
ExpectNotPlaying "after stop"

PLAY SOUND 2, B, S, 660, 20
ExpectSoundPlaying "slot 2 on"
PLAY SOUND 2, B, O
ExpectNotPlaying "slot mask cleanup"

PLAY NOTE ON 0, 69, 80
ExpectSoundPlaying "note on ch0"
PLAY NOTE OFF 0, 69
ExpectNotPlaying "note off ch0"

PLAY NOTE ON 1, 72, 80
ExpectSoundPlaying "note on ch1"
PLAY NOTE ON 1, 72, 0
ExpectNotPlaying "velocity zero ch1"

PLAY NOTE ON 0, 60, 90
PLAY NOTE ON 1, 64, 90
ExpectSoundPlaying "two notes"
PLAY NOTE OFF 0, 60
ExpectSoundPlaying "one note remaining"
PLAY NOTE OFF 1, 64
ExpectNotPlaying "all notes off"

PLAY STOP
PRINT "audio sound/note ok"
END

SUB ExpectSoundPlaying label$
  ON ERROR IGNORE
  PLAY PAUSE
  ON ERROR ABORT
  IF MM.ERRNO <> 0 THEN ERROR label$ + " did not accept pause"
  ON ERROR CLEAR
  PLAY RESUME
END SUB

SUB ExpectNotPlaying label$
  ON ERROR IGNORE
  PLAY PAUSE
  ON ERROR ABORT
  IF MM.ERRNO = 0 THEN ERROR label$ + " unexpectedly accepted pause"
  ON ERROR CLEAR
END SUB
