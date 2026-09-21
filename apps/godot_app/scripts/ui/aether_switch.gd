extends Button

const TRACK_SIZE := Vector2(51, 31)
const KNOB_SIZE := Vector2(23, 23)
const TRACK_INSET := 4.0

var tokens
var motion
var knob: PanelContainer

func setup(design_tokens, motion_system, initial_value: bool) -> void:
    tokens = design_tokens
    motion = motion_system
    text = ""
    toggle_mode = true
    button_pressed = initial_value
    focus_mode = Control.FOCUS_ALL
    mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
    custom_minimum_size = TRACK_SIZE
    size_flags_horizontal = Control.SIZE_SHRINK_END
    size_flags_vertical = Control.SIZE_SHRINK_CENTER
    add_theme_stylebox_override("focus", tokens.focus_style(16))

    knob = PanelContainer.new()
    knob.mouse_filter = Control.MOUSE_FILTER_IGNORE
    knob.size = KNOB_SIZE
    knob.pivot_offset = KNOB_SIZE * 0.5
    var knob_style: StyleBoxFlat = tokens.panel(Color.WHITE, 12)
    # Light themes need a hairline that darkens toward the track; dark themes
    # get a top light edge. Either way the geometry stays identical, so every
    # switch in every section reads as the same control.
    knob_style.border_color = (
        Color(1, 1, 1, 0.56) if tokens.mode == "dark" or tokens.mode == "warm_dark" else Color(0, 0, 0, 0.12)
    )
    knob_style.border_width_top = 1
    knob_style.shadow_color = Color(0, 0, 0, 0.28 if tokens.mode == "dark" or tokens.mode == "warm_dark" else 0.16)
    knob_style.shadow_size = 5 if tokens.mode == "dark" or tokens.mode == "warm_dark" else 3
    knob_style.shadow_offset = Vector2(0, 2)
    knob.add_theme_stylebox_override("panel", knob_style)
    add_child(knob)

    button_down.connect(_press_in)
    button_up.connect(_press_out)
    mouse_exited.connect(_press_out)
    toggled.connect(func(value: bool): _sync(value, true))
    _sync(initial_value, false)

func _sync(enabled: bool, animate: bool) -> void:
    var dark: bool = tokens.mode == "dark" or tokens.mode == "warm_dark"
    # Rounded pill track in the app's surface language: a quiet neutral fill
    # with hairline when off, accent fill with a soft glow when on.
    var off_fill := Color(
        tokens.text_primary.r,
        tokens.text_primary.g,
        tokens.text_primary.b,
        0.10 if dark else 0.14
    )
    var track_fill: Color = tokens.accent if enabled else off_fill
    var track_border: Color = tokens.accent.lightened(0.25) if enabled else tokens.separator
    var style: StyleBoxFlat = tokens.panel(track_fill, 16, track_border, 1)
    if enabled:
        style.shadow_color = Color(tokens.accent.r, tokens.accent.g, tokens.accent.b, 0.30)
        style.shadow_size = 6
    else:
        style.shadow_color = Color(tokens.shadow.r, tokens.shadow.g, tokens.shadow.b, 0.10)
        style.shadow_size = 2
    style.shadow_offset = Vector2(0, 2)
    add_theme_stylebox_override("normal", style)
    add_theme_stylebox_override("hover", _track_variant_box(style, 0.04))
    add_theme_stylebox_override("pressed", _track_variant_box(style, -0.05))
    add_theme_stylebox_override("hover_pressed", _track_variant_box(style, 0.04))
    add_theme_stylebox_override("disabled", _track_variant_box(style, -0.38))
    var target := Vector2(TRACK_SIZE.x - TRACK_INSET - KNOB_SIZE.x, TRACK_INSET) if enabled else Vector2(TRACK_INSET, TRACK_INSET)
    if not animate or motion.reduced_motion:
        knob.position = target
        return
    motion.spring_property(knob, "position", target, 0.30, 1.0)
    _stretch_knob()

func _track_variant_box(base: StyleBoxFlat, lighten: float) -> StyleBoxFlat:
    var style: StyleBoxFlat = base.duplicate()
    style.bg_color = base.bg_color.lightened(lighten)
    return style

func _stretch_knob() -> void:
    if motion.reduced_motion:
        return
    # iOS-style elastic deformation: horizontal stretch while sliding, spring back to a round shape
    _animate_knob_scale(Vector2(1.24, 0.80), 0.10)
    var tree := get_tree()
    if tree != null:
        tree.create_timer(0.09).timeout.connect(
            func(): _animate_knob_scale(Vector2.ONE, 0.30),
            CONNECT_ONE_SHOT
        )

func _press_in() -> void:
    if motion.reduced_motion:
        return
    _animate_knob_scale(Vector2(1.11, 0.94), 0.16)

func _press_out() -> void:
    if knob == null:
        return
    if motion.reduced_motion:
        knob.scale = Vector2.ONE
        return
    _animate_knob_scale(Vector2.ONE, 0.24)

func _animate_knob_scale(target: Vector2, response: float) -> void:
    motion.spring_property(knob, "scale", target, response, 1.0)

func _track_box(_fill: Color) -> StyleBoxFlat:
    # Retained for compatibility; the rounded pill built in _sync is the
    # canonical track style now.
    var style: StyleBoxFlat = tokens.panel(tokens.accent, 16, tokens.separator, 1)
    style.shadow_offset = Vector2(0, 2)
    return style
