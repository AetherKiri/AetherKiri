extends SceneTree

const GameVirtualControls = preload("res://scripts/game_virtual_controls.gd")
const AetherDesignTokens = preload("res://scripts/ui/aether_design_tokens.gd")

var _key_events: Array[Dictionary] = []
var _pointer_moves: Array[Dictionary] = []
var _pointer_buttons: Array[Dictionary] = []
var _scroll_events: Array[Dictionary] = []
var _keyboard_requests := 0
var _virtual_requests := 0
var _input_modes: Array[String] = []

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
    controls.input_mode_changed.connect(func(mode: String): _input_modes.append(mode))

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
        controls.cursor_handle, controls.input_mode_button,
    ]
    required_controls.append_array(controls.digit_buttons)
    for control in required_controls:
        if not control.visible or not safe_rect.encloses(control.get_global_rect()):
            _fail("required control is hidden or outside safe area: %s" % control.name)
            return
    if (
        controls.input_mode() != GameVirtualControls.INPUT_MODE_MOUSE
        or controls.is_touch_mode()
        or controls.input_mode_button.text != "Mode: Mouse"
        or controls.input_mode_button.get_meta("input_mode", "") != "mouse"
    ):
        _fail("virtual controls did not default to mouse mode")
        return

    var reference_circles: Array[Button] = [
        controls.escape_button,
        controls.control_button,
        controls.enter_button,
        controls.space_button,
        controls.mouse_left_button,
        controls.mouse_right_button,
        controls.scroll_up_button,
        controls.scroll_down_button,
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
    var scroll_up_rect: Rect2 = controls.scroll_up_button.get_global_rect()
    var scroll_down_rect: Rect2 = controls.scroll_down_button.get_global_rect()
    if (
        scroll_up_rect.intersects(scroll_down_rect)
        or scroll_up_rect.intersects(controls.escape_button.get_global_rect())
        or scroll_down_rect.intersects(controls.dpad_backdrop.get_global_rect())
        or not is_equal_approx(scroll_up_rect.position.x, scroll_down_rect.position.x)
        or scroll_down_rect.position.y <= scroll_up_rect.end.y
        or controls.scroll_up_button.text != "▲"
        or controls.scroll_down_button.text != "▼"
    ):
        _fail("scroll actions are not two separate vertical buttons")
        return
    var cursor: Control = controls.cursor_handle
    var cursor_arrow := cursor.get_node_or_null("Arrow") as Polygon2D
    var cursor_arrow_glow := cursor.get_node_or_null("ArrowGlow") as Line2D
    var cursor_outline := cursor.get_node_or_null("Outline") as Line2D
    if (
        cursor.get_meta("visual_style", "") != "computer_use"
        or cursor.get_meta("hotspot", Vector2.ZERO) != GameVirtualControls.CURSOR_HOTSPOT
        or cursor.get_node_or_null("Glow") != null
        or cursor_arrow == null
        or cursor_arrow_glow != null
        or cursor_outline == null
        or cursor.get_node_or_null("Shadow") != null
    ):
        _fail("Computer Use cursor must not include glow or shadow layers")
        return
    if (
        cursor.size.x < 40.0
        or cursor_arrow.polygon.size() != 7
        or cursor_arrow.color.get_luminance() > 0.2
        or cursor_arrow.color.a < 0.8
        or cursor_outline.default_color.b < 0.8
        or cursor_outline.default_color.a < 0.9
    ):
        _fail("Computer Use cursor arrow shape or palette is incorrect")
        return
    if controls.digit_buttons[3].get_global_rect().intersects(
        controls.menu_button.get_global_rect()
    ):
        _fail("edge Menu overlaps the numeric controls")
        return
    if not controls.cursor_screen_position().is_equal_approx(
        controls.cursor_handle.get_global_rect().position
        + GameVirtualControls.CURSOR_HOTSPOT
    ):
        _fail("cursor pointer position is not anchored to the arrow tip")
        return

    var cursor_before_full_screen_drag := controls.cursor_screen_position()
    var emulated_down := InputEventMouseButton.new()
    # iOS has reported synthesized mouse tracks with more than one device id.
    # Touch ownership must not depend on recognizing a particular id.
    emulated_down.device = 7
    emulated_down.button_index = MOUSE_BUTTON_LEFT
    emulated_down.pressed = true
    emulated_down.position = safe_rect.get_center() + Vector2(0, -96)
    if not controls.routes_pointer(emulated_down):
        _fail("iOS emulated press was not captured before ScreenTouch")
        return
    var outside := InputEventScreenTouch.new()
    outside.index = 11
    outside.pressed = true
    outside.position = safe_rect.get_center() + Vector2(0, -70)
    if not controls.routes_pointer(outside):
        _fail("mouse mode leaked a direct touch into the game")
        return
    var stale_emulated_motion := InputEventMouseMotion.new()
    stale_emulated_motion.device = 7
    stale_emulated_motion.position = emulated_down.position + Vector2(0, -28)
    if (
        not controls.routes_pointer(stale_emulated_motion)
        or not _pointer_moves.is_empty()
        or not controls.cursor_screen_position().is_equal_approx(
            cursor_before_full_screen_drag
        )
    ):
        _fail("ScreenTouch did not take ownership from the offset emulated track")
        return
    # The same iOS event reaches both Main._input and the game viewport's
    # gui_input callback. Re-routing the press must not classify its own touch
    # index as a blocked second finger.
    if not controls.routes_pointer(outside):
        _fail("mouse mode lost a repeated iOS touch press")
        return
    var outside_drag := InputEventScreenDrag.new()
    outside_drag.index = outside.index
    outside_drag.relative = Vector2(12, 8)
    outside_drag.position = outside.position + Vector2(12, 8)
    if (
        not controls.routes_pointer(outside_drag)
        or _pointer_moves.size() != 1
        or not controls.cursor_screen_position().is_equal_approx(
            cursor_before_full_screen_drag + outside_drag.relative
        )
    ):
        _fail("mouse mode did not move the cursor from the full-screen touchpad")
        return
    if (
        not controls.routes_pointer(outside_drag)
        or _pointer_moves.size() != 1
    ):
        _fail("repeated iOS drag event moved the cursor twice")
        return
    var outside_up := InputEventScreenTouch.new()
    outside_up.index = outside.index
    outside_up.pressed = false
    outside_up.position = outside_drag.position
    if not controls.routes_pointer(outside_up):
        _fail("mouse mode lost its full-screen touchpad release")
        return
    var outside_mouse := InputEventMouseButton.new()
    outside_mouse.device = GameVirtualControls.INPUT_DEVICE_ID_EMULATION
    outside_mouse.button_index = MOUSE_BUTTON_LEFT
    outside_mouse.pressed = true
    outside_mouse.position = outside.position
    if not controls.routes_pointer(outside_mouse):
        _fail("mouse mode leaked a hardware click into the game")
        return
    var outside_mouse_motion := InputEventMouseMotion.new()
    outside_mouse_motion.device = GameVirtualControls.INPUT_DEVICE_ID_EMULATION
    outside_mouse_motion.relative = Vector2(-7, 5)
    outside_mouse_motion.position = outside_mouse.position + outside_mouse_motion.relative
    if (
        not controls.routes_pointer(outside_mouse_motion)
        or _pointer_moves.size() != 2
    ):
        _fail("mouse mode did not support iOS emulated-mouse dragging")
        return
    if (
        not controls.routes_pointer(outside_mouse_motion)
        or _pointer_moves.size() != 2
    ):
        _fail("repeated iOS emulated-mouse motion moved the cursor twice")
        return
    outside_mouse.pressed = false
    outside_mouse.position = outside_mouse_motion.position
    if not controls.routes_pointer(outside_mouse):
        _fail("mouse mode lost the full-screen hardware drag release")
        return
    var inside := InputEventScreenTouch.new()
    inside.pressed = true
    inside.position = controls.escape_button.get_global_rect().get_center()
    if not controls.routes_pointer(inside):
        _fail("pointer on Esc leaked into the game")
        return

    controls.input_mode_button.emit_signal("pressed")
    await process_frame
    if (
        not controls.is_touch_mode()
        or controls.input_mode_button.text != "Mode: Touch"
        or controls.cursor_handle.visible
        or controls.mouse_left_button.visible
        or controls.mouse_right_button.visible
        or not controls.escape_button.visible
        or _input_modes != [GameVirtualControls.INPUT_MODE_TOUCH]
    ):
        _fail("touch mode presentation or change signal is incorrect")
        return
    var direct_touch := InputEventScreenTouch.new()
    direct_touch.index = 12
    direct_touch.pressed = true
    direct_touch.position = outside.position
    if controls.routes_pointer(direct_touch):
        _fail("touch mode did not pass direct game touch through")
        return
    if controls.routes_pointer(outside_mouse):
        _fail("touch mode did not pass direct hardware clicks through")
        return
    var hidden_cursor_touch := InputEventScreenTouch.new()
    hidden_cursor_touch.index = 13
    hidden_cursor_touch.pressed = true
    hidden_cursor_touch.position = controls.cursor_screen_position()
    if controls.routes_pointer(hidden_cursor_touch):
        _fail("hidden touch-mode cursor still captured input")
        return
    controls.input_mode_button.emit_signal("pressed")
    await process_frame
    if (
        controls.is_touch_mode()
        or not controls.cursor_handle.visible
        or not controls.mouse_left_button.visible
        or not controls.mouse_right_button.visible
        or _input_modes != [
            GameVirtualControls.INPUT_MODE_TOUCH,
            GameVirtualControls.INPUT_MODE_MOUSE,
        ]
    ):
        _fail("switching back did not restore mouse-mode controls")
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
    cursor_down.position = safe_rect.position + safe_rect.size * Vector2(0.62, 0.34)
    if controls.cursor_handle.get_global_rect().has_point(cursor_down.position):
        _fail("full-screen cursor test unexpectedly started on the cursor")
        return
    if not controls.routes_pointer(cursor_down):
        _fail("cursor press was not captured")
        return
    var cursor_drag := InputEventScreenDrag.new()
    cursor_drag.index = 7
    cursor_drag.relative = Vector2(31, -24)
    cursor_drag.position = cursor_down.position + cursor_drag.relative
    if (
        not controls.routes_pointer(cursor_drag)
        or _pointer_moves.size() != 3
        or not controls.cursor_screen_position().is_equal_approx(
            cursor_before + cursor_drag.relative
        )
    ):
        _fail("full-screen cursor drag did not route a relative pointer move")
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
    controls.scroll_up_button.emit_signal("button_down")
    controls.scroll_up_button.emit_signal("button_up")
    controls.scroll_down_button.emit_signal("button_down")
    controls.scroll_down_button.emit_signal("button_up")
    if (
        _scroll_events.size() != 2
        or _scroll_events[0].delta_y != 1.0
        or _scroll_events[1].delta_y != -1.0
    ):
        _fail("separate scroll-button directions are incorrect")
        return

    var hold_event_start := _scroll_events.size()
    controls.scroll_up_button.emit_signal("button_down")
    controls._advance_scroll_hold(
        controls._scroll_next_repeat_msec
        + GameVirtualControls.SCROLL_HOLD_REPEAT_MSEC * 2
    )
    controls.scroll_up_button.emit_signal("button_up")
    if _scroll_events.size() - hold_event_start != 4:
        _fail("holding the scroll-up button did not repeat at the expected cadence")
        return
    for index in range(hold_event_start, _scroll_events.size()):
        if _scroll_events[index].delta_y != 1.0:
            _fail("scroll-up button hold produced the wrong direction")
            return
    var released_scroll_count := _scroll_events.size()
    controls._advance_scroll_hold(
        Time.get_ticks_msec() + GameVirtualControls.SCROLL_HOLD_DELAY_MSEC * 2
    )
    if _scroll_events.size() != released_scroll_count:
        _fail("released scroll button kept repeating")
        return

    controls.a_button.emit_signal("button_down")
    controls.mouse_right_button.emit_signal("button_down")
    controls.scroll_down_button.emit_signal("button_down")
    var scroll_count_before_mode_change := _scroll_events.size()
    controls.input_mode_button.emit_signal("pressed")
    if _key_events.back() != {"pressed": false, "key_code": 0x41, "modifiers": 0}:
        _fail("changing modes did not release the held key")
        return
    if _pointer_buttons.back().pressed or _pointer_buttons.back().modifiers != 0x10:
        _fail("changing modes did not release the held mouse button")
        return
    controls._advance_scroll_hold(
        Time.get_ticks_msec() + GameVirtualControls.SCROLL_HOLD_DELAY_MSEC * 2
    )
    if (
        not is_zero_approx(controls._held_scroll_direction)
        or _scroll_events.size() != scroll_count_before_mode_change
    ):
        _fail("changing modes did not release the held scroll button")
        return
    controls.scroll_down_button.emit_signal("button_up")
    controls.input_mode_button.emit_signal("pressed")

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
