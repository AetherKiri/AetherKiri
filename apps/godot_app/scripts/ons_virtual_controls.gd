extends CanvasLayer

signal key_event_requested(pressed: bool, key_code: int, modifiers: int)
signal pointer_move_requested(screen_position: Vector2, screen_delta: Vector2)
signal pointer_button_requested(
    pressed: bool,
    button: int,
    modifiers: int,
    screen_position: Vector2
)
signal pointer_scroll_requested(delta_y: float, screen_position: Vector2)
signal keyboard_requested
signal virtual_controls_requested

const VK_RETURN := 0x0D
const VK_CONTROL := 0x11
const VK_ESCAPE := 0x1B
const VK_SPACE := 0x20
const VK_1 := 0x31
const VK_2 := 0x32
const VK_3 := 0x33
const VK_4 := 0x34
const VK_A := 0x41
const VK_D := 0x44
const VK_S := 0x53
const VK_W := 0x57
const MOD_CONTROL := 0x04
const POINTER_MOD_RIGHT := 0x10
const OVERLAY_LAYER := 120
const BUTTON_MIN_SIZE := 42.0
const BUTTON_MAX_SIZE := 58.0
const CURSOR_SIZE := 38.0

var menu_button: Button
var keyboard_button: Button
var virtual_controls_button: Button
var escape_button: Button
var control_button: Button
var w_button: Button
var a_button: Button
var s_button: Button
var d_button: Button
var digit_buttons: Array[Button] = []
var enter_button: Button
var space_button: Button
var mouse_left_button: Button
var mouse_right_button: Button
var scroll_up_button: Button
var scroll_down_button: Button
var cursor_handle: Label

var _root: Control
var _tokens
var _enabled := false
var _panel_open := false
var _menu_open := false
var _safe_rect := Rect2()
var _held_keys := {}
var _held_mouse_buttons := {}
var _panel_controls: Array[Control] = []
var _interactive_controls: Array[Control] = []
var _cursor_touch_index := -1
var _cursor_mouse_dragging := false

func setup(tokens) -> void:
    if _root != null:
        return
    name = "OnsVirtualControls"
    layer = OVERLAY_LAYER
    _tokens = tokens

    _root = Control.new()
    _root.name = "Root"
    _root.set_anchors_preset(Control.PRESET_FULL_RECT)
    _root.mouse_filter = Control.MOUSE_FILTER_IGNORE
    add_child(_root)

    menu_button = _create_action_button("MenuButton", "Menu")
    keyboard_button = _create_action_button("KeyboardButton", "Keyboard")
    virtual_controls_button = _create_action_button(
        "VirtualControlsButton", "Controls"
    )
    menu_button.pressed.connect(_toggle_menu)
    keyboard_button.pressed.connect(_request_keyboard)
    virtual_controls_button.pressed.connect(show_virtual_controls)
    _root.add_child(menu_button)
    _root.add_child(keyboard_button)
    _root.add_child(virtual_controls_button)

    escape_button = _add_key_button("EscapeButton", "Esc", VK_ESCAPE)
    control_button = _add_key_button("ControlButton", "Ctrl", VK_CONTROL)
    w_button = _add_key_button("WButton", "W", VK_W)
    a_button = _add_key_button("AButton", "A", VK_A)
    s_button = _add_key_button("SButton", "S", VK_S)
    d_button = _add_key_button("DButton", "D", VK_D)
    for index in range(4):
        var digit := _add_key_button(
            "Digit%dButton" % (index + 1),
            str(index + 1),
            VK_1 + index
        )
        digit_buttons.append(digit)
    enter_button = _add_key_button("EnterButton", "Enter", VK_RETURN, true)
    space_button = _add_key_button("SpaceButton", "Space", VK_SPACE, true)

    mouse_left_button = _add_mouse_button("MouseLeftButton", "L Click", 0, 0)
    mouse_right_button = _add_mouse_button(
        "MouseRightButton", "R Click", 0, POINTER_MOD_RIGHT
    )
    scroll_up_button = _add_scroll_button("ScrollUpButton", "Wheel +", -1.0)
    scroll_down_button = _add_scroll_button("ScrollDownButton", "Wheel -", 1.0)

    cursor_handle = Label.new()
    cursor_handle.name = "MouseCursor"
    cursor_handle.text = "◎"
    cursor_handle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    cursor_handle.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    cursor_handle.mouse_filter = Control.MOUSE_FILTER_IGNORE
    cursor_handle.add_theme_font_size_override("font_size", 30)
    cursor_handle.add_theme_color_override("font_color", Color.WHITE)
    cursor_handle.add_theme_color_override(
        "font_shadow_color", Color(0, 0, 0, 0.9)
    )
    cursor_handle.add_theme_constant_override("shadow_offset_x", 2)
    cursor_handle.add_theme_constant_override("shadow_offset_y", 2)
    _root.add_child(cursor_handle)
    _panel_controls.append(cursor_handle)

    _interactive_controls.append(menu_button)
    _interactive_controls.append(keyboard_button)
    _interactive_controls.append(virtual_controls_button)
    set_enabled(false)

func set_enabled(enabled: bool) -> void:
    if _enabled == enabled:
        visible = enabled
        _sync_visibility()
        return
    _enabled = enabled
    if not enabled:
        release_all()
        _panel_open = false
        _menu_open = false
        _cursor_touch_index = -1
        _cursor_mouse_dragging = false
    visible = enabled
    _sync_visibility()

func is_panel_open() -> bool:
    return _panel_open

func is_menu_open() -> bool:
    return _menu_open

func show_virtual_controls() -> void:
    if not _enabled:
        return
    _menu_open = false
    _panel_open = true
    _sync_visibility()
    virtual_controls_requested.emit()

func release_all() -> void:
    for key_variant in _held_keys.keys().duplicate():
        _release_key(int(key_variant))
    for mouse_variant in _held_mouse_buttons.keys().duplicate():
        var state: Dictionary = _held_mouse_buttons[mouse_variant]
        _release_mouse_button(
            int(state.get("button", 0)),
            int(state.get("modifiers", 0))
        )

func layout(_window_size: Vector2, safe_rect: Rect2) -> void:
    if _root == null:
        return
    _safe_rect = safe_rect
    var short_edge := minf(safe_rect.size.x, safe_rect.size.y)
    var diameter := clampf(short_edge * 0.125, BUTTON_MIN_SIZE, BUTTON_MAX_SIZE)
    var gap := clampf(diameter * 0.16, 7.0, 10.0)
    var margin := clampf(diameter * 0.2, 9.0, 13.0)
    var safe_end := safe_rect.end
    var key_size := Vector2(diameter, diameter)

    var menu_size := Vector2(maxf(48.0, diameter), 34.0)
    menu_button.position = Vector2(
        safe_end.x - menu_size.x,
        safe_rect.position.y + (safe_rect.size.y - menu_size.y) * 0.5
    )
    menu_button.size = menu_size

    var option_size := Vector2(maxf(96.0, diameter * 1.9), 40.0)
    var option_x := menu_button.position.x - option_size.x - gap
    var options_top := menu_button.position.y - option_size.y - gap * 0.5
    keyboard_button.position = Vector2(option_x, options_top)
    keyboard_button.size = option_size
    virtual_controls_button.position = Vector2(
        option_x,
        options_top + option_size.y + gap
    )
    virtual_controls_button.size = option_size

    escape_button.position = safe_rect.position + Vector2(margin, margin)
    escape_button.size = key_size

    var dpad_origin := Vector2(
        safe_rect.position.x + margin,
        safe_end.y - margin - diameter * 2.0 - gap
    )
    w_button.position = dpad_origin + Vector2(diameter + gap, 0)
    a_button.position = dpad_origin + Vector2(0, diameter + gap)
    s_button.position = dpad_origin + Vector2(diameter + gap, diameter + gap)
    d_button.position = dpad_origin + Vector2((diameter + gap) * 2.0, diameter + gap)
    for button in [w_button, a_button, s_button, d_button]:
        button.size = key_size

    var digit_x := safe_end.x - margin - diameter - menu_size.x - gap
    var digit_top := safe_rect.position.y + margin
    for index in range(digit_buttons.size()):
        digit_buttons[index].position = Vector2(
            digit_x,
            digit_top + index * (diameter + gap)
        )
        digit_buttons[index].size = key_size

    var bottom_y := safe_end.y - margin - diameter
    control_button.position = Vector2(
        dpad_origin.x + (diameter + gap) * 3.0,
        bottom_y
    )
    control_button.size = key_size

    var wide_width := diameter * 1.55
    space_button.position = Vector2(
        safe_rect.position.x + (safe_rect.size.x - wide_width) * 0.5,
        bottom_y
    )
    space_button.size = Vector2(wide_width, diameter)
    enter_button.position = Vector2(
        space_button.position.x + wide_width + gap,
        bottom_y
    )
    enter_button.size = Vector2(wide_width, diameter)

    var mouse_width := maxf(72.0, diameter * 1.45)
    mouse_right_button.position = Vector2(digit_x - mouse_width - gap, bottom_y)
    mouse_right_button.size = Vector2(mouse_width, diameter)
    mouse_left_button.position = Vector2(
        mouse_right_button.position.x - mouse_width - gap,
        bottom_y
    )
    mouse_left_button.size = Vector2(mouse_width, diameter)

    var wheel_width := maxf(64.0, diameter * 1.25)
    var wheel_x := safe_rect.position.x + margin
    var wheel_y := safe_rect.position.y + (safe_rect.size.y - diameter * 2.0 - gap) * 0.5
    scroll_up_button.position = Vector2(wheel_x, wheel_y)
    scroll_up_button.size = Vector2(wheel_width, diameter)
    scroll_down_button.position = Vector2(wheel_x, wheel_y + diameter + gap)
    scroll_down_button.size = Vector2(wheel_width, diameter)

    if (
        cursor_handle.position == Vector2.ZERO
        or not safe_rect.encloses(cursor_handle.get_rect())
    ):
        cursor_handle.position = safe_rect.get_center() - Vector2.ONE * CURSOR_SIZE * 0.5
    cursor_handle.size = Vector2.ONE * CURSOR_SIZE
    _clamp_cursor()

func routes_pointer(event: InputEvent) -> bool:
    if not _enabled or _root == null:
        return false

    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        if (
            touch.pressed
            and _panel_open
            and cursor_handle.get_global_rect().has_point(touch.position)
        ):
            _cursor_touch_index = touch.index
            return true
        if touch.index == _cursor_touch_index:
            if not touch.pressed:
                _cursor_touch_index = -1
            return true
    elif event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        if drag.index == _cursor_touch_index:
            _move_cursor_to(drag.position)
            return true
    elif event is InputEventMouseButton:
        var mouse_button := event as InputEventMouseButton
        if mouse_button.button_index == MOUSE_BUTTON_LEFT:
            if (
                mouse_button.pressed
                and _panel_open
                and cursor_handle.get_global_rect().has_point(
                    mouse_button.position
                )
            ):
                _cursor_mouse_dragging = true
                return true
            if _cursor_mouse_dragging:
                if not mouse_button.pressed:
                    _cursor_mouse_dragging = false
                return true
    elif event is InputEventMouseMotion and _cursor_mouse_dragging:
        _move_cursor_to((event as InputEventMouseMotion).position)
        return true

    if not _held_keys.is_empty() or not _held_mouse_buttons.is_empty():
        return event is InputEventScreenTouch \
            or event is InputEventScreenDrag \
            or event is InputEventMouseButton \
            or event is InputEventMouseMotion

    var position := _event_position(event)
    if position.x < 0.0 or position.y < 0.0:
        return false
    for control in _interactive_controls:
        if control.is_visible_in_tree() and control.get_global_rect().has_point(position):
            return true
    if _panel_open and cursor_handle.get_global_rect().has_point(position):
        return true
    return false

func cursor_screen_position() -> Vector2:
    if cursor_handle == null:
        return Vector2.ZERO
    return cursor_handle.get_global_rect().get_center()

func _add_key_button(
    node_name: String,
    label: String,
    key_code: int,
    wide: bool = false
) -> Button:
    var button := _create_control_button(node_name, label, wide)
    button.button_down.connect(_press_key.bind(key_code))
    button.button_up.connect(_release_key.bind(key_code))
    _root.add_child(button)
    _panel_controls.append(button)
    _interactive_controls.append(button)
    return button

func _add_mouse_button(
    node_name: String,
    label: String,
    button_code: int,
    modifiers: int
) -> Button:
    var button := _create_control_button(node_name, label, true)
    button.button_down.connect(_press_mouse_button.bind(button_code, modifiers))
    button.button_up.connect(_release_mouse_button.bind(button_code, modifiers))
    _root.add_child(button)
    _panel_controls.append(button)
    _interactive_controls.append(button)
    return button

func _add_scroll_button(node_name: String, label: String, delta_y: float) -> Button:
    var button := _create_control_button(node_name, label, true)
    button.pressed.connect(_scroll.bind(delta_y))
    _root.add_child(button)
    _panel_controls.append(button)
    _interactive_controls.append(button)
    return button

func _create_control_button(node_name: String, label: String, wide: bool) -> Button:
    var button := Button.new()
    button.name = node_name
    button.text = label
    button.focus_mode = Control.FOCUS_NONE
    button.mouse_filter = Control.MOUSE_FILTER_STOP
    button.keep_pressed_outside = true
    button.add_theme_font_size_override("font_size", 15 if wide else 17)
    button.add_theme_color_override("font_color", Color(1.0, 1.0, 1.0, 0.96))
    button.add_theme_color_override("font_hover_color", Color.WHITE)
    button.add_theme_color_override("font_pressed_color", Color.WHITE)
    button.add_theme_stylebox_override(
        "normal",
        _button_style(
            Color(0.035, 0.035, 0.04, 0.52),
            Color(1.0, 1.0, 1.0, 0.42),
            not wide
        )
    )
    button.add_theme_stylebox_override(
        "hover",
        _button_style(
            Color(0.08, 0.08, 0.09, 0.72),
            Color(1.0, 1.0, 1.0, 0.7),
            not wide
        )
    )
    button.add_theme_stylebox_override(
        "pressed",
        _button_style(
            Color(
                _tokens.accent.r,
                _tokens.accent.g,
                _tokens.accent.b,
                0.86
            ),
            Color.WHITE,
            not wide
        )
    )
    button.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
    return button

func _create_action_button(node_name: String, label: String) -> Button:
    var button := _create_control_button(node_name, label, true)
    button.add_theme_font_size_override("font_size", 14)
    return button

func _button_style(fill: Color, border: Color, circular: bool) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = fill
    style.border_color = border
    style.set_border_width_all(2)
    style.set_corner_radius_all(999 if circular else 12)
    style.shadow_color = Color(0.0, 0.0, 0.0, 0.28)
    style.shadow_size = 7
    style.shadow_offset = Vector2(0, 3)
    return style

func _toggle_menu() -> void:
    if not _enabled:
        return
    if _panel_open:
        release_all()
        _panel_open = false
        _menu_open = true
    else:
        _menu_open = not _menu_open
    _sync_visibility()

func _request_keyboard() -> void:
    if not _enabled:
        return
    release_all()
    _menu_open = false
    _panel_open = false
    _sync_visibility()
    keyboard_requested.emit()

func _sync_visibility() -> void:
    if menu_button == null:
        return
    menu_button.visible = _enabled
    keyboard_button.visible = _enabled and _menu_open
    virtual_controls_button.visible = _enabled and _menu_open
    for control in _panel_controls:
        control.visible = _enabled and _panel_open

func _press_key(key_code: int) -> void:
    if not _enabled or not _panel_open or _held_keys.has(key_code):
        return
    _held_keys[key_code] = true
    key_event_requested.emit(
        true,
        key_code,
        MOD_CONTROL if key_code == VK_CONTROL else 0
    )

func _release_key(key_code: int) -> void:
    if not _held_keys.erase(key_code):
        return
    key_event_requested.emit(false, key_code, 0)

func _press_mouse_button(button: int, modifiers: int) -> void:
    if not _enabled or not _panel_open or _held_mouse_buttons.has(modifiers):
        return
    _held_mouse_buttons[modifiers] = {"button": button, "modifiers": modifiers}
    pointer_button_requested.emit(
        true, button, modifiers, cursor_screen_position()
    )

func _release_mouse_button(button: int, modifiers: int) -> void:
    if not _held_mouse_buttons.erase(modifiers):
        return
    pointer_button_requested.emit(
        false, button, modifiers, cursor_screen_position()
    )

func _scroll(delta_y: float) -> void:
    if not _enabled or not _panel_open:
        return
    pointer_scroll_requested.emit(delta_y, cursor_screen_position())

func _move_cursor_to(screen_position: Vector2) -> void:
    var previous := cursor_screen_position()
    cursor_handle.position = screen_position - cursor_handle.size * 0.5
    _clamp_cursor()
    var current := cursor_screen_position()
    var delta := current - previous
    if not delta.is_zero_approx():
        pointer_move_requested.emit(current, delta)

func _clamp_cursor() -> void:
    if cursor_handle == null or _safe_rect.size == Vector2.ZERO:
        return
    cursor_handle.position.x = clampf(
        cursor_handle.position.x,
        _safe_rect.position.x,
        _safe_rect.end.x - cursor_handle.size.x
    )
    cursor_handle.position.y = clampf(
        cursor_handle.position.y,
        _safe_rect.position.y,
        _safe_rect.end.y - cursor_handle.size.y
    )

func _event_position(event: InputEvent) -> Vector2:
    if event is InputEventScreenTouch:
        return (event as InputEventScreenTouch).position
    if event is InputEventScreenDrag:
        return (event as InputEventScreenDrag).position
    if event is InputEventMouseButton:
        return (event as InputEventMouseButton).position
    if event is InputEventMouseMotion:
        return (event as InputEventMouseMotion).position
    return Vector2(-1.0, -1.0)
