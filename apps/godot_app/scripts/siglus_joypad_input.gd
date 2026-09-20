extends RefCounted

# Raw keys preserve the 32-button Siglus domain. Optional VK bindings make
# ordinary decide/cancel/navigation work as well as games querying JOYPAD.
const CONFIG_PATH := "user://siglus_joypad.cfg"
const DEFAULT_BINDINGS := {
    JOY_BUTTON_A: 0x0d, JOY_BUTTON_B: 0x1b, JOY_BUTTON_X: 0x20,
    JOY_BUTTON_Y: 0x11, JOY_BUTTON_LEFT_SHOULDER: 0x21,
    JOY_BUTTON_RIGHT_SHOULDER: 0x22, JOY_BUTTON_BACK: 0x09,
    JOY_BUTTON_START: 0x1b, JOY_BUTTON_DPAD_UP: 0x26,
    JOY_BUTTON_DPAD_DOWN: 0x28, JOY_BUTTON_DPAD_LEFT: 0x25,
    JOY_BUTTON_DPAD_RIGHT: 0x27,
}
var bindings: Dictionary = DEFAULT_BINDINGS.duplicate()
var held_sources: Dictionary = {}
var counts: Dictionary = {}

func load_config() -> void:
    var config := ConfigFile.new()
    if config.load(CONFIG_PATH) != OK:
        return
    for button in range(32):
        var value := int(config.get_value("buttons", str(button), bindings.get(button, 0)))
        if value >= 0 and value <= 255:
            bindings[button] = value

func save_config() -> Error:
    var config := ConfigFile.new()
    for button in range(32):
        config.set_value("buttons", str(button), int(bindings.get(button, 0)))
    return config.save(CONFIG_PATH)

func _button(device: int, source: String, button: int, down: bool) -> Array:
    var events: Array = []
    var source_id := "%d:%s" % [device, source]
    if down == held_sources.has(source_id):
        return events
    var keys: Array = []
    if down:
        var vk := int(bindings.get(button, 0))
        if vk > 0:
            keys.append(vk)
        keys.append(0x200 + button) # Keep joypad as the last active input family.
        held_sources[source_id] = keys
    else:
        keys = held_sources[source_id]
        held_sources.erase(source_id)
    for key in keys:
        var before := int(counts.get(key, 0))
        var after := before + (1 if down else -1)
        if after == 0:
            counts.erase(key)
        else:
            counts[key] = after
        if before == 0 or after == 0:
            events.append([key, down])
    return events

func translate(event: InputEvent) -> Array:
    if event is InputEventJoypadButton:
        var button := event as InputEventJoypadButton
        if button.button_index >= 0 and button.button_index < 32:
            return _button(button.device, "b%d" % button.button_index, button.button_index, button.pressed)
    if event is InputEventJoypadMotion:
        var motion := event as InputEventJoypadMotion
        if motion.axis not in [JOY_AXIS_LEFT_X, JOY_AXIS_LEFT_Y]:
            return []
        var output: Array = []
        for direction in [-1, 1]:
            var source := "a%d:%d" % [motion.axis, direction]
            var held := held_sources.has("%d:%s" % [motion.device, source])
            var threshold := 0.35 if held else 0.55
            var button := JOY_BUTTON_DPAD_RIGHT if direction > 0 else JOY_BUTTON_DPAD_LEFT
            if motion.axis == JOY_AXIS_LEFT_Y:
                button = JOY_BUTTON_DPAD_DOWN if direction > 0 else JOY_BUTTON_DPAD_UP
            output.append_array(_button(motion.device, source, button, motion.axis_value * direction >= threshold))
        return output
    return []

func release_device(device: int) -> Array:
    var output: Array = []
    for source_id in held_sources.keys():
        if String(source_id).begins_with("%d:" % device):
            output.append_array(_button(device, String(source_id).get_slice(":", 1) + (
                ":" + String(source_id).get_slice(":", 2) if String(source_id).count(":") > 1 else ""
            ), 0, false))
    return output

func release_all() -> Array:
    var output: Array = []
    for key in counts:
        output.append([key, false])
    counts.clear()
    held_sources.clear()
    return output
