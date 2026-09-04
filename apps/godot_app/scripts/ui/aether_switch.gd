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
    knob_style.border_color = Color(1, 1, 1, 0.56)
    knob_style.border_width_top = 1
    knob_style.shadow_color = Color(0, 0, 0, 0.28 if tokens.mode == "dark" else 0.16)
    knob_style.shadow_size = 5 if tokens.mode == "dark" else 3
    knob_style.shadow_offset = Vector2(0, 2)
    knob.add_theme_stylebox_override("panel", knob_style)
    add_child(knob)

    button_down.connect(_press_in)
    button_up.connect(_press_out)
    mouse_exited.connect(_press_out)
    toggled.connect(func(value: bool): _sync(value, true))
    _sync(initial_value, false)

func _sync(enabled: bool, animate: bool) -> void:
    var off_track := Color(0.28, 0.29, 0.32, 1.0) if tokens.mode == "dark" else Color(0.72, 0.73, 0.75, 1.0)
    var track: Color = tokens.accent if enabled else off_track
    var hover_track: Color = track.lightened(0.055)
    var pressed_track: Color = track.darkened(0.055)
    add_theme_stylebox_override("normal", _track_box(track))
    add_theme_stylebox_override("hover", _track_box(hover_track))
    add_theme_stylebox_override("pressed", _track_box(pressed_track))
    add_theme_stylebox_override("hover_pressed", _track_box(hover_track))
    add_theme_stylebox_override("disabled", _track_box(Color(track.r, track.g, track.b, 0.38)))
    var target := Vector2(TRACK_SIZE.x - TRACK_INSET - KNOB_SIZE.x, TRACK_INSET) if enabled else Vector2(TRACK_INSET, TRACK_INSET)
    if not animate or motion.reduced_motion:
        knob.position = target
        return
    motion.spring_property(knob, "position", target, 0.30, 1.0)
    _stretch_knob()

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

func _track_box(fill: Color) -> StyleBoxFlat:
    var border := Color(1, 1, 1, 0.13) if tokens.mode == "dark" else Color(1, 1, 1, 0.48)
    var style: StyleBoxFlat = tokens.panel(fill, 16, border, 1)
    style.shadow_color = Color(tokens.shadow.r, tokens.shadow.g, tokens.shadow.b, 0.16 if tokens.mode == "dark" else 0.06)
    style.shadow_size = 3 if tokens.mode == "dark" else 2
    style.shadow_offset = Vector2(0, 1)
    return style
