# ONScripter movie command smoke fixture

Place a test movie at `video.avi`, then launch the Godot smoke test with
`AETHERKIRI_SMOKE_EXPECT_SCRIPT_MEDIA=1`. The script exercises `movie pos`,
`async`, `loop`, and `movie stop`, followed by click-skippable `mpegplay` and
`avi` commands. It then starts a second positioned movie so the probe can
verify a decoded frame. The fixture ignores the local movie so test media is
not redistributed.
