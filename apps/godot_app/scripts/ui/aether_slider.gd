extends HSlider

const CONTROL_SIZE := Vector2(220.0, 40.0)
const KNOB_SIZE := 18
const TRACK_THIN := 3.0
const TRACK_THICK := 6.5
const THICKNESS_DURATION := 0.22

var tokens
var track_style: StyleBoxFlat
var fill_style: StyleBoxFlat
var fill_highlight_style: StyleBoxFlat
var thickness_tween: Tween
var scrubbing := false

func setup(design_tokens, initial_value: float) -> void:
    tokens = design_tokens
    custom_minimum_size = CONTROL_SIZE
    size_flags_horizontal = Control.SIZE_EXPAND_FILL
    size_flags_vertical = Control.SIZE_SHRINK_CENTER
    focus_mode = Control.FOCUS_ALL
    mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
    mouse_force_pass_scroll_events = false
    value = clampf(initial_value, min_value, max_value)

    track_style = _track_style(tokens.background_raised)
    fill_style = _track_style(tokens.accent)
    fill_highlight_style = _track_style(tokens.accent.lightened(0.055))
    add_theme_stylebox_override("slider", track_style)
    add_theme_stylebox_override("grabber_area", fill_style)
    add_theme_stylebox_override("grabber_area_highlight", fill_highlight_style)
    add_theme_stylebox_override("focus", tokens.focus_style(8))
    var knob_fill: Color = (
        tokens.text_primary if tokens.mode == "dark" else tokens.background
    )
    add_theme_icon_override(
        "grabber",
        _knob_texture(knob_fill, tokens.accent)
    )
    add_theme_icon_override(
        "grabber_highlight",
        _knob_texture(Color.WHITE, tokens.accent.lightened(0.08))
    )
    add_theme_icon_override(
        "grabber_disabled",
        _knob_texture(tokens.text_tertiary, tokens.separator)
    )
    # The control itself never scales while touched: scaling distorts the round
    # grabber into an ellipse. The track thickens under the fixed knob instead.
    drag_started.connect(func():
        scrubbing = true
        _animate_thickness(1.0)
    )
    drag_ended.connect(func(_value_changed: bool):
        scrubbing = false
        _animate_thickness(0.0)
    )
    mouse_entered.connect(func(): _animate_thickness(0.6))
    mouse_exited.connect(func():
        if not scrubbing:
            _animate_thickness(0.0)
    )

func _animate_thickness(target: float) -> void:
    if thickness_tween != null and thickness_tween.is_valid():
        thickness_tween.kill()
    thickness_tween = create_tween()
    thickness_tween.tween_method(
        _set_track_thickness,
        _current_thickness(),
        clampf(target, 0.0, 1.0),
        THICKNESS_DURATION
    ).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)

func _current_thickness() -> float:
    if track_style == null:
        return 0.0
    return clampf(
        (track_style.content_margin_top - TRACK_THIN) / (TRACK_THICK - TRACK_THIN),
        0.0,
        1.0
    )

func _set_track_thickness(amount: float) -> void:
    var margin := lerpf(TRACK_THIN, TRACK_THICK, clampf(amount, 0.0, 1.0))
    for style in [track_style, fill_style, fill_highlight_style]:
        if style != null:
            style.content_margin_top = margin
            style.content_margin_bottom = margin

func _track_style(fill: Color) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = fill
    style.set_corner_radius_all(3)
    style.content_margin_top = TRACK_THIN
    style.content_margin_bottom = TRACK_THIN
    return style

func _knob_texture(fill: Color, outline: Color) -> Texture2D:
    var image := Image.create(KNOB_SIZE, KNOB_SIZE, false, Image.FORMAT_RGBA8)
    image.fill(Color.TRANSPARENT)
    var center := Vector2.ONE * (float(KNOB_SIZE) - 1.0) * 0.5
    var outer_radius := float(KNOB_SIZE) * 0.5 - 0.5
    var inner_radius := outer_radius - 1.5
    for y in range(KNOB_SIZE):
        for x in range(KNOB_SIZE):
            var distance := Vector2(float(x), float(y)).distance_to(center)
            var edge_alpha := clampf(outer_radius + 0.5 - distance, 0.0, 1.0)
            if edge_alpha <= 0.0:
                continue
            var color := fill if distance <= inner_radius else outline
            color.a *= edge_alpha
            image.set_pixel(x, y, color)
    return ImageTexture.create_from_image(image)
