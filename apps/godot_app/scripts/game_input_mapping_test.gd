extends SceneTree

const GameInputMapping = preload("res://scripts/game_input_mapping.gd")

func _initialize() -> void:
    var mapped := GameInputMapping.map_point(
        Vector2(850, 330),
        Rect2(0, 0, 1280, 720),
        Vector2(1280, 720)
    )
    if not mapped.is_equal_approx(Vector2(850, 330)):
        _fail("native-size frame changed pointer coordinates: %s" % mapped)
        return

    # Regression: the host may have requested 1920x1080, but Artemis can
    # restore its 1280x720 game size during Open(). The presented texture,
    # not that stale request, remains the input coordinate space.
    var requested_surface := Vector2(1920, 1080)
    var stale_surface_mapping := mapped * requested_surface / Vector2(1280, 720)
    if stale_surface_mapping.is_equal_approx(mapped):
        _fail("fixture did not exercise a mismatched requested surface")
        return
    if not mapped.is_equal_approx(Vector2(850, 330)):
        _fail("frame coordinate was rescaled to the stale surface")
        return

    var letterboxed := GameInputMapping.map_point(
        Vector2(1000, 540),
        Rect2(40, 20, 1920, 1080),
        Vector2(1280, 720)
    )
    if not letterboxed.is_equal_approx(Vector2(640, 346.66666)):
        _fail("window-to-texture mapping is incorrect: %s" % letterboxed)
        return

    print("game_input_mapping_test: PASS")
    quit(0)

func _fail(message: String) -> void:
    push_error("game_input_mapping_test: %s" % message)
    quit(1)
