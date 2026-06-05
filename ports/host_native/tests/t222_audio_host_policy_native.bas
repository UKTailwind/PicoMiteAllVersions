' RUN_ARGS: --interp
' Interpreter-only: the current VM path is unstable for shared PLAY SOUND/NOTE audio tests.
PLAY STOP
ExpectNotPlaying "initial"

ON ERROR IGNORE
PLAY TONE 440, 440, 10, ToneDone
ON ERROR ABORT
IF MM.ERRNO = 0 THEN ERROR "PLAY TONE interrupt accepted on host"
ON ERROR CLEAR
ExpectNotPlaying "tone interrupt reject"

CheckFilePlay "WAV", "B:/t222_missing.wav"
CheckFilePlay "MP3", "B:/t222_missing.mp3"
CheckFilePlay "FLAC", "B:/t222_missing.flac"
CheckFilePlay "MODFILE", "B:/t222_missing.mod"

PRINT "audio host policy ok"
END

SUB CheckFilePlay kind$, path$
  PLAY SOUND 1, B, S, 440, 20
  ExpectSoundPlaying "PLAY " + kind$ + " active baseline"

  ON ERROR IGNORE
  IF kind$ = "WAV" THEN PLAY WAV path$
  IF kind$ = "MP3" THEN PLAY MP3 path$
  IF kind$ = "FLAC" THEN PLAY FLAC path$
  IF kind$ = "MODFILE" THEN PLAY MODFILE path$
  ON ERROR ABORT
  IF MM.ERRNO = 0 THEN ERROR "PLAY " + kind$ + " unexpectedly succeeded"
  ON ERROR CLEAR
  ExpectNotPlaying "PLAY " + kind$ + " reject"
END SUB

SUB ToneDone
END SUB

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
