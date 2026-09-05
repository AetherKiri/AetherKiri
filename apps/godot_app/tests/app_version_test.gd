extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

func _initialize() -> void:
    var app = MAIN_SCRIPT.new()
    var expected := str(ProjectSettings.get_setting(
        "application/config/version",
        "development"
    ))
    assert(app._application_version_text() == expected)
    app.free()
    print("app_version_test: PASS")
    quit(0)
