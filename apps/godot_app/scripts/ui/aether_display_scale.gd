extends RefCounted

const PHONE_SHORT_EDGE := 600.0
const TABLET_SHORT_EDGE := 1000.0
const COMPACT_SHELL_WIDTH := 760.0
const PHONE_ASPECT_RATIO := 1.85
const PHONE_LOGICAL_SHORT_EDGE := 430.0
const TABLET_LOGICAL_SHORT_EDGE := 800.0
const PHONE_SCALE := 1.0
const COMPACT_DESKTOP_SCALE := 1.20
const TABLET_SCALE := 1.25
const LARGE_TABLET_SCALE := 1.35
const MAX_AUTO_SCALE := 3.0
const MAX_HOME_COLUMNS := 3

static func ui_scale(platform_name: String, window_size: Vector2i, default_scale: float, override_text: String = "") -> float:
    var requested := override_text.strip_edges()
    if not requested.is_empty():
        return clampf(requested.to_float(), 0.75, MAX_AUTO_SCALE)

    var short_edge := float(mini(window_size.x, window_size.y))
    var long_edge := float(maxi(window_size.x, window_size.y))
    if short_edge < PHONE_SHORT_EDGE:
        return PHONE_SCALE

    if platform_name == "iOS" or platform_name == "Android":
        var aspect_ratio := long_edge / maxf(1.0, short_edge)
        if aspect_ratio >= PHONE_ASPECT_RATIO:
            return clampf(short_edge / PHONE_LOGICAL_SHORT_EDGE, PHONE_SCALE, MAX_AUTO_SCALE)
        var base_scale := LARGE_TABLET_SCALE if short_edge >= TABLET_SHORT_EDGE else TABLET_SCALE
        return clampf(maxf(base_scale, short_edge / TABLET_LOGICAL_SHORT_EDGE), TABLET_SCALE, MAX_AUTO_SCALE)

    if short_edge < 900.0:
        return COMPACT_DESKTOP_SCALE
    return clampf(default_scale, 0.75, 2.0)

static func use_compact_shell(viewport_size: Vector2) -> bool:
    return minf(viewport_size.x, viewport_size.y) < PHONE_SHORT_EDGE or viewport_size.x < COMPACT_SHELL_WIDTH

static func home_columns(list_width: float, minimum_tile_width: float, gap: float, compact: bool) -> int:
    if compact:
        return 1
    var available_columns := maxi(1, int(floor((list_width + gap) / (minimum_tile_width + gap))))
    return mini(available_columns, MAX_HOME_COLUMNS)
