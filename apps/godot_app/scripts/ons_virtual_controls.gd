extends CanvasLayer

signal key_event_requested(pressed: bool, key_code: int, modifiers: int)

const VK_ESCAPE := 0x1B
const VK_CONTROL := 0x11
const MOD_CONTROL := 0x04
const OVERLAY_LAYER := 120
const BUTTON_MIN_SIZE := 56.0
const BUTTON_MAX_SIZE := 76.0

var escape_button: Button
var control_button: Button

var _root: Control
var _held_keys := {}

func setup(tokens) -> void:
    if _root != null:
        return
    name = "OnsVirtualControls"
    layer = OVERLAY_LAYER

    _root = Control.new()
    _root.name = "Root"
    _root.set_anchors_preset(Control.PRESET_FULL_RECT)
    _root.mouse_filter = Control.MOUSE_FILTER_IGNORE
    add_child(_root)

    escape_button = _create_button("EscapeButton", "Esc", VK_ESCAPE, tokens)
    control_button = _create_button("ControlButton", "Ctrl", VK_CONTROL, tokens)
    _root.add_child(escape_button)
    _root.add_child(control_button)
    set_enabled(false)

func set_enabled(enabled: bool) -> void:
    if not enabled:
        release_all()
    visible = enabled

func release_all() -> void:
    for key_variant in _held_keys.keys().duplicate():
        _release_key(int(key_variant))

func layout(_window_size: Vector2, safe_rect: Rect2) -> void:
    if _root == null:
        return

    var short_edge := minf(safe_rect.size.x, safe_rect.size.y)
    var diameter := clampf(short_edge * 0.145, BUTTON_MIN_SIZE, BUTTON_MAX_SIZE)
    var margin := clampf(diameter * 0.24, 12.0, 18.0)
    var button_size := Vector2(diameter, diameter)
    var safe_end := safe_rect.position + safe_rect.size

    escape_button.position = safe_rect.position + Vector2(margin, margin)
    escape_button.size = button_size
    control_button.position = Vector2(
        safe_rect.position.x + margin,
        safe_end.y - margin - diameter
    )
    control_button.size = button_size

func routes_pointer(event: InputEvent) -> bool:
    if not visible or _root == null:
        return false
    if not _held_keys.is_empty() and (
        event is InputEventScreenTouch
        or event is InputEventScreenDrag
        or event is InputEventMouseButton
        or event is InputEventMouseMotion
    ):
        return true

    var position := _event_position(event)
    if position.x < 0.0 or position.y < 0.0:
        return false
    return (
        escape_button.get_global_rect().has_point(position)
        or control_button.get_global_rect().has_point(position)
    )

func _create_button(
    node_name: String,
    label: String,
    key_code: int,
    tokens
) -> Button:
    var button := Button.new()
    button.name = node_name
    button.text = label
    button.focus_mode = Control.FOCUS_NONE
    button.mouse_filter = Control.MOUSE_FILTER_STOP
    button.keep_pressed_outside = true
    button.add_theme_font_size_override("font_size", 18)
    button.add_theme_color_override("font_color", Color(1.0, 1.0, 1.0, 0.94))
    button.add_theme_color_override("font_hover_color", Color.WHITE)
    button.add_theme_color_override("font_pressed_color", Color.WHITE)
    button.add_theme_stylebox_override(
        "normal",
        _button_style(Color(0.035, 0.035, 0.04, 0.48), Color(1.0, 1.0, 1.0, 0.42))
    )
    button.add_theme_stylebox_override(
        "hover",
        _button_style(Color(0.08, 0.08, 0.09, 0.68), Color(1.0, 1.0, 1.0, 0.68))
    )
    button.add_theme_stylebox_override(
        "pressed",
        _button_style(Color(tokens.accent.r, tokens.accent.g, tokens.accent.b, 0.82), Color.WHITE)
    )
    button.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
    button.button_down.connect(_press_key.bind(key_code))
    button.button_up.connect(_release_key.bind(key_code))
    return button

func _button_style(fill: Color, border: Color) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = fill
    style.border_color = border
    style.set_border_width_all(2)
    style.set_corner_radius_all(999)
    style.shadow_color = Color(0.0, 0.0, 0.0, 0.24)
    style.shadow_size = 8
    style.shadow_offset = Vector2(0, 3)
    return style

func _press_key(key_code: int) -> void:
    if not visible or _held_keys.has(key_code):
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
