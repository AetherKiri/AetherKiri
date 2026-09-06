extends HSlider

const CONTROL_SIZE := Vector2(220.0, 40.0)
const KNOB_SIZE := 18

var tokens

func setup(design_tokens, initial_value: float) -> void:
    tokens = design_tokens
    custom_minimum_size = CONTROL_SIZE
    size_flags_horizontal = Control.SIZE_EXPAND_FILL
    size_flags_vertical = Control.SIZE_SHRINK_CENTER
    focus_mode = Control.FOCUS_ALL
    mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
    mouse_force_pass_scroll_events = false
    value = clampf(initial_value, min_value, max_value)

    add_theme_stylebox_override(
        "slider",
        _track_style(tokens.background_raised)
    )
    add_theme_stylebox_override(
        "grabber_area",
        _track_style(tokens.accent)
    )
    add_theme_stylebox_override(
        "grabber_area_highlight",
        _track_style(tokens.accent.lightened(0.055))
    )
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

func _track_style(fill: Color) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = fill
    style.set_corner_radius_all(3)
    style.content_margin_top = 3
    style.content_margin_bottom = 3
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
