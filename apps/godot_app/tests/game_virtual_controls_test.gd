extends SceneTree

const GameVirtualControls = preload("res://scripts/game_virtual_controls.gd")
const AetherDesignTokens = preload("res://scripts/ui/aether_design_tokens.gd")

var _key_events: Array[Dictionary] = []
var _pointer_moves: Array[Dictionary] = []
var _pointer_buttons: Array[Dictionary] = []
var _scroll_events: Array[Dictionary] = []
var _keyboard_requests := 0
var _virtual_requests := 0

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    var controls = GameVirtualControls.new()
    root.add_child(controls)
    controls.setup(AetherDesignTokens.new())
    controls.key_event_requested.connect(func(
        pressed: bool,
        key_code: int,
        modifiers: int
    ):
        _key_events.append({
            "pressed": pressed,
            "key_code": key_code,
            "modifiers": modifiers,
        })
    )
    controls.pointer_move_requested.connect(func(position: Vector2, delta: Vector2):
        _pointer_moves.append({"position": position, "delta": delta})
    )
    controls.pointer_button_requested.connect(func(
        pressed: bool,
        button: int,
        modifiers: int,
        position: Vector2
    ):
        _pointer_buttons.append({
            "pressed": pressed,
            "button": button,
            "modifiers": modifiers,
            "position": position,
        })
    )
    controls.pointer_scroll_requested.connect(func(delta_y: float, position: Vector2):
        _scroll_events.append({"delta_y": delta_y, "position": position})
    )
    controls.keyboard_requested.connect(func(): _keyboard_requests += 1)
    controls.virtual_controls_requested.connect(func(): _virtual_requests += 1)

    var safe_rect := Rect2(Vector2(44, 0), Vector2(756, 369))
    controls.layout(Vector2(844, 390), safe_rect)
    controls.set_enabled(true)
    await process_frame

    if (
        not controls.menu_button.visible
        or controls.is_menu_open()
        or controls.is_panel_open()
        or controls.keyboard_button.visible
        or controls.virtual_controls_button.visible
        or controls.escape_button.visible
    ):
        _fail("collapsed state must expose only the edge Menu entry")
        return
    if not safe_rect.encloses(controls.menu_button.get_global_rect()):
        _fail("Menu escaped the mobile safe area")
        return
    if not is_equal_approx(
        controls.menu_button.get_global_rect().end.x,
        safe_rect.end.x
    ):
        _fail("Menu is not flush with the safe-area edge")
        return
    if not is_equal_approx(
        controls.menu_button.get_global_rect().position.y,
        safe_rect.position.y
    ):
        _fail("Menu does not start at the top edge")
        return
    if (
        not controls.menu_button.text.is_empty()
        or controls.menu_button.get_node_or_null("MenuIcon") == null
    ):
        _fail("edge Menu must render as an icon without a text label")
        return
    var menu_style := controls.menu_button.get_theme_stylebox(
        "normal"
    ) as StyleBoxFlat
    if (
        menu_style == null
        or menu_style.bg_color.a < 0.75
        or menu_style.corner_radius_top_left < controls.menu_button.size.y * 0.5
        or menu_style.corner_radius_bottom_left < controls.menu_button.size.y * 0.5
        or menu_style.corner_radius_top_right != 0
        or menu_style.corner_radius_bottom_right != 0
    ):
        _fail("edge Menu is missing its visible right-edge semicircle")
        return

    var menu_x: float = controls.menu_button.position.x
    var menu_drag_down := InputEventScreenTouch.new()
    menu_drag_down.index = 3
    menu_drag_down.pressed = true
    menu_drag_down.position = controls.menu_button.get_global_rect().get_center()
    if not controls.routes_pointer(menu_drag_down):
        _fail("Menu drag press was not captured")
        return
    var menu_drag := InputEventScreenDrag.new()
    menu_drag.index = 3
    menu_drag.position = menu_drag_down.position + Vector2(-120, 86)
    if not controls.routes_pointer(menu_drag):
        _fail("Menu drag motion was not captured")
        return
    var menu_drag_up := InputEventScreenTouch.new()
    menu_drag_up.index = 3
    menu_drag_up.pressed = false
    menu_drag_up.position = menu_drag.position
    if not controls.routes_pointer(menu_drag_up):
        _fail("Menu drag release was not captured")
        return
    controls.menu_button.emit_signal("pressed")
    if controls.is_menu_open():
        _fail("dragging Menu unexpectedly activated it")
        return
    await process_frame
    if (
        not is_equal_approx(controls.menu_button.position.x, menu_x)
        or controls.menu_button.position.y <= safe_rect.position.y
    ):
        _fail("Menu drag must change only its Y position")
        return
    controls.menu_button.emit_signal("pressed")
    if (
        not controls.is_menu_open()
        or controls.is_panel_open()
        or not controls.keyboard_button.visible
        or not controls.virtual_controls_button.visible
    ):
        _fail("Menu did not expose keyboard and virtual-control choices")
        return
    if (
        not safe_rect.encloses(controls.keyboard_button.get_global_rect())
        or not safe_rect.encloses(
            controls.virtual_controls_button.get_global_rect()
        )
    ):
        _fail("Menu options escaped the mobile safe area after dragging")
        return
    controls.keyboard_button.emit_signal("pressed")
    if _keyboard_requests != 1 or controls.is_menu_open():
        _fail("soft-keyboard action was not routed")
        return

    controls.menu_button.emit_signal("pressed")
    controls.virtual_controls_button.emit_signal("pressed")
    await process_frame
    if not controls.is_panel_open() or _virtual_requests != 1:
        _fail("virtual-control panel did not open")
        return
    var required_controls: Array[Control] = [
        controls.escape_button, controls.control_button,
        controls.w_button, controls.a_button, controls.s_button, controls.d_button,
        controls.enter_button, controls.space_button,
        controls.mouse_left_button, controls.mouse_right_button,
        controls.scroll_up_button, controls.scroll_down_button,
        controls.cursor_handle,
    ]
    required_controls.append_array(controls.digit_buttons)
    for control in required_controls:
        if not control.visible or not safe_rect.encloses(control.get_global_rect()):
            _fail("required control is hidden or outside safe area: %s" % control.name)
            return

    var reference_circles: Array[Button] = [
        controls.escape_button,
        controls.control_button,
        controls.enter_button,
        controls.space_button,
        controls.mouse_left_button,
        controls.mouse_right_button,
    ]
    reference_circles.append_array(controls.digit_buttons)
    for button in reference_circles:
        if not is_equal_approx(button.size.x, button.size.y):
            _fail("UU-style control is not circular: %s" % button.name)
            return
        var normal_style := button.get_theme_stylebox("normal") as StyleBoxFlat
        if normal_style == null or normal_style.corner_radius_top_left < button.size.x * 0.5:
            _fail("UU-style circular treatment is missing: %s" % button.name)
            return

    var dpad_rect: Rect2 = controls.dpad_backdrop.get_global_rect()
    for direction in [
        controls.w_button,
        controls.a_button,
        controls.s_button,
        controls.d_button,
    ]:
        if not dpad_rect.encloses(direction.get_global_rect()):
            _fail("direction escaped the UU-style circular pad: %s" % direction.name)
            return
    if controls.dpad_backdrop.size.x < controls.w_button.size.x * 2.5:
        _fail("direction pad is not the large UU-style disc")
        return
    if (
        controls.mouse_left_button.get_node_or_null("MouseIcon") == null
        or controls.mouse_right_button.get_node_or_null("MouseIcon") == null
    ):
        _fail("mouse buttons are missing the UU-style mouse icons")
        return
    if (
        controls.wheel_backdrop == null
        or not controls.wheel_backdrop.get_global_rect().encloses(
            controls.scroll_up_button.get_global_rect()
        )
        or not controls.wheel_backdrop.get_global_rect().encloses(
            controls.scroll_down_button.get_global_rect()
        )
    ):
        _fail("scroll controls are not contained in the UU-style wheel")
        return
    var cursor: Control = controls.cursor_handle
    var cursor_glow := cursor.get_node_or_null("Glow") as TextureRect
    var cursor_arrow := cursor.get_node_or_null("Arrow") as Polygon2D
    var cursor_arrow_glow := cursor.get_node_or_null("ArrowGlow") as Line2D
    var cursor_outline := cursor.get_node_or_null("Outline") as Line2D
    if (
        cursor.get_meta("visual_style", "") != "computer_use"
        or cursor_glow == null
        or cursor_arrow == null
        or cursor_arrow_glow == null
        or cursor_outline == null
        or cursor.get_node_or_null("Shadow") != null
    ):
        _fail("draggable cursor does not use the Computer Use visual layers")
        return
    var cursor_glow_texture := cursor_glow.texture as GradientTexture2D
    if (
        cursor_glow_texture == null
        or cursor_glow_texture.fill != GradientTexture2D.FILL_RADIAL
        or cursor_glow_texture.gradient == null
        or cursor_glow_texture.gradient.colors.size() != 4
        or cursor_glow_texture.gradient.colors[0].a < 0.2
        or cursor_glow_texture.gradient.colors[-1].a != 0.0
    ):
        _fail("Computer Use cursor is missing its soft radial blue glow")
        return
    if (
        cursor.size.x < 40.0
        or cursor_arrow.polygon.size() != 7
        or cursor_arrow.color.get_luminance() > 0.2
        or cursor_arrow.color.a < 0.8
        or cursor_outline.default_color.b < 0.8
        or cursor_outline.default_color.a < 0.9
        or cursor_arrow_glow.default_color.b < 0.9
    ):
        _fail("Computer Use cursor arrow shape or palette is incorrect")
        return
    if controls.digit_buttons[3].get_global_rect().intersects(
        controls.menu_button.get_global_rect()
    ):
        _fail("edge Menu overlaps the numeric controls")
        return

    var outside := InputEventScreenTouch.new()
    outside.position = safe_rect.get_center() + Vector2(0, -70)
    if controls.routes_pointer(outside):
        _fail("pointer outside controls was captured")
        return
    var inside := InputEventScreenTouch.new()
    inside.position = controls.escape_button.get_global_rect().get_center()
    if not controls.routes_pointer(inside):
        _fail("pointer on Esc leaked into the game")
        return

    controls.escape_button.emit_signal("button_down")
    controls.escape_button.emit_signal("button_down")
    controls.escape_button.emit_signal("button_up")
    controls.control_button.emit_signal("button_down")
    controls.control_button.emit_signal("button_up")
    controls.w_button.emit_signal("button_down")
    controls.w_button.emit_signal("button_up")
    controls.digit_buttons[3].emit_signal("button_down")
    controls.digit_buttons[3].emit_signal("button_up")
    controls.enter_button.emit_signal("button_down")
    controls.enter_button.emit_signal("button_up")
    controls.space_button.emit_signal("button_down")
    controls.space_button.emit_signal("button_up")
    var expected_keys := [
        {"pressed": true, "key_code": 0x1B, "modifiers": 0},
        {"pressed": false, "key_code": 0x1B, "modifiers": 0},
        {"pressed": true, "key_code": 0x11, "modifiers": 0x04},
        {"pressed": false, "key_code": 0x11, "modifiers": 0},
        {"pressed": true, "key_code": 0x57, "modifiers": 0},
        {"pressed": false, "key_code": 0x57, "modifiers": 0},
        {"pressed": true, "key_code": 0x34, "modifiers": 0},
        {"pressed": false, "key_code": 0x34, "modifiers": 0},
        {"pressed": true, "key_code": 0x0D, "modifiers": 0},
        {"pressed": false, "key_code": 0x0D, "modifiers": 0},
        {"pressed": true, "key_code": 0x20, "modifiers": 0},
        {"pressed": false, "key_code": 0x20, "modifiers": 0},
    ]
    if _key_events != expected_keys:
        _fail("unexpected virtual-key sequence: %s" % [_key_events])
        return

    var cursor_before := controls.cursor_screen_position()
    var cursor_down := InputEventScreenTouch.new()
    cursor_down.index = 7
    cursor_down.pressed = true
    cursor_down.position = cursor_before
    if not controls.routes_pointer(cursor_down):
        _fail("cursor press was not captured")
        return
    var cursor_drag := InputEventScreenDrag.new()
    cursor_drag.index = 7
    cursor_drag.position = cursor_before + Vector2(31, -24)
    if not controls.routes_pointer(cursor_drag) or _pointer_moves.size() != 1:
        _fail("cursor drag did not route a pointer move")
        return
    var cursor_up := InputEventScreenTouch.new()
    cursor_up.index = 7
    cursor_up.pressed = false
    cursor_up.position = cursor_drag.position
    if not controls.routes_pointer(cursor_up):
        _fail("cursor release was not captured")
        return

    controls.mouse_left_button.emit_signal("button_down")
    controls.mouse_left_button.emit_signal("button_up")
    controls.mouse_right_button.emit_signal("button_down")
    controls.mouse_right_button.emit_signal("button_up")
    if _pointer_buttons.size() != 4:
        _fail("mouse buttons did not produce down/up pairs")
        return
    if (
        _pointer_buttons[0].modifiers != 0
        or _pointer_buttons[2].modifiers != 0x10
    ):
        _fail("left/right mouse modifiers are incorrect")
        return
    controls.scroll_up_button.emit_signal("pressed")
    controls.scroll_down_button.emit_signal("pressed")
    if (
        _scroll_events.size() != 2
        or _scroll_events[0].delta_y != -1.0
        or _scroll_events[1].delta_y != 1.0
    ):
        _fail("mouse-wheel directions are incorrect")
        return

    controls.a_button.emit_signal("button_down")
    controls.mouse_right_button.emit_signal("button_down")
    controls.set_enabled(false)
    if _key_events.back() != {"pressed": false, "key_code": 0x41, "modifiers": 0}:
        _fail("hiding controls did not release held key")
        return
    if _pointer_buttons.back().pressed or _pointer_buttons.back().modifiers != 0x10:
        _fail("hiding controls did not release held mouse button")
        return
    if controls.routes_pointer(inside):
        _fail("hidden controls still captured input")
        return

    print("game_virtual_controls_test: PASS")
    quit(0)

func _fail(message: String) -> void:
    push_error("game_virtual_controls_test: %s" % message)
    quit(1)
