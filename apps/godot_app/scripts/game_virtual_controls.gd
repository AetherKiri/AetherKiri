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
signal input_mode_changed(mode: String)

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
const BUTTON_MIN_SIZE := 44.0
const BUTTON_MAX_SIZE := 68.0
const MENU_BUTTON_SIZE := Vector2(44.0, 40.0)
const CURSOR_SIZE := 44.0
const CURSOR_HOTSPOT := Vector2(15.0, 10.0)
const CURSOR_TAP_DRAG_THRESHOLD := 10.0
const CURSOR_TWO_FINGER_TAP_WINDOW_MSEC := 220
const MENU_DRAG_THRESHOLD := 6.0
const SCROLL_HOLD_DELAY_MSEC := 360
const SCROLL_HOLD_REPEAT_MSEC := 90
const SCROLL_HOLD_MAX_CATCH_UP_STEPS := 4
const DPAD_DEAD_ZONE_RATIO := 0.18
const DPAD_DIRECTION_AXIS_THRESHOLD := 0.382683
const INPUT_DEVICE_ID_EMULATION := -1
const INPUT_MODE_MOUSE := "mouse"
const INPUT_MODE_TOUCH := "touch"
const INPUT_MODES := [INPUT_MODE_MOUSE, INPUT_MODE_TOUCH]
const KEYBOARD_CONTROLS_OPACITY_MIN := 0.2
const KEYBOARD_CONTROLS_OPACITY_MAX := 1.0

const REFERENCE_FILL := Color(0.30, 0.31, 0.33, 0.58)
const REFERENCE_FILL_HOVER := Color(0.36, 0.37, 0.39, 0.72)
const REFERENCE_BORDER := Color(0.82, 0.84, 0.88, 0.72)
const REFERENCE_TEXT := Color(0.94, 0.95, 0.97, 0.84)
const COMPUTER_USE_CURSOR_FILL := Color(0.08, 0.12, 0.15, 0.82)
const COMPUTER_USE_CURSOR_OUTLINE := Color(0.68, 0.78, 0.84, 0.96)

var menu_button: Button
var keyboard_button: Button
var virtual_controls_button: Button
var input_mode_button: Button
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
var cursor_handle: Control
var dpad_backdrop: Panel
var dpad_center: Panel

var _root: Control
var _tokens
var _enabled := false
var _menu_button_enabled := true
var _keyboard_controls_opacity := 1.0
var _panel_open := false
var _menu_open := false
var _safe_rect := Rect2()
var _held_keys := {}
var _held_mouse_buttons := {}
var _panel_controls: Array[Control] = []
var _interactive_controls: Array[Control] = []
var _cursor_touch_index := -1
var _cursor_mouse_dragging := false
var _cursor_initialized := false
var _cursor_drag_has_position := false
var _cursor_drag_last_screen_position := Vector2.ZERO
var _cursor_touch_start_screen_position := Vector2.ZERO
var _cursor_touch_down_msec := 0
var _cursor_touch_tap_candidate := false
var _cursor_touch_released := false
var _cursor_secondary_touch_index := -1
var _cursor_secondary_touch_start_screen_position := Vector2.ZERO
var _cursor_secondary_touch_released := false
var _cursor_two_finger_tap_candidate := false
var _menu_touch_index := -1
var _menu_mouse_dragging := false
var _menu_drag_start_pointer_y := 0.0
var _menu_drag_grab_offset_y := 0.0
var _menu_drag_moved := false
var _menu_y_ratio := 0.0
var _input_mode := INPUT_MODE_MOUSE
var _mouse_mode_blocked_touch_indices := {}
var _held_scroll_direction := 0.0
var _scroll_next_repeat_msec := 0
var _dpad_touch_index := -1
var _dpad_mouse_dragging := false
var _dpad_home_position := Vector2.ZERO
var _dpad_travel_radius := 0.0
var _dpad_vector := Vector2.ZERO
var _dpad_held_keys := {}

func setup(tokens) -> void:
    if _root != null:
        return
    name = "GameVirtualControls"
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

    _apply_edge_menu_style(menu_button)
    _attach_edge_menu_icon(menu_button)
    _apply_menu_option_style(keyboard_button)
    _apply_menu_option_style(virtual_controls_button)

    input_mode_button = _create_action_button("InputModeButton", "")
    input_mode_button.pressed.connect(_toggle_input_mode)
    _root.add_child(input_mode_button)
    _panel_controls.append(input_mode_button)
    _interactive_controls.append(input_mode_button)
    _apply_input_mode_style(input_mode_button)
    _sync_input_mode_presentation()

    dpad_backdrop = _create_decorative_panel(
        "DpadBackdrop",
        _button_style(Color(0.31, 0.32, 0.34, 0.46), REFERENCE_BORDER, true)
    )
    dpad_backdrop.z_index = -2
    _root.add_child(dpad_backdrop)
    _panel_controls.append(dpad_backdrop)

    dpad_center = _create_decorative_panel(
        "DpadCenter",
        _button_style(Color(0.42, 0.43, 0.45, 0.46), REFERENCE_BORDER, true)
    )
    dpad_center.z_index = -1
    _root.add_child(dpad_center)
    _panel_controls.append(dpad_center)

    escape_button = _add_key_button("EscapeButton", "Esc", VK_ESCAPE)
    control_button = _add_key_button("ControlButton", "Ctrl", VK_CONTROL)
    w_button = _add_key_button("WButton", "W", VK_W)
    a_button = _add_key_button("AButton", "A", VK_A)
    s_button = _add_key_button("SButton", "S", VK_S)
    d_button = _add_key_button("DButton", "D", VK_D)
    for direction_button in [w_button, a_button, s_button, d_button]:
        _apply_dpad_button_style(direction_button)
        # The whole disc is an eight-way joystick. Direction labels are only
        # visual targets; letting their individual Buttons handle input would
        # prevent a drag from holding two keys on a diagonal.
        direction_button.toggle_mode = true
        direction_button.set_pressed_no_signal(false)
        direction_button.mouse_filter = Control.MOUSE_FILTER_IGNORE
        _interactive_controls.erase(direction_button)
    for index in range(4):
        var digit := _add_key_button(
            "Digit%dButton" % (index + 1),
            str(index + 1),
            VK_1 + index
        )
        digit_buttons.append(digit)
    enter_button = _add_key_button("EnterButton", "Enter", VK_RETURN, true)
    space_button = _add_key_button("SpaceButton", "Space", VK_SPACE, true)

    mouse_left_button = _add_mouse_button("MouseLeftButton", "", 0, 0)
    mouse_right_button = _add_mouse_button(
        "MouseRightButton", "", 1, POINTER_MOD_RIGHT
    )
    _attach_mouse_icon(mouse_left_button, true)
    _attach_mouse_icon(mouse_right_button, false)

    scroll_up_button = _add_scroll_button("ScrollUpButton", "▲", 1.0)
    scroll_down_button = _add_scroll_button("ScrollDownButton", "▼", -1.0)
    scroll_up_button.tooltip_text = "Scroll up (hold to repeat)"
    scroll_down_button.tooltip_text = "Scroll down (hold to repeat)"

    cursor_handle = Control.new()
    cursor_handle.name = "MouseCursor"
    cursor_handle.mouse_filter = Control.MOUSE_FILTER_IGNORE
    _attach_pointer_visual(cursor_handle)
    _root.add_child(cursor_handle)
    _panel_controls.append(cursor_handle)

    _interactive_controls.append(menu_button)
    _interactive_controls.append(keyboard_button)
    _interactive_controls.append(virtual_controls_button)
    _apply_keyboard_controls_opacity()
    set_enabled(false)

func set_enabled(enabled: bool) -> void:
    if _enabled == enabled:
        visible = enabled and _menu_button_enabled
        _sync_visibility()
        return
    _enabled = enabled
    if not enabled:
        release_all()
        _panel_open = false
        _menu_open = false
        _reset_cursor_touch_gesture()
        _cursor_mouse_dragging = false
        _reset_cursor_drag_position()
        _menu_touch_index = -1
        _menu_mouse_dragging = false
        _menu_drag_moved = false
        _dpad_touch_index = -1
        _dpad_mouse_dragging = false
        _mouse_mode_blocked_touch_indices.clear()
    visible = enabled and _menu_button_enabled
    _sync_visibility()

func set_menu_button_enabled(enabled: bool) -> void:
    if _menu_button_enabled == enabled:
        visible = _enabled and enabled
        _sync_visibility()
        return
    _menu_button_enabled = enabled
    if not enabled:
        release_all()
        _panel_open = false
        _menu_open = false
        _reset_cursor_touch_gesture()
        _cursor_mouse_dragging = false
        _reset_cursor_drag_position()
        _mouse_mode_blocked_touch_indices.clear()
        _menu_touch_index = -1
        _menu_mouse_dragging = false
        _menu_drag_moved = false
    visible = _enabled and enabled
    _sync_visibility()

func is_menu_button_enabled() -> bool:
    return _menu_button_enabled

func set_keyboard_controls_opacity(opacity: float) -> void:
    _keyboard_controls_opacity = clampf(
        opacity,
        KEYBOARD_CONTROLS_OPACITY_MIN,
        KEYBOARD_CONTROLS_OPACITY_MAX
    )
    _apply_keyboard_controls_opacity()

func keyboard_controls_opacity() -> float:
    return _keyboard_controls_opacity

func is_panel_open() -> bool:
    return _panel_open

func is_menu_open() -> bool:
    return _menu_open

func input_mode() -> String:
    return _input_mode

func is_touch_mode() -> bool:
    return _input_mode == INPUT_MODE_TOUCH

func set_input_mode(mode: String, notify: bool = false) -> void:
    var normalized := mode if mode in INPUT_MODES else INPUT_MODE_MOUSE
    if _input_mode == normalized:
        _sync_input_mode_presentation()
        _sync_visibility()
        return
    release_all()
    _reset_cursor_touch_gesture()
    _cursor_mouse_dragging = false
    _reset_cursor_drag_position()
    _mouse_mode_blocked_touch_indices.clear()
    _input_mode = normalized
    _sync_input_mode_presentation()
    _sync_visibility()
    if notify:
        input_mode_changed.emit(_input_mode)

func show_virtual_controls() -> void:
    if not _enabled or not _menu_button_enabled:
        return
    _menu_open = false
    _panel_open = true
    _sync_visibility()
    virtual_controls_requested.emit()

func release_all() -> void:
    _cancel_scroll_hold()
    _reset_dpad()
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
    var diameter := clampf(short_edge * 0.13, BUTTON_MIN_SIZE, BUTTON_MAX_SIZE)
    var gap := clampf(diameter * 0.13, 6.0, 9.0)
    var margin := clampf(diameter * 0.2, 9.0, 14.0)
    var safe_end := safe_rect.end
    var key_size := Vector2(diameter, diameter)

    # Keep the edge affordance close to the 40 px standard-button token from
    # DESIGN.md instead of scaling it up with the larger virtual key caps.
    var menu_size := MENU_BUTTON_SIZE
    var menu_y_range := maxf(0.0, safe_rect.size.y - menu_size.y)
    menu_button.position = Vector2(
        safe_end.x - menu_size.x,
        safe_rect.position.y + menu_y_range * _menu_y_ratio
    )
    menu_button.size = menu_size
    _layout_edge_menu_icon()

    _layout_menu_options(diameter, gap)

    escape_button.position = safe_rect.position + Vector2(margin, margin)
    escape_button.size = key_size

    var mode_size := Vector2(
        clampf(safe_rect.size.x * 0.22, 132.0, 168.0),
        clampf(diameter * 0.72, 40.0, 48.0)
    )
    input_mode_button.position = Vector2(
        safe_rect.get_center().x - mode_size.x * 0.5,
        safe_rect.position.y + margin
    )
    input_mode_button.size = mode_size

    var dpad_diameter := clampf(short_edge * 0.36, 118.0, 210.0)
    var dpad_origin := Vector2(
        safe_rect.position.x + margin,
        safe_end.y - margin - dpad_diameter
    )
    dpad_backdrop.position = dpad_origin
    dpad_backdrop.size = Vector2.ONE * dpad_diameter

    var dpad_center_position := dpad_origin + Vector2.ONE * dpad_diameter * 0.5
    var direction_size := Vector2.ONE * dpad_diameter * 0.36
    var direction_offset := dpad_diameter * 0.30
    w_button.position = dpad_center_position + Vector2(0, -direction_offset) \
        - direction_size * 0.5
    a_button.position = dpad_center_position + Vector2(-direction_offset, 0) \
        - direction_size * 0.5
    s_button.position = dpad_center_position + Vector2(0, direction_offset) \
        - direction_size * 0.5
    d_button.position = dpad_center_position + Vector2(direction_offset, 0) \
        - direction_size * 0.5
    for button in [w_button, a_button, s_button, d_button]:
        button.size = direction_size

    var center_size := dpad_diameter * 0.34
    dpad_center.size = Vector2.ONE * center_size
    _dpad_home_position = (
        dpad_center_position - Vector2.ONE * center_size * 0.5
    )
    _dpad_travel_radius = (dpad_diameter - center_size) * 0.5
    _position_dpad_center()

    var digit_x := menu_button.position.x - diameter - gap
    var digit_top := safe_rect.position.y + margin + diameter * 0.24
    for index in range(digit_buttons.size()):
        digit_buttons[index].position = Vector2(
            digit_x,
            digit_top + index * (diameter + gap)
        )
        digit_buttons[index].size = key_size

    var bottom_y := safe_end.y - margin - diameter
    control_button.position = Vector2(
        dpad_origin.x + dpad_diameter + gap,
        bottom_y
    )
    control_button.size = key_size

    var action_diameter := diameter * 1.12
    var action_size := Vector2.ONE * action_diameter
    var action_left := digit_x - action_diameter * 2.0 - gap * 2.0
    var mouse_y := bottom_y - action_diameter - gap
    mouse_left_button.position = Vector2(action_left, mouse_y)
    mouse_right_button.position = Vector2(action_left + action_diameter + gap, mouse_y)
    space_button.position = Vector2(action_left, bottom_y)
    enter_button.position = Vector2(action_left + action_diameter + gap, bottom_y)
    for action_button in [
        mouse_left_button,
        mouse_right_button,
        space_button,
        enter_button,
    ]:
        action_button.size = action_size
    _layout_mouse_icon(mouse_left_button)
    _layout_mouse_icon(mouse_right_button)

    var scroll_stack_height := diameter * 2.0 + gap
    var desired_scroll_top := (
        safe_rect.position.y
        + (safe_rect.size.y - scroll_stack_height) * 0.46
    )
    var scroll_min_y := escape_button.get_rect().end.y + gap
    var scroll_max_y := dpad_origin.y - gap - scroll_stack_height
    var scroll_top := clampf(
        desired_scroll_top,
        scroll_min_y,
        maxf(scroll_min_y, scroll_max_y)
    )
    var scroll_x := safe_rect.position.x + margin
    scroll_up_button.position = Vector2(scroll_x, scroll_top)
    scroll_up_button.size = key_size
    scroll_down_button.position = Vector2(scroll_x, scroll_top + diameter + gap)
    scroll_down_button.size = key_size

    if not _cursor_initialized:
        cursor_handle.position = safe_rect.get_center() - CURSOR_HOTSPOT
        _cursor_initialized = true
    cursor_handle.size = Vector2.ONE * CURSOR_SIZE
    # Preserve the pointer across repeated safe-area layouts. Rect2.has_point()
    # excludes its right and bottom edges, so using it here used to recenter a
    # cursor whose image origin was correctly clamped exactly onto either edge.
    _clamp_cursor()

func owns_viewport_pointer(event: InputEvent) -> bool:
    if not _enabled or not _panel_open or is_touch_mode():
        return false
    return (
        event is InputEventScreenTouch
        or event is InputEventScreenDrag
        or event is InputEventMouseButton
        or event is InputEventMouseMotion
    )

func routes_pointer(event: InputEvent) -> bool:
    if not _enabled or _root == null:
        return false

    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        if touch.index == _dpad_touch_index:
            if touch.pressed:
                _update_dpad(touch.position)
            else:
                _dpad_touch_index = -1
                _reset_dpad()
            return true
        if (
            touch.pressed
            and _panel_open
            and _dpad_contains_point(touch.position)
        ):
            _cancel_cursor_tap_for_additional_touch(touch.index)
            if _dpad_touch_index == -1:
                _dpad_touch_index = touch.index
                # iOS may synthesize a mouse press before exposing the same
                # contact as ScreenTouch. The real touch owns the joystick.
                _dpad_mouse_dragging = false
                _update_dpad(touch.position)
            return true
        if _mouse_mode_blocked_touch_indices.has(touch.index):
            if not touch.pressed:
                _mouse_mode_blocked_touch_indices.erase(touch.index)
            return true
        if touch.index == _cursor_secondary_touch_index:
            if not touch.pressed:
                _finish_cursor_secondary_touch()
            return true
        if touch.pressed and _visible_interactive_control_at(touch.position):
            _cancel_cursor_tap_for_additional_touch(touch.index)
        if (
            touch.pressed
            and menu_button.visible
            and menu_button.get_global_rect().has_point(touch.position)
        ):
            _begin_menu_drag(touch.position.y)
            _menu_touch_index = touch.index
            return true
        if touch.index == _menu_touch_index:
            if not touch.pressed:
                _menu_touch_index = -1
                _finish_menu_drag()
            return true
        if (
            touch.pressed
            and _panel_open
            and not is_touch_mode()
            and not _visible_interactive_control_at(touch.position)
        ):
            if _cursor_touch_index == -1:
                if _cursor_two_finger_tap_candidate:
                    _cancel_cursor_two_finger_tap()
                    _mouse_mode_blocked_touch_indices[touch.index] = true
                    return true
                # iOS can deliver the same finger as both a mouse track and a
                # ScreenTouch track. Their positions are not guaranteed to be
                # in the same coordinate space, so the real touch must always
                # take ownership before its first ScreenDrag.
                if _cursor_mouse_dragging:
                    _cursor_mouse_dragging = false
                _begin_cursor_touch(touch.index, touch.position)
            elif _cursor_touch_index != touch.index:
                _begin_cursor_secondary_touch(touch.index, touch.position)
            return true
        if touch.index == _cursor_touch_index:
            if not touch.pressed:
                _finish_cursor_primary_touch()
            return true
    elif event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        if drag.index == _dpad_touch_index:
            _update_dpad(drag.position)
            return true
        if _mouse_mode_blocked_touch_indices.has(drag.index):
            return true
        if drag.index == _cursor_secondary_touch_index:
            _update_cursor_secondary_touch(drag.position)
            return true
        if drag.index == _menu_touch_index:
            _drag_menu_to(drag.position.y)
            return true
        if drag.index == _cursor_touch_index:
            _update_cursor_primary_touch(drag.position)
            return true
    elif event is InputEventMouseButton:
        var mouse_button := event as InputEventMouseButton
        if mouse_button.button_index == MOUSE_BUTTON_LEFT:
            if (
                mouse_button.pressed
                and _panel_open
                and _dpad_contains_point(mouse_button.position)
            ):
                if _dpad_touch_index == -1:
                    _dpad_mouse_dragging = true
                    _update_dpad(mouse_button.position)
                return true
            if _dpad_mouse_dragging:
                if not mouse_button.pressed:
                    _dpad_mouse_dragging = false
                    _reset_dpad()
                return true
            if (
                mouse_button.device != INPUT_DEVICE_ID_EMULATION
                and
                mouse_button.pressed
                and menu_button.visible
                and menu_button.get_global_rect().has_point(mouse_button.position)
            ):
                _begin_menu_drag(mouse_button.position.y)
                _menu_mouse_dragging = true
                return true
            if _menu_mouse_dragging:
                if not mouse_button.pressed:
                    _menu_mouse_dragging = false
                    _finish_menu_drag()
                return true
            if (
                mouse_button.pressed
                and _panel_open
                and not is_touch_mode()
                and not _visible_interactive_control_at(mouse_button.position)
            ):
                # Ignore a mouse press synthesized after the real touch. A
                # physical mouse remains supported because it has no active
                # ScreenTouch track.
                if _cursor_touch_index != -1:
                    return true
                _cursor_mouse_dragging = true
                _begin_cursor_drag(mouse_button.position)
                return true
            if _cursor_mouse_dragging:
                if not mouse_button.pressed:
                    _cursor_mouse_dragging = false
                    _finish_cursor_drag_if_inactive()
                return true
    elif event is InputEventMouseMotion:
        var motion := event as InputEventMouseMotion
        if _dpad_mouse_dragging:
            _update_dpad(motion.position)
            return true
        if (
            motion.device != INPUT_DEVICE_ID_EMULATION
            and _menu_mouse_dragging
        ):
            _drag_menu_to(motion.position.y)
            return true
        if _cursor_mouse_dragging:
            _drag_cursor_to(motion.position)
            return true

    if not _held_keys.is_empty() or not _held_mouse_buttons.is_empty():
        return event is InputEventScreenTouch \
            or event is InputEventScreenDrag \
            or event is InputEventMouseButton \
            or event is InputEventMouseMotion

    var position := _event_position(event)
    if position.x < 0.0 or position.y < 0.0:
        return false
    if _visible_interactive_control_at(position):
        return true
    if (
        _panel_open
        and not is_touch_mode()
        and cursor_handle.get_global_rect().has_point(position)
    ):
        return true
    if _panel_open and not is_touch_mode():
        return (
            event is InputEventScreenTouch
            or event is InputEventScreenDrag
            or event is InputEventMouseButton
            or event is InputEventMouseMotion
        )
    return false

func cursor_screen_position() -> Vector2:
    if cursor_handle == null:
        return Vector2.ZERO
    return cursor_handle.get_global_rect().position + CURSOR_HOTSPOT

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
    button.set_meta("scroll_delta_y", delta_y)
    button.button_down.connect(_press_scroll.bind(delta_y))
    button.button_up.connect(_release_scroll.bind(delta_y))
    button.add_theme_font_size_override("font_size", 18)
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
    button.add_theme_font_size_override("font_size", 14 if wide else 17)
    button.add_theme_color_override("font_color", REFERENCE_TEXT)
    button.add_theme_color_override("font_hover_color", Color.WHITE)
    button.add_theme_color_override("font_pressed_color", Color.WHITE)
    button.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.48))
    button.add_theme_constant_override("outline_size", 2)
    button.add_theme_stylebox_override(
        "normal",
        _button_style(REFERENCE_FILL, REFERENCE_BORDER, true)
    )
    button.add_theme_stylebox_override(
        "hover",
        _button_style(REFERENCE_FILL_HOVER, Color.WHITE, true)
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
            true
        )
    )
    button.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
    return button

func _create_action_button(node_name: String, label: String) -> Button:
    var button := _create_control_button(node_name, label, true)
    button.add_theme_font_size_override("font_size", 14)
    return button

func _create_decorative_panel(node_name: String, style: StyleBoxFlat) -> Panel:
    var panel := Panel.new()
    panel.name = node_name
    panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
    panel.add_theme_stylebox_override("panel", style)
    return panel

func _apply_dpad_button_style(button: Button) -> void:
    var empty := StyleBoxEmpty.new()
    button.add_theme_stylebox_override("normal", empty)
    button.add_theme_stylebox_override("hover", empty)
    button.add_theme_stylebox_override(
        "pressed",
        _button_style(
            Color(_tokens.accent.r, _tokens.accent.g, _tokens.accent.b, 0.48),
            Color.TRANSPARENT,
            true,
            0
        )
    )
    button.add_theme_font_size_override("font_size", 24)

func _dpad_contains_point(screen_position: Vector2) -> bool:
    if dpad_backdrop == null or not dpad_backdrop.is_visible_in_tree():
        return false
    var backdrop_rect := dpad_backdrop.get_global_rect()
    return screen_position.distance_to(backdrop_rect.get_center()) \
        <= minf(backdrop_rect.size.x, backdrop_rect.size.y) * 0.5

func _update_dpad(screen_position: Vector2) -> void:
    if _dpad_travel_radius <= 0.0:
        return
    var offset := screen_position - dpad_backdrop.get_global_rect().get_center()
    if offset.length() > _dpad_travel_radius:
        offset = offset.normalized() * _dpad_travel_radius
    _dpad_vector = offset / _dpad_travel_radius
    _position_dpad_center()
    _set_dpad_keys(_dpad_vector)

func _position_dpad_center() -> void:
    if dpad_center == null:
        return
    dpad_center.position = (
        _dpad_home_position + _dpad_vector * _dpad_travel_radius
    )

func _set_dpad_keys(direction_vector: Vector2) -> void:
    var desired := {}
    if direction_vector.length() >= DPAD_DEAD_ZONE_RATIO:
        var direction := direction_vector.normalized()
        if direction.y <= -DPAD_DIRECTION_AXIS_THRESHOLD:
            desired[VK_W] = true
        if direction.x <= -DPAD_DIRECTION_AXIS_THRESHOLD:
            desired[VK_A] = true
        if direction.y >= DPAD_DIRECTION_AXIS_THRESHOLD:
            desired[VK_S] = true
        if direction.x >= DPAD_DIRECTION_AXIS_THRESHOLD:
            desired[VK_D] = true

    for key_variant in _dpad_held_keys.keys().duplicate():
        var key_code := int(key_variant)
        if not desired.has(key_code):
            _dpad_held_keys.erase(key_code)
            _release_key(key_code)
    for key_variant in desired.keys():
        var key_code := int(key_variant)
        if not _dpad_held_keys.has(key_code):
            _dpad_held_keys[key_code] = true
            _press_key(key_code)
    _sync_dpad_button_highlights(desired)

func _sync_dpad_button_highlights(desired: Dictionary) -> void:
    if w_button != null:
        w_button.set_pressed_no_signal(desired.has(VK_W))
    if a_button != null:
        a_button.set_pressed_no_signal(desired.has(VK_A))
    if s_button != null:
        s_button.set_pressed_no_signal(desired.has(VK_S))
    if d_button != null:
        d_button.set_pressed_no_signal(desired.has(VK_D))

func _reset_dpad() -> void:
    _set_dpad_keys(Vector2.ZERO)
    _dpad_vector = Vector2.ZERO
    _position_dpad_center()

func _apply_edge_menu_style(button: Button) -> void:
    button.text = ""
    button.tooltip_text = "Menu"
    button.add_theme_stylebox_override(
        "normal", _edge_menu_style(
            Color(0.10, 0.10, 0.11, 0.88),
            Color(0.82, 0.84, 0.88, 0.78)
        )
    )
    button.add_theme_stylebox_override(
        "hover", _edge_menu_style(
            Color(0.18, 0.19, 0.21, 0.96),
            Color(0.92, 0.93, 0.95, 0.94)
        )
    )
    button.add_theme_stylebox_override(
        "pressed", _edge_menu_style(
            Color(_tokens.accent.r, _tokens.accent.g, _tokens.accent.b, 0.82),
            Color(0.95, 0.97, 1.0, 0.98)
        )
    )

func _edge_menu_style(fill: Color, border: Color) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = fill
    style.border_color = border
    style.border_width_left = 2
    style.border_width_top = 2
    style.border_width_bottom = 2
    style.border_width_right = 0
    style.corner_radius_top_left = 999
    style.corner_radius_bottom_left = 999
    style.corner_radius_top_right = 0
    style.corner_radius_bottom_right = 0
    style.shadow_color = Color(0.0, 0.0, 0.0, 0.32)
    style.shadow_size = 3
    style.shadow_offset = Vector2(-2.0, 1.0)
    return style

func _attach_edge_menu_icon(button: Button) -> void:
    var icon := Control.new()
    icon.name = "MenuIcon"
    icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
    button.add_child(icon)

    for row in range(2):
        var track := Panel.new()
        track.name = "Track%d" % row
        track.mouse_filter = Control.MOUSE_FILTER_IGNORE
        var track_style := StyleBoxFlat.new()
        track_style.bg_color = Color(0.10, 0.10, 0.11, 0.72)
        track_style.border_color = Color(0.92, 0.93, 0.95, 0.94)
        track_style.set_border_width_all(2)
        track_style.set_corner_radius_all(999)
        track.add_theme_stylebox_override("panel", track_style)
        icon.add_child(track)

        var knob := Panel.new()
        knob.name = "Knob%d" % row
        knob.mouse_filter = Control.MOUSE_FILTER_IGNORE
        knob.add_theme_stylebox_override(
            "panel",
            _button_style(
                Color(0.92, 0.93, 0.95, 1.0),
                Color(0.10, 0.10, 0.11, 0.92),
                true,
                1
            )
        )
        icon.add_child(knob)

func _layout_edge_menu_icon() -> void:
    var icon := menu_button.get_node_or_null("MenuIcon") as Control
    if icon == null:
        return
    var icon_size := Vector2(
        minf(28.0, menu_button.size.x * 0.56),
        minf(30.0, menu_button.size.y * 0.62)
    )
    icon.position = (menu_button.size - icon_size) * 0.5
    icon.size = icon_size
    var track_height := maxf(8.0, icon_size.y * 0.28)
    var knob_size := track_height - 2.0
    for row in range(2):
        var track := icon.get_node("Track%d" % row) as Panel
        var knob := icon.get_node("Knob%d" % row) as Panel
        var row_y := 1.0 if row == 0 else icon_size.y - track_height - 1.0
        track.position = Vector2(0.0, row_y)
        track.size = Vector2(icon_size.x, track_height)
        knob.position = Vector2(
            icon_size.x - knob_size - 1.0 if row == 0 else 1.0,
            row_y + 1.0
        )
        knob.size = Vector2.ONE * knob_size

func _layout_menu_options(diameter: float, gap: float) -> void:
    var option_size := Vector2(maxf(108.0, diameter * 2.15), menu_button.size.y)
    var option_x := menu_button.position.x - option_size.x - gap
    var options_height := option_size.y * 2.0 + gap
    var options_top := clampf(
        menu_button.position.y,
        _safe_rect.position.y,
        _safe_rect.end.y - options_height
    )
    keyboard_button.position = Vector2(option_x, options_top)
    keyboard_button.size = option_size
    virtual_controls_button.position = Vector2(
        option_x,
        options_top + option_size.y + gap
    )
    virtual_controls_button.size = option_size

func _apply_menu_option_style(button: Button) -> void:
    for state in ["normal", "hover", "pressed"]:
        var fill := Color(0.08, 0.08, 0.09, 0.82)
        if state == "hover":
            fill = Color(0.16, 0.16, 0.17, 0.9)
        elif state == "pressed":
            fill = Color(_tokens.accent.r, _tokens.accent.g, _tokens.accent.b, 0.9)
        button.add_theme_stylebox_override(
            state,
            _button_style(fill, REFERENCE_BORDER, false, 2)
        )

func _apply_input_mode_style(button: Button) -> void:
    button.add_theme_font_size_override("font_size", 13)
    button.add_theme_stylebox_override(
        "normal",
        _button_style(Color(0.08, 0.10, 0.12, 0.88), REFERENCE_BORDER, false)
    )
    button.add_theme_stylebox_override(
        "hover",
        _button_style(Color(0.14, 0.18, 0.21, 0.94), Color.WHITE, false)
    )
    button.add_theme_stylebox_override(
        "pressed",
        _button_style(
            Color(_tokens.accent.r, _tokens.accent.g, _tokens.accent.b, 0.9),
            Color.WHITE,
            false
        )
    )

func _attach_mouse_icon(button: Button, highlights_left: bool) -> void:
    var icon := Control.new()
    icon.name = "MouseIcon"
    icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
    icon.set_meta("highlights_left", highlights_left)
    button.add_child(icon)

    var base := Panel.new()
    base.name = "Base"
    base.mouse_filter = Control.MOUSE_FILTER_IGNORE
    var base_style := StyleBoxFlat.new()
    base_style.bg_color = Color(0.84, 0.85, 0.87, 0.82)
    base_style.corner_radius_top_left = 12
    base_style.corner_radius_top_right = 12
    base_style.corner_radius_bottom_left = 7
    base_style.corner_radius_bottom_right = 7
    base.add_theme_stylebox_override("panel", base_style)
    icon.add_child(base)

    var highlight := Panel.new()
    highlight.name = "Highlight"
    highlight.mouse_filter = Control.MOUSE_FILTER_IGNORE
    var highlight_style := StyleBoxFlat.new()
    highlight_style.bg_color = Color(0.31, 0.60, 1.0, 0.95)
    highlight.add_theme_stylebox_override("panel", highlight_style)
    icon.add_child(highlight)

    var shell := Panel.new()
    shell.name = "Shell"
    shell.mouse_filter = Control.MOUSE_FILTER_IGNORE
    var shell_style := StyleBoxFlat.new()
    shell_style.bg_color = Color.TRANSPARENT
    shell_style.border_color = Color(0.22, 0.23, 0.25, 0.92)
    shell_style.set_border_width_all(2)
    shell_style.corner_radius_top_left = 12
    shell_style.corner_radius_top_right = 12
    shell_style.corner_radius_bottom_left = 7
    shell_style.corner_radius_bottom_right = 7
    shell.add_theme_stylebox_override("panel", shell_style)
    icon.add_child(shell)

    var divider := ColorRect.new()
    divider.name = "Divider"
    divider.color = Color(0.22, 0.23, 0.25, 0.9)
    divider.mouse_filter = Control.MOUSE_FILTER_IGNORE
    icon.add_child(divider)

    var wheel := Panel.new()
    wheel.name = "Wheel"
    wheel.mouse_filter = Control.MOUSE_FILTER_IGNORE
    wheel.add_theme_stylebox_override(
        "panel",
        _button_style(
            Color(0.72, 0.74, 0.77, 1.0),
            Color(0.22, 0.23, 0.25, 0.9),
            true,
            1
        )
    )
    icon.add_child(wheel)

func _layout_mouse_icon(button: Button) -> void:
    var icon := button.get_node_or_null("MouseIcon") as Control
    if icon == null:
        return
    var icon_size := Vector2(button.size.x * 0.46, button.size.y * 0.60)
    icon.position = (button.size - icon_size) * 0.5
    icon.size = icon_size

    var highlight := icon.get_node("Highlight") as Panel
    var base := icon.get_node("Base") as Panel
    var shell := icon.get_node("Shell") as Panel
    var divider := icon.get_node("Divider") as ColorRect
    var wheel := icon.get_node("Wheel") as Panel
    var highlights_left := bool(icon.get_meta("highlights_left", true))
    var top_height := icon_size.y * 0.46
    base.position = Vector2.ZERO
    base.size = icon_size
    highlight.position = Vector2(
        2 if highlights_left else icon_size.x * 0.5,
        2
    )
    highlight.size = Vector2(icon_size.x * 0.5 - 2, top_height - 2)
    shell.position = Vector2.ZERO
    shell.size = icon_size
    divider.position = Vector2(icon_size.x * 0.5 - 0.75, 1)
    divider.size = Vector2(1.5, top_height)
    wheel.position = Vector2(icon_size.x * 0.5 - 2.0, icon_size.y * 0.1)
    wheel.size = Vector2(4.0, icon_size.y * 0.22)

func _attach_pointer_visual(pointer: Control) -> void:
    pointer.set_meta("visual_style", "computer_use")
    pointer.set_meta("hotspot", CURSOR_HOTSPOT)
    var points := PackedVector2Array([
        Vector2(15, 10),
        Vector2(15, 29),
        Vector2(19.5, 24.5),
        Vector2(24, 33),
        Vector2(27.5, 31),
        Vector2(23, 23),
        Vector2(32, 23),
    ])
    var arrow := Polygon2D.new()
    arrow.name = "Arrow"
    arrow.polygon = points
    arrow.color = COMPUTER_USE_CURSOR_FILL
    arrow.antialiased = true
    pointer.add_child(arrow)
    var outline := Line2D.new()
    outline.name = "Outline"
    outline.points = points
    outline.closed = true
    outline.width = 1.5
    outline.antialiased = true
    outline.default_color = COMPUTER_USE_CURSOR_OUTLINE
    pointer.add_child(outline)

func _button_style(
    fill: Color,
    border: Color,
    circular: bool,
    border_width: int = 2
) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = fill
    style.border_color = border
    style.set_border_width_all(border_width)
    style.set_corner_radius_all(999 if circular else 12)
    return style

func _begin_menu_drag(pointer_y: float) -> void:
    _menu_drag_start_pointer_y = pointer_y
    _menu_drag_grab_offset_y = pointer_y - menu_button.position.y
    _menu_drag_moved = false

func _drag_menu_to(pointer_y: float) -> void:
    if not _menu_drag_moved:
        _menu_drag_moved = absf(
            pointer_y - _menu_drag_start_pointer_y
        ) >= MENU_DRAG_THRESHOLD
    if not _menu_drag_moved:
        return
    var min_y := _safe_rect.position.y
    var max_y := _safe_rect.end.y - menu_button.size.y
    menu_button.position.y = clampf(
        pointer_y - _menu_drag_grab_offset_y,
        min_y,
        max_y
    )
    menu_button.position.x = _safe_rect.end.x - menu_button.size.x
    var menu_y_range := maxf(0.0, max_y - min_y)
    _menu_y_ratio = (
        (menu_button.position.y - min_y) / menu_y_range
        if menu_y_range > 0.0
        else 0.0
    )
    var diameter := clampf(
        minf(_safe_rect.size.x, _safe_rect.size.y) * 0.13,
        BUTTON_MIN_SIZE,
        BUTTON_MAX_SIZE
    )
    var gap := clampf(diameter * 0.13, 6.0, 9.0)
    _layout_menu_options(diameter, gap)

func _finish_menu_drag() -> void:
    if _menu_drag_moved:
        call_deferred("_clear_menu_drag_flag")

func _clear_menu_drag_flag() -> void:
    if _menu_touch_index == -1 and not _menu_mouse_dragging:
        _menu_drag_moved = false

func _toggle_menu() -> void:
    if not _enabled or not _menu_button_enabled:
        return
    if _menu_drag_moved:
        _menu_drag_moved = false
        return
    if _panel_open:
        release_all()
        _reset_cursor_touch_gesture()
        _mouse_mode_blocked_touch_indices.clear()
        _panel_open = false
        _menu_open = true
    else:
        _menu_open = not _menu_open
    _sync_visibility()

func _request_keyboard() -> void:
    if not _enabled or not _menu_button_enabled:
        return
    release_all()
    _reset_cursor_touch_gesture()
    _mouse_mode_blocked_touch_indices.clear()
    _menu_open = false
    _panel_open = false
    _sync_visibility()
    keyboard_requested.emit()

func _toggle_input_mode() -> void:
    if not _enabled or not _panel_open:
        return
    set_input_mode(
        INPUT_MODE_TOUCH if _input_mode == INPUT_MODE_MOUSE else INPUT_MODE_MOUSE,
        true
    )

func _sync_input_mode_presentation() -> void:
    if input_mode_button == null:
        return
    var touch_mode := is_touch_mode()
    input_mode_button.text = "Mode: Touch" if touch_mode else "Mode: Mouse"
    input_mode_button.tooltip_text = (
        "Direct touch mode" if touch_mode else "Virtual mouse mode"
    )
    input_mode_button.set_meta("input_mode", _input_mode)

func _sync_visibility() -> void:
    if menu_button == null:
        return
    var controls_visible := _enabled and _menu_button_enabled
    menu_button.visible = controls_visible
    keyboard_button.visible = controls_visible and _menu_open
    virtual_controls_button.visible = controls_visible and _menu_open
    for control in _panel_controls:
        control.visible = controls_visible and _panel_open
    var show_virtual_mouse := controls_visible and _panel_open and not is_touch_mode()
    mouse_left_button.visible = show_virtual_mouse
    mouse_right_button.visible = show_virtual_mouse
    cursor_handle.visible = show_virtual_mouse

func _apply_keyboard_controls_opacity() -> void:
    for control in _panel_controls:
        # The pointer must remain precise even when the surrounding virtual
        # key caps are intentionally subdued.
        var opacity := 1.0 if control == cursor_handle else _keyboard_controls_opacity
        control.self_modulate = Color(1.0, 1.0, 1.0, opacity)

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

func _press_scroll(delta_y: float) -> void:
    if (
        not _enabled
        or not _panel_open
        or not is_zero_approx(_held_scroll_direction)
    ):
        return
    _held_scroll_direction = delta_y
    _scroll(delta_y)
    _scroll_next_repeat_msec = (
        Time.get_ticks_msec() + SCROLL_HOLD_DELAY_MSEC
    )

func _release_scroll(delta_y: float) -> void:
    if not is_equal_approx(_held_scroll_direction, delta_y):
        return
    _cancel_scroll_hold()

func _cancel_scroll_hold() -> void:
    _held_scroll_direction = 0.0
    _scroll_next_repeat_msec = 0

func _process(_delta: float) -> void:
    _advance_scroll_hold(Time.get_ticks_msec())

func _advance_scroll_hold(now_msec: int) -> void:
    if (
        is_zero_approx(_held_scroll_direction)
        or now_msec < _scroll_next_repeat_msec
    ):
        return
    var emitted := 0
    while (
        now_msec >= _scroll_next_repeat_msec
        and emitted < SCROLL_HOLD_MAX_CATCH_UP_STEPS
    ):
        _scroll(_held_scroll_direction)
        _scroll_next_repeat_msec += SCROLL_HOLD_REPEAT_MSEC
        emitted += 1
    if now_msec >= _scroll_next_repeat_msec:
        _scroll_next_repeat_msec = now_msec + SCROLL_HOLD_REPEAT_MSEC

func _move_cursor_by(screen_delta: Vector2) -> void:
    var previous := cursor_screen_position()
    cursor_handle.position += screen_delta
    _clamp_cursor()
    var current := cursor_screen_position()
    var delta := current - previous
    if not delta.is_zero_approx():
        pointer_move_requested.emit(current, delta)

func _begin_cursor_touch(index: int, screen_position: Vector2) -> void:
    _cursor_touch_index = index
    _cursor_touch_start_screen_position = screen_position
    _cursor_touch_down_msec = Time.get_ticks_msec()
    _cursor_touch_tap_candidate = true
    _cursor_touch_released = false
    _cursor_secondary_touch_index = -1
    _cursor_secondary_touch_start_screen_position = Vector2.ZERO
    _cursor_secondary_touch_released = false
    _cursor_two_finger_tap_candidate = false
    _begin_cursor_drag(screen_position)

func _begin_cursor_secondary_touch(index: int, screen_position: Vector2) -> void:
    if _cursor_secondary_touch_index != -1:
        _cancel_cursor_two_finger_tap()
        _mouse_mode_blocked_touch_indices[index] = true
        return
    var age_msec := Time.get_ticks_msec() - _cursor_touch_down_msec
    if (
        not _cursor_touch_tap_candidate
        or age_msec > CURSOR_TWO_FINGER_TAP_WINDOW_MSEC
    ):
        _cursor_touch_tap_candidate = false
        _mouse_mode_blocked_touch_indices[index] = true
        return
    _cursor_touch_tap_candidate = false
    _cursor_secondary_touch_index = index
    _cursor_secondary_touch_start_screen_position = screen_position
    _cursor_secondary_touch_released = false
    _cursor_two_finger_tap_candidate = true

func _update_cursor_primary_touch(screen_position: Vector2) -> void:
    var moved_distance := screen_position.distance_to(
        _cursor_touch_start_screen_position
    )
    if _cursor_two_finger_tap_candidate:
        if moved_distance >= CURSOR_TAP_DRAG_THRESHOLD:
            _cancel_cursor_two_finger_tap()
            _drag_cursor_to(screen_position)
        return
    if moved_distance >= CURSOR_TAP_DRAG_THRESHOLD:
        _cursor_touch_tap_candidate = false
    _drag_cursor_to(screen_position)

func _update_cursor_secondary_touch(screen_position: Vector2) -> void:
    if (
        _cursor_two_finger_tap_candidate
        and screen_position.distance_to(
            _cursor_secondary_touch_start_screen_position
        ) >= CURSOR_TAP_DRAG_THRESHOLD
    ):
        _cancel_cursor_two_finger_tap()

func _finish_cursor_primary_touch() -> void:
    _cursor_touch_index = -1
    _cursor_touch_released = true
    _finish_cursor_drag_if_inactive()
    if _cursor_two_finger_tap_candidate:
        _finish_cursor_two_finger_tap_if_ready()
        return
    var should_click := _cursor_touch_tap_candidate
    _reset_cursor_touch_gesture()
    if should_click:
        _click_cursor_button(0, 0)

func _finish_cursor_secondary_touch() -> void:
    _cursor_secondary_touch_index = -1
    _cursor_secondary_touch_released = true
    _finish_cursor_two_finger_tap_if_ready()

func _finish_cursor_two_finger_tap_if_ready() -> void:
    if (
        not _cursor_two_finger_tap_candidate
        or not _cursor_touch_released
        or not _cursor_secondary_touch_released
    ):
        return
    _reset_cursor_touch_gesture()
    _click_cursor_button(1, POINTER_MOD_RIGHT)

func _cancel_cursor_two_finger_tap() -> void:
    _cursor_two_finger_tap_candidate = false
    _cursor_touch_tap_candidate = false
    if _cursor_secondary_touch_index != -1:
        _mouse_mode_blocked_touch_indices[_cursor_secondary_touch_index] = true
    _cursor_secondary_touch_index = -1
    _cursor_secondary_touch_released = false
    _cursor_secondary_touch_start_screen_position = Vector2.ZERO
    if _cursor_touch_released:
        _reset_cursor_touch_gesture()

func _cancel_cursor_tap_for_additional_touch(index: int) -> void:
    if index == _cursor_touch_index or index == _cursor_secondary_touch_index:
        return
    if _cursor_two_finger_tap_candidate:
        _cancel_cursor_two_finger_tap()
    elif _cursor_touch_index != -1:
        _cursor_touch_tap_candidate = false

func _reset_cursor_touch_gesture() -> void:
    _cursor_touch_index = -1
    _cursor_touch_start_screen_position = Vector2.ZERO
    _cursor_touch_down_msec = 0
    _cursor_touch_tap_candidate = false
    _cursor_touch_released = false
    _cursor_secondary_touch_index = -1
    _cursor_secondary_touch_start_screen_position = Vector2.ZERO
    _cursor_secondary_touch_released = false
    _cursor_two_finger_tap_candidate = false
    if not _cursor_mouse_dragging:
        _reset_cursor_drag_position()

func _click_cursor_button(button: int, modifiers: int) -> void:
    if (
        not _enabled
        or not _panel_open
        or is_touch_mode()
        or _held_mouse_buttons.has(modifiers)
    ):
        return
    _press_mouse_button(button, modifiers)
    _release_mouse_button(button, modifiers)

func _begin_cursor_drag(screen_position: Vector2) -> void:
    _cursor_drag_last_screen_position = screen_position
    _cursor_drag_has_position = true

func _drag_cursor_to(screen_position: Vector2) -> void:
    if not _cursor_drag_has_position:
        _begin_cursor_drag(screen_position)
        return
    var screen_delta := screen_position - _cursor_drag_last_screen_position
    _cursor_drag_last_screen_position = screen_position
    _move_cursor_by(screen_delta)

func _finish_cursor_drag_if_inactive() -> void:
    if _cursor_touch_index == -1 and not _cursor_mouse_dragging:
        _reset_cursor_drag_position()

func _reset_cursor_drag_position() -> void:
    _cursor_drag_has_position = false
    _cursor_drag_last_screen_position = Vector2.ZERO

func _clamp_cursor() -> void:
    if cursor_handle == null or _safe_rect.size == Vector2.ZERO:
        return
    cursor_handle.position.x = clampf(
        cursor_handle.position.x,
        _safe_rect.position.x - CURSOR_HOTSPOT.x,
        _safe_rect.end.x - CURSOR_HOTSPOT.x
    )
    cursor_handle.position.y = clampf(
        cursor_handle.position.y,
        _safe_rect.position.y,
        _safe_rect.end.y
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

func _visible_interactive_control_at(screen_position: Vector2) -> bool:
    for control in _interactive_controls:
        if (
            control.is_visible_in_tree()
            and control.get_global_rect().has_point(screen_position)
        ):
            return true
    return false
