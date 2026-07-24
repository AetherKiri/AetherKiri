extends RefCounted

const PRESS_SCALE := Vector2(0.975, 0.975)
const REST_SCALE := Vector2.ONE
const ENTER_OFFSET := Vector2(0, 10)
const ENTER_DURATION := 0.22
const PRESS_IN_DURATION := 0.10
const PRESS_OUT_DURATION := 0.16

var reduced_motion := false
var active_tweens := {}

func _init() -> void:
    var value := OS.get_environment("AETHERKIRI_REDUCED_MOTION").strip_edges().to_lower()
    reduced_motion = value in ["1", "true", "yes", "on"]

func bind_pressable(control: Control) -> void:
    if control == null or control.has_meta("aether_motion_bound"):
        return
    control.set_meta("aether_motion_bound", true)
    _update_pivot(control)
    control.resized.connect(func(): _update_pivot(control))
    if control is BaseButton:
        var button := control as BaseButton
        button.button_down.connect(func(): _press_in(button))
        button.button_up.connect(func(): _press_out(button))
        button.mouse_exited.connect(func():
            if not button.button_pressed:
                _press_out(button)
        )

func enter(control: Control, offset: Vector2 = ENTER_OFFSET, delay: float = 0.0) -> void:
    if control == null or not is_instance_valid(control):
        return
    _stop(control)
    var target_position := control.position
    control.modulate.a = 0.0
    if not reduced_motion:
        control.position = target_position + offset
    var tween := control.create_tween().set_parallel(true)
    active_tweens[control.get_instance_id()] = tween
    var duration := 0.12 if reduced_motion else ENTER_DURATION
    tween.tween_property(control, "modulate:a", 1.0, duration).set_delay(delay).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    if not reduced_motion:
        tween.tween_property(control, "position", target_position, duration).set_delay(delay).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    tween.chain().tween_callback(func(): _finish(control))

func modal_in(scrim: CanvasItem, dialog: Control) -> void:
    if scrim == null or dialog == null:
        return
    scrim.modulate.a = 0.0
    dialog.modulate.a = 0.0
    _update_pivot(dialog)
    dialog.scale = Vector2.ONE if reduced_motion else Vector2(0.985, 0.985)
    var tween := dialog.create_tween().set_parallel(true)
    active_tweens[dialog.get_instance_id()] = tween
    var duration := 0.14 if reduced_motion else 0.22
    tween.tween_property(scrim, "modulate:a", 1.0, duration).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    tween.tween_property(dialog, "modulate:a", 1.0, duration).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    if not reduced_motion:
        tween.tween_property(dialog, "scale", Vector2.ONE, duration).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    tween.chain().tween_callback(func(): _finish(dialog))

func _press_in(control: Control) -> void:
    if reduced_motion:
        return
    _animate_scale(control, PRESS_SCALE, PRESS_IN_DURATION)

func _press_out(control: Control) -> void:
    if reduced_motion:
        control.scale = REST_SCALE
        return
    _animate_scale(control, REST_SCALE, PRESS_OUT_DURATION)

func _animate_scale(control: Control, target: Vector2, duration: float) -> void:
    if control == null or not is_instance_valid(control):
        return
    _stop(control)
    _update_pivot(control)
    var tween := control.create_tween()
    active_tweens[control.get_instance_id()] = tween
    tween.tween_property(control, "scale", target, duration).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    tween.tween_callback(func(): _finish(control))

func _update_pivot(control: Control) -> void:
    if control != null and is_instance_valid(control):
        control.pivot_offset = control.size * 0.5

func _stop(control: Control) -> void:
    if control == null or not is_instance_valid(control):
        return
    var key := control.get_instance_id()
    var tween = active_tweens.get(key)
    if tween is Tween and tween.is_valid():
        tween.kill()
    active_tweens.erase(key)

func _finish(control: Control) -> void:
    if control != null and is_instance_valid(control):
        active_tweens.erase(control.get_instance_id())
