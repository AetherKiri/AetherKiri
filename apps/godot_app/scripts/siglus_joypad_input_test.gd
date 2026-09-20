extends SceneTree
const Joypad = preload("res://scripts/siglus_joypad_input.gd")

func _initialize() -> void:
    var pad := Joypad.new()
    var button := InputEventJoypadButton.new()
    button.button_index = JOY_BUTTON_A
    button.pressed = true
    button.device = 0
    assert(pad.translate(button) == [[13, true], [0x200, true]])
    assert(pad.translate(button).is_empty())
    button.device = 1
    assert(pad.translate(button).is_empty())
    # Disconnecting one of two devices must not release the other's hold.
    assert(pad.release_device(0).is_empty())
    assert(pad.release_device(1) == [[13, false], [0x200, false]])
    var axis := InputEventJoypadMotion.new()
    axis.device = 2
    axis.axis = JOY_AXIS_LEFT_X
    axis.axis_value = 0.7
    assert(pad.translate(axis) == [[39, true], [0x200 + JOY_BUTTON_DPAD_RIGHT, true]])
    axis.axis_value = 0.45
    assert(pad.translate(axis).is_empty())
    assert(pad.release_device(2) == [[39, false], [0x200 + JOY_BUTTON_DPAD_RIGHT, false]])
    axis.axis_value = -0.8
    assert(pad.translate(axis) == [[37, true], [0x200 + JOY_BUTTON_DPAD_LEFT, true]])
    assert(pad.release_all().size() == 2)
    assert(pad.release_all().is_empty())
    print("Siglus gamepad regression passed: raw/VK, repeat, multi-device, analog hysteresis, disconnect/focus release")
    quit()
