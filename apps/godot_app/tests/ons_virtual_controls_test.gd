extends SceneTree

const OnsVirtualControls = preload("res://scripts/ons_virtual_controls.gd")
const AetherDesignTokens = preload("res://scripts/ui/aether_design_tokens.gd")

var _events: Array[Dictionary] = []

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    var controls = OnsVirtualControls.new()
    root.add_child(controls)
    controls.setup(AetherDesignTokens.new())
    controls.key_event_requested.connect(func(
        pressed: bool,
        key_code: int,
        modifiers: int
    ):
        _events.append({
            "pressed": pressed,
            "key_code": key_code,
            "modifiers": modifiers,
        })
    )
    controls.layout(
        Vector2(844, 390),
        Rect2(Vector2(44, 0), Vector2(756, 369))
    )
    controls.set_enabled(true)
    await process_frame

    if controls.escape_button.text != "Esc" or controls.control_button.text != "Ctrl":
        _fail("virtual key labels are missing")
        return
    var safe_rect := Rect2(Vector2(44, 0), Vector2(756, 369))
    if not safe_rect.encloses(controls.escape_button.get_global_rect()):
        _fail("Esc button escaped the mobile safe area")
        return
    if not safe_rect.encloses(controls.control_button.get_global_rect()):
        _fail("Ctrl button escaped the mobile safe area")
        return

    var inside := InputEventScreenTouch.new()
    inside.position = controls.escape_button.get_global_rect().get_center()
    if not controls.routes_pointer(inside):
        _fail("pointer on Esc was allowed through to the game")
        return
    var outside := InputEventScreenTouch.new()
    outside.position = safe_rect.get_center()
    if controls.routes_pointer(outside):
        _fail("pointer outside the controls was captured")
        return

    controls.escape_button.emit_signal("button_down")
    controls.escape_button.emit_signal("button_down")
    controls.escape_button.emit_signal("button_up")
    controls.control_button.emit_signal("button_down")

    var held_drag := InputEventScreenDrag.new()
    held_drag.position = safe_rect.get_center()
    if not controls.routes_pointer(held_drag):
        _fail("held Ctrl drag leaked through to the game")
        return

    controls.set_enabled(false)
    var expected := [
        {"pressed": true, "key_code": 0x1B, "modifiers": 0},
        {"pressed": false, "key_code": 0x1B, "modifiers": 0},
        {"pressed": true, "key_code": 0x11, "modifiers": 0x04},
        {"pressed": false, "key_code": 0x11, "modifiers": 0},
    ]
    if _events != expected:
        _fail("unexpected key sequence: %s" % [_events])
        return
    if controls.routes_pointer(inside):
        _fail("hidden controls still captured input")
        return

    print("ons_virtual_controls_test: PASS")
    quit(0)

func _fail(message: String) -> void:
    push_error("ons_virtual_controls_test: %s" % message)
    quit(1)
