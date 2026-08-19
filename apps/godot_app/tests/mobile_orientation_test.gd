extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

func _initialize() -> void:
    var app = MAIN_SCRIPT.new()
    app.lock_landscape = false
    assert(app._game_runtime_restore_orientation(
        Vector2i(1080, 2400),
        DisplayServer.SCREEN_SENSOR
    ) == DisplayServer.SCREEN_PORTRAIT)
    assert(app._game_runtime_restore_orientation(
        Vector2i(2400, 1080),
        DisplayServer.SCREEN_SENSOR
    ) == DisplayServer.SCREEN_LANDSCAPE)
    assert(app._game_runtime_restore_orientation(
        Vector2i.ZERO,
        DisplayServer.SCREEN_SENSOR_PORTRAIT
    ) == DisplayServer.SCREEN_SENSOR_PORTRAIT)

    app.lock_landscape = true
    assert(app._game_runtime_restore_orientation(
        Vector2i(1080, 2400),
        DisplayServer.SCREEN_SENSOR
    ) == DisplayServer.SCREEN_LANDSCAPE)

    quit()
