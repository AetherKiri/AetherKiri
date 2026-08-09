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

    # Regression: frame enhancement publishes a raw 4:3 frame and scales it
    # to 1440x1080, while the runtime still accepts input on a 1920x1080
    # surface. DrawDevice maps the entire surface back to its 800x600 layer,
    # so the visible frame must map across the entire surface on both axes.
    var enhanced_top_left := GameInputMapping.map_point_to_surface(
        Vector2(240, 0),
        Rect2(240, 0, 1440, 1080),
        Vector2(800, 600),
        Vector2(1920, 1080)
    )
    if not enhanced_top_left.is_equal_approx(Vector2.ZERO):
        _fail("enhanced 4:3 top-left missed surface origin: %s" % enhanced_top_left)
        return

    var enhanced_center := GameInputMapping.map_point_to_surface(
        Vector2(960, 540),
        Rect2(240, 0, 1440, 1080),
        Vector2(800, 600),
        Vector2(1920, 1080)
    )
    if not enhanced_center.is_equal_approx(Vector2(960, 540)):
        _fail("enhanced 4:3 center was rescaled incorrectly: %s" % enhanced_center)
        return

    var enhanced_bottom_right := GameInputMapping.map_point_to_surface(
        Vector2(1680, 1080),
        Rect2(240, 0, 1440, 1080),
        Vector2(800, 600),
        Vector2(1920, 1080)
    )
    if not enhanced_bottom_right.is_equal_approx(Vector2(1920, 1080)):
        _fail("enhanced 4:3 bottom-right missed surface edge: %s" % enhanced_bottom_right)
        return

    var enhanced_delta := GameInputMapping.map_delta_to_surface(
        Vector2(72, 54),
        Vector2(720, 540),
        Vector2(800, 600),
        Vector2(1920, 1080)
    )
    if not enhanced_delta.is_equal_approx(Vector2(192, 108)):
        _fail("enhanced 4:3 drag delta was rescaled incorrectly: %s" % enhanced_delta)
        return

    var enhanced_outside := GameInputMapping.map_point_to_surface(
        Vector2(200, 540),
        Rect2(240, 0, 1440, 1080),
        Vector2(800, 600),
        Vector2(1920, 1080)
    )
    if enhanced_outside.x >= 0.0 or enhanced_outside.y >= 0.0:
        _fail("enhanced 4:3 side bar accepted pointer input: %s" % enhanced_outside)
        return

    var tap_jitter := GameInputMapping.stable_tap_point(
        Vector2(120, 80), Vector2(127, 88), 18.0
    )
    if not tap_jitter.is_equal_approx(Vector2(120, 80)):
        _fail("tap jitter moved the click point: %s" % tap_jitter)
        return

    var real_drag := GameInputMapping.stable_tap_point(
        Vector2(120, 80), Vector2(150, 80), 18.0
    )
    if not real_drag.is_equal_approx(Vector2(150, 80)):
        _fail("real drag lost its release point: %s" % real_drag)
        return

    print("game_input_mapping_test: PASS")
    quit(0)

func _fail(message: String) -> void:
    push_error("game_input_mapping_test: %s" % message)
    quit(1)
