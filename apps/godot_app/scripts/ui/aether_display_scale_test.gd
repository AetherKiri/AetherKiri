extends SceneTree

const AetherDisplayScale = preload("res://scripts/ui/aether_display_scale.gd")

func _init() -> void:
    if not _expect_scale("phone portrait", AetherDisplayScale.ui_scale("iOS", Vector2i(390, 844), 1.35), 1.0): return
    if not _expect_scale("phone landscape", AetherDisplayScale.ui_scale("Android", Vector2i(844, 390), 1.35), 1.0): return
    if not _expect_scale("tablet portrait", AetherDisplayScale.ui_scale("iOS", Vector2i(768, 1024), 1.35), 1.25): return
    if not _expect_scale("tablet landscape", AetherDisplayScale.ui_scale("Android", Vector2i(1024, 768), 1.35), 1.25): return
    if not _expect_scale("large tablet", AetherDisplayScale.ui_scale("iOS", Vector2i(1366, 1024), 1.35), 1.35): return
    if not _expect_scale("4K tablet", AetherDisplayScale.ui_scale("Android", Vector2i(3840, 2160), 1.35), 2.70): return
    if not _expect_scale("high-resolution phone", AetherDisplayScale.ui_scale("Android", Vector2i(1440, 3200), 1.35), 3.0): return
    if not _expect_scale("compact desktop", AetherDisplayScale.ui_scale("Windows", Vector2i(1280, 720), 1.35), 1.20): return
    if not _expect_scale("explicit override", AetherDisplayScale.ui_scale("Android", Vector2i(390, 844), 1.35, "1.6"), 1.6): return

    if AetherDisplayScale.use_compact_shell(Vector2(819, 614)):
        _fail("tablet landscape incorrectly selected compact navigation")
        return
    if not AetherDisplayScale.use_compact_shell(Vector2(614, 819)):
        _fail("tablet portrait did not select compact navigation")
        return
    if not AetherDisplayScale.use_compact_shell(Vector2(844, 390)):
        _fail("phone landscape did not select compact navigation")
        return
    if AetherDisplayScale.home_columns(2200.0, 340.0, 16.0, false) != 3:
        _fail("wide tablet grid exceeded three columns")
        return
    if AetherDisplayScale.home_columns(2200.0, 340.0, 16.0, true) != 1:
        _fail("compact grid did not stay single-column")
        return

    print("aether_display_scale_test: PASS")
    quit(0)

func _expect_scale(label: String, actual: float, expected: float) -> bool:
    if not is_equal_approx(actual, expected):
        _fail("%s expected %.2f, received %.2f" % [label, expected, actual])
        return false
    return true

func _fail(message: String) -> void:
    push_error("aether_display_scale_test: %s" % message)
    quit(1)
