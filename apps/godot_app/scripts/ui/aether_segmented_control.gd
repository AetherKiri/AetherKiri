extends Control

signal item_selected(index: int)

const TRACK_INSET := 3.0
const CONTROL_HEIGHT := 44.0

var tokens
var motion
var buttons: Array[Button] = []
var selected_index := 0
var indicator: PanelContainer

func setup(design_tokens, motion_system, labels: PackedStringArray, initial_index: int = 0) -> void:
    tokens = design_tokens
    motion = motion_system
    selected_index = clampi(initial_index, 0, maxi(0, labels.size() - 1))
    custom_minimum_size = Vector2(300, CONTROL_HEIGHT)
    clip_contents = false

    var track := PanelContainer.new()
    track.mouse_filter = Control.MOUSE_FILTER_IGNORE
    track.set_anchors_preset(Control.PRESET_FULL_RECT)
    track.add_theme_stylebox_override("panel", tokens.panel(tokens.background_raised, 8, tokens.separator, 1))
    add_child(track)

    indicator = PanelContainer.new()
    indicator.mouse_filter = Control.MOUSE_FILTER_IGNORE
    var indicator_style: StyleBoxFlat = tokens.panel(tokens.surface_hover, 6, tokens.highlight, 1)
    indicator_style.shadow_color = Color(tokens.shadow.r, tokens.shadow.g, tokens.shadow.b, 0.26 if tokens.mode == "dark" else 0.12)
    indicator_style.shadow_size = 7 if tokens.mode == "dark" else 4
    indicator_style.shadow_offset = Vector2(0, 2)
    indicator.add_theme_stylebox_override("panel", indicator_style)
    add_child(indicator)

    var row := HBoxContainer.new()
    row.set_anchors_preset(Control.PRESET_FULL_RECT)
    row.offset_left = TRACK_INSET
    row.offset_top = TRACK_INSET
    row.offset_right = -TRACK_INSET
    row.offset_bottom = -TRACK_INSET
    row.add_theme_constant_override("separation", 0)
    add_child(row)

    var group := ButtonGroup.new()
    group.allow_unpress = false
    for index in range(labels.size()):
        var button := Button.new()
        button.text = labels[index]
        button.toggle_mode = true
        button.button_group = group
        button.button_pressed = index == selected_index
        button.focus_mode = Control.FOCUS_ALL
        button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
        button.clip_text = true
        button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        button.add_theme_font_size_override("font_size", 14)
        button.add_theme_color_override("font_color", tokens.text_secondary)
        button.add_theme_color_override("font_hover_color", tokens.text_primary)
        button.add_theme_color_override("font_pressed_color", tokens.text_primary)
        button.add_theme_color_override("font_focus_color", tokens.text_primary)
        button.add_theme_stylebox_override("normal", tokens.panel(Color.TRANSPARENT, 6))
        button.add_theme_stylebox_override("hover", tokens.panel(Color(tokens.highlight.r, tokens.highlight.g, tokens.highlight.b, 0.34), 6))
        button.add_theme_stylebox_override("pressed", tokens.panel(Color.TRANSPARENT, 6))
        button.add_theme_stylebox_override("hover_pressed", tokens.panel(Color.TRANSPARENT, 6))
        button.add_theme_stylebox_override("focus", tokens.focus_style(6))
        button.pressed.connect(_select.bind(index, true))
        motion.bind_pressable(button)
        buttons.append(button)
        row.add_child(button)

    resized.connect(_layout_indicator.bind(false))
    call_deferred("_layout_indicator", false)
    _sync_button_colors()

func _select(index: int, animate: bool) -> void:
    if index < 0 or index >= buttons.size():
        return
    var changed := index != selected_index
    selected_index = index
    for button_index in range(buttons.size()):
        buttons[button_index].set_pressed_no_signal(button_index == selected_index)
    _sync_button_colors()
    _layout_indicator(animate)
    if changed:
        item_selected.emit(selected_index)

func _sync_button_colors() -> void:
    for index in range(buttons.size()):
        var button := buttons[index]
        var color: Color = tokens.text_primary if index == selected_index else tokens.text_secondary
        button.add_theme_color_override("font_color", color)
        button.add_theme_color_override("font_pressed_color", color)
        button.add_theme_color_override("font_focus_color", color)

func _layout_indicator(animate: bool = false) -> void:
    if indicator == null or buttons.is_empty() or size.x <= 0.0:
        return
    var available_width := maxf(0.0, size.x - TRACK_INSET * 2.0)
    var segment_width := available_width / float(buttons.size())
    var target_position := Vector2(TRACK_INSET + segment_width * float(selected_index), TRACK_INSET)
    var target_size := Vector2(segment_width, maxf(0.0, size.y - TRACK_INSET * 2.0))
    if not animate or motion.reduced_motion:
        indicator.position = target_position
        indicator.size = target_size
        return
    motion.spring_property(indicator, "position", target_position, 0.32, 1.0)
    motion.spring_property(indicator, "size", target_size, 0.32, 1.0)
