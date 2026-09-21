extends Control

signal item_selected(index: int)

const TRACK_INSET := 3.0
const CONTROL_HEIGHT := 44.0

var tokens
var motion
var buttons: Array[Button] = []
var selected_index := 0
var indicator: PanelContainer
var drag_active := false
var drag_x := -1.0

func setup(design_tokens, motion_system, labels: PackedStringArray, initial_index: int = 0) -> void:
    tokens = design_tokens
    motion = motion_system
    selected_index = clampi(initial_index, 0, maxi(0, labels.size() - 1))
    custom_minimum_size = Vector2(300, CONTROL_HEIGHT)
    clip_contents = false
    mouse_filter = Control.MOUSE_FILTER_STOP
    focus_mode = Control.FOCUS_ALL
    mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND

    # Subtle neutral track: no heavy gray, no borders
    var track := PanelContainer.new()
    track.mouse_filter = Control.MOUSE_FILTER_IGNORE
    track.set_anchors_preset(Control.PRESET_FULL_RECT)
    var track_style: StyleBoxFlat = tokens.panel(Color(tokens.text_primary.r, tokens.text_primary.g, tokens.text_primary.b, 0.05), 8)
    track.add_theme_stylebox_override("panel", track_style)
    add_child(track)

    # Sliding jelly indicator: the only selection visual
    indicator = PanelContainer.new()
    indicator.mouse_filter = Control.MOUSE_FILTER_IGNORE
    var indicator_style: StyleBoxFlat = tokens.panel(tokens.accent_fill, 6)
    indicator.add_theme_stylebox_override("panel", indicator_style)
    add_child(indicator)

    var row := HBoxContainer.new()
    row.set_anchors_preset(Control.PRESET_FULL_RECT)
    row.offset_left = TRACK_INSET
    row.offset_top = TRACK_INSET
    row.offset_right = -TRACK_INSET
    row.offset_bottom = -TRACK_INSET
    row.add_theme_constant_override("separation", 0)
    row.mouse_filter = Control.MOUSE_FILTER_IGNORE
    add_child(row)

    for index in range(labels.size()):
        var button := Button.new()
        button.text = labels[index]
        button.focus_mode = Control.FOCUS_NONE
        button.mouse_filter = Control.MOUSE_FILTER_IGNORE
        button.clip_text = true
        button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        button.add_theme_font_size_override("font_size", 14)
        button.add_theme_color_override("font_color", tokens.text_secondary)
        for state in ["normal", "hover", "pressed", "hover_pressed", "focus", "disabled"]:
            button.add_theme_stylebox_override(state, tokens.panel(Color.TRANSPARENT, 6))
        buttons.append(button)
        row.add_child(button)

    resized.connect(_layout_indicator.bind(false))
    call_deferred("_layout_indicator", false)
    _sync_button_colors()

func _gui_input(event: InputEvent) -> void:
    if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
        if event.pressed:
            drag_active = true
            accept_event()
            _drag_to(event.position.x)
            var pressed_index := _index_at_x(event.position.x)
            if pressed_index >= 0 and pressed_index != selected_index:
                _select(pressed_index, true)
        elif drag_active:
            drag_active = false
            var release_index := _index_at_x(drag_x if drag_x >= 0.0 else event.position.x)
            _end_drag()
            if release_index >= 0 and release_index != selected_index:
                _select(release_index, true)
            else:
                _layout_indicator(true)
            accept_event()
    elif event is InputEventMouseMotion and drag_active:
        _drag_to(event.position.x)
        accept_event()
    elif event.is_action_pressed("ui_left"):
        _select(maxi(0, selected_index - 1), true)
        accept_event()
    elif event.is_action_pressed("ui_right"):
        _select(mini(buttons.size() - 1, selected_index + 1), true)
        accept_event()

func _index_at_x(x: float) -> int:
    if buttons.is_empty() or size.x <= 0.0:
        return -1
    var available_width := maxf(0.0, size.x - TRACK_INSET * 2.0)
    var segment_width := available_width / float(buttons.size())
    if segment_width <= 0.0:
        return -1
    return clampi(int((x - TRACK_INSET) / segment_width), 0, buttons.size() - 1)

func _drag_to(x: float) -> void:
    if buttons.is_empty() or size.x <= 0.0:
        return
    var available_width := maxf(0.0, size.x - TRACK_INSET * 2.0)
    var segment_width := available_width / float(buttons.size())
    var target_size := Vector2(segment_width, maxf(0.0, size.y - TRACK_INSET * 2.0))
    drag_x = clampf(x - segment_width * 0.5, TRACK_INSET, TRACK_INSET + available_width - segment_width)
    indicator.size = target_size
    indicator.pivot_offset = target_size * 0.5
    if motion.reduced_motion:
        indicator.position = Vector2(drag_x, TRACK_INSET)
        return
    # Elastic finger-follow: fast spring with slight elasticity, squashed while dragging
    motion.spring_property(indicator, "position", Vector2(drag_x, TRACK_INSET), 0.07, 1.0)
    motion.spring_property(indicator, "scale", Vector2(1.05, 0.93), 0.10, 0.9)

func _end_drag() -> void:
    drag_x = -1.0
    if motion.reduced_motion:
        return
    motion.spring_property(indicator, "scale", Vector2.ONE, 0.26, 0.55)

func _select(index: int, animate: bool) -> void:
    if index < 0 or index >= buttons.size():
        return
    var changed := index != selected_index
    selected_index = index
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
        button.add_theme_color_override("font_hover_color", color)

func _layout_indicator(animate: bool = false) -> void:
    if indicator == null or buttons.is_empty() or size.x <= 0.0:
        return
    var available_width := maxf(0.0, size.x - TRACK_INSET * 2.0)
    var segment_width := available_width / float(buttons.size())
    var target_position := Vector2(TRACK_INSET + segment_width * float(selected_index), TRACK_INSET)
    var target_size := Vector2(segment_width, maxf(0.0, size.y - TRACK_INSET * 2.0))
    indicator.pivot_offset = target_size * 0.5
    if not animate or motion.reduced_motion:
        indicator.position = target_position
        indicator.size = target_size
        indicator.scale = Vector2.ONE
        return
    # Jelly slide: under-damped position spring + squash then wobble back
    motion.spring_property(indicator, "position", target_position, 0.36, 0.55)
    motion.spring_property(indicator, "size", target_size, 0.30, 1.0)
    motion.spring_property(indicator, "scale", Vector2(1.05, 0.90), 0.12, 0.8)
    var tree := get_tree()
    if tree != null:
        tree.create_timer(0.09).timeout.connect(
            func():
                if indicator != null and is_instance_valid(indicator):
                    motion.spring_property(indicator, "scale", Vector2.ONE, 0.28, 0.55),
            CONNECT_ONE_SHOT
        )
