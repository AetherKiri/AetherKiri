extends RefCounted

const PHONE_SHORT_EDGE := 600.0
const TABLET_SHORT_EDGE := 1000.0
const COMPACT_SHELL_WIDTH := 760.0
const PHONE_SCALE := 1.0
const COMPACT_DESKTOP_SCALE := 1.20
const TABLET_SCALE := 1.25
const LARGE_TABLET_SCALE := 1.35

static func ui_scale(platform_name: String, window_size: Vector2i, default_scale: float, override_text: String = "") -> float:
    var requested := override_text.strip_edges()
    if not requested.is_empty():
        return clampf(requested.to_float(), 0.75, 2.0)

    var short_edge := float(mini(window_size.x, window_size.y))
    if short_edge < PHONE_SHORT_EDGE:
        return PHONE_SCALE

    if platform_name == "iOS" or platform_name == "Android":
        return LARGE_TABLET_SCALE if short_edge >= TABLET_SHORT_EDGE else TABLET_SCALE

    if short_edge < 900.0:
        return COMPACT_DESKTOP_SCALE
    return clampf(default_scale, 0.75, 2.0)

static func use_compact_shell(viewport_size: Vector2) -> bool:
    return minf(viewport_size.x, viewport_size.y) < PHONE_SHORT_EDGE or viewport_size.x < COMPACT_SHELL_WIDTH
