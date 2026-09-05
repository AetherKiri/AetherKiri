# ONScripter smoke fixture

This minimal UTF-8 ONS script paints a white 640 x 480 frame and remains active
long enough for the Godot smoke probe to read the RGBA surface and create an
`ImageTexture`. It has no external assets; the probe supplies AetherKiri's
bundled runtime font.
