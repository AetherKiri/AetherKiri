# Minimal source fixture for the official Ren'Py SDK.
define smoke = Character("Smoke")
default smoke_choice = ""

label start:
    scene black
    with None

    "AetherKiri Ren'Py SDK smoke fixture."
    $ smoke_choice = renpy.call_screen("aetherkiri_choice")
    $ open(renpy.config.gamedir + "/aetherkiri-choice", "w").write(smoke_choice + "\n")
    if smoke_choice == "continue":
        smoke "Choice input is working."
    else:
        smoke "The default path is working."
    "Ren'Py smoke test complete."
    return

screen aetherkiri_choice:
    modal True
    key "K_RETURN" action Return("continue")
    button:
        xysize (640, 360)
        background None
        action Return("continue")
        text "Continue the smoke test"
