extends Node

const PRESS_SCALE := Vector2(0.972, 0.972)
const TACTILE_PRESS_SCALE := Vector2(0.955, 0.955)
const HOVER_SCALE := Vector2(1.010, 1.010)
const REST_SCALE := Vector2.ONE
const ENTER_OFFSET := Vector2(0, 10)
const ENTER_DURATION := 0.24
const ROUTE_EXIT_DURATION := 0.18
const ROUTE_ENTER_DELAY := 0.075
const PRESS_RESPONSE := 0.16
const RELEASE_RESPONSE := 0.24
const HOVER_RESPONSE := 0.26
const SPRING_STEP := 1.0 / 120.0
const HERO_DURATION := 0.34
const HERO_MIN_ARC := 20.0
const HERO_MAX_ARC := 72.0

var reduced_motion := false
var active_tweens: Dictionary = {}
var active_springs: Dictionary = {}

func _init() -> void:
    var value := OS.get_environment("AETHERKIRI_REDUCED_MOTION").strip_edges().to_lower()
    reduced_motion = value in ["1", "true", "yes", "on"]
    set_process(false)

func _process(delta: float) -> void:
    if active_springs.is_empty():
        set_process(false)
        return
    var clamped_delta := minf(delta, 1.0 / 30.0)
    var step_count := maxi(1, ceili(clamped_delta / SPRING_STEP))
    var step_delta := clamped_delta / float(step_count)
    var completed: Array[String] = []
    var callbacks: Array[Callable] = []
    for key_variant in active_springs.keys():
        var key := String(key_variant)
        var state: Dictionary = active_springs[key]
        # Keep the stored reference untyped until validity is checked. GDScript
        # raises when assigning a freed Object to a typed local before the
        # is_instance_valid guard can run.
        var owner = state.get("owner")
        if owner == null or not is_instance_valid(owner):
            completed.append(key)
            continue
        var property: StringName = state.get("property")
        var current = owner.get(property)
        var target = state.get("target")
        var velocity = state.get("velocity")
        var response: float = state.get("response", 0.32)
        var damping_ratio: float = state.get("damping", 1.0)
        var omega := TAU / maxf(0.08, response)
        var stiffness := omega * omega
        var damping := 2.0 * damping_ratio * omega
        for _step in range(step_count):
            velocity += ((target - current) * stiffness - velocity * damping) * step_delta
            current += velocity * step_delta
        owner.set(property, current)
        state["velocity"] = velocity
        active_springs[key] = state
        var epsilon: float = state.get("epsilon", 0.001)
        if _value_length(target - current) <= epsilon and _value_length(velocity) <= epsilon * 4.0:
            owner.set(property, target)
            completed.append(key)
            var callback: Callable = state.get("finished", Callable())
            if callback.is_valid():
                callbacks.append(callback)
    for key in completed:
        active_springs.erase(key)
    for callback in callbacks:
        callback.call()
    if active_springs.is_empty():
        set_process(false)

func spring_property(
    owner: Object,
    property: StringName,
    target,
    response: float = 0.32,
    damping: float = 1.0,
    finished: Callable = Callable()
) -> void:
    if owner == null or not is_instance_valid(owner):
        if finished.is_valid():
            finished.call()
        return
    var key := _motion_key(owner, property)
    if reduced_motion:
        active_springs.erase(key)
        owner.set(property, target)
        if finished.is_valid():
            finished.call()
        return
    var current = owner.get(property)
    var velocity = _zero_like(current)
    if active_springs.has(key):
        velocity = active_springs[key].get("velocity", velocity)
    active_springs[key] = {
        "owner": owner,
        "property": property,
        "target": target,
        "velocity": velocity,
        "response": response,
        "damping": damping,
        "epsilon": 0.0008 if current is float else 0.04,
        "finished": finished,
    }
    set_process(true)

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
        button.visibility_changed.connect(func():
            if not button.is_visible_in_tree():
                cancel_press(button)
        )

func bind_tactile(control: Control) -> void:
    if control == null:
        return
    control.set_meta("aether_press_scale", TACTILE_PRESS_SCALE)
    control.set_meta("aether_release_damping", 0.82)
    bind_pressable(control)

func cancel_press(control: Control) -> void:
    if control == null or not is_instance_valid(control):
        return
    active_springs.erase(_motion_key(control, "scale"))
    control.scale = REST_SCALE

func bind_lift(control: Control, highlight: CanvasItem = null, rest_alpha: float = 0.0, hover_alpha: float = 1.0) -> void:
    if control == null or control.has_meta("aether_lift_bound"):
        return
    control.set_meta("aether_lift_bound", true)
    control.set_meta("aether_hovered", false)
    _update_pivot(control)
    control.resized.connect(func(): _update_pivot(control))
    if highlight != null:
        highlight.modulate.a = rest_alpha
    control.mouse_entered.connect(func(): _set_lift_hover(control, highlight, true, rest_alpha, hover_alpha))
    control.mouse_exited.connect(func(): _set_lift_hover(control, highlight, false, rest_alpha, hover_alpha))
    control.focus_entered.connect(func(): _set_lift_hover(control, highlight, true, rest_alpha, hover_alpha))
    control.focus_exited.connect(func():
        if not control.get_global_rect().has_point(control.get_global_mouse_position()):
            _set_lift_hover(control, highlight, false, rest_alpha, hover_alpha)
    )
    if control is BaseButton:
        var button := control as BaseButton
        button.button_down.connect(func(): _press_in(button))
        button.button_up.connect(func():
            var hovered := bool(button.get_meta("aether_hovered", false))
            _animate_scale(button, HOVER_SCALE if hovered else REST_SCALE, RELEASE_RESPONSE)
        )

func enter(control: Control, offset: Vector2 = ENTER_OFFSET, delay: float = 0.0) -> void:
    if control == null or not is_instance_valid(control):
        return
    _stop_tweens(control)
    var target_position := control.position
    control.modulate.a = 0.0
    if not reduced_motion:
        control.position = target_position + offset
    var tween := control.create_tween().set_parallel(true)
    active_tweens[_tween_key(control, "enter")] = tween
    var duration := 0.12 if reduced_motion else ENTER_DURATION
    tween.tween_property(control, "modulate:a", 1.0, duration).set_delay(delay).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    if not reduced_motion:
        tween.tween_property(control, "position", target_position, duration).set_delay(delay).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    tween.chain().tween_callback(func(): _finish_tween(control, "enter"))

func route_in(control: Control, direction: float = 1.0) -> void:
    enter(control, Vector2(0.0, 8.0))

func route_transition(
    outgoing: Control,
    incoming: Control,
    lift: bool = true,
    finished: Callable = Callable()
) -> void:
    if incoming == null or not is_instance_valid(incoming):
        if finished.is_valid():
            finished.call()
        return
    _stop_tweens(incoming)
    if outgoing != null and is_instance_valid(outgoing) and outgoing != incoming:
        _stop_tweens(outgoing)
    var incoming_rest: Vector2 = incoming.get_meta("aether_route_rest_position", incoming.position)
    incoming.set_meta("aether_route_rest_position", incoming_rest)
    incoming.visible = true
    incoming.modulate.a = 0.0
    incoming.position = incoming_rest if reduced_motion or not lift else incoming_rest + Vector2(0, 14)
    var outgoing_rest := Vector2.ZERO
    if outgoing != null and is_instance_valid(outgoing) and outgoing != incoming:
        outgoing_rest = outgoing.get_meta("aether_route_rest_position", outgoing.position)
        outgoing.set_meta("aether_route_rest_position", outgoing_rest)
        outgoing.visible = true
    var tween := incoming.create_tween().set_parallel(true)
    active_tweens[_tween_key(incoming, "route")] = tween
    if outgoing != null and is_instance_valid(outgoing) and outgoing != incoming:
        active_tweens[_tween_key(outgoing, "route")] = tween
        tween.tween_property(outgoing, "modulate:a", 0.0, ROUTE_EXIT_DURATION).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
        if not reduced_motion and lift:
            tween.tween_property(outgoing, "position", outgoing_rest + Vector2(0, -6), ROUTE_EXIT_DURATION).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    var enter_duration := 0.12 if reduced_motion else ENTER_DURATION
    var enter_delay := 0.0 if reduced_motion else ROUTE_ENTER_DELAY
    tween.tween_property(incoming, "modulate:a", 1.0, enter_duration).set_delay(enter_delay).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    if not reduced_motion and lift:
        tween.tween_property(incoming, "position", incoming_rest, enter_duration).set_delay(enter_delay).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    tween.chain().tween_callback(func():
        incoming.position = incoming_rest
        incoming.modulate.a = 1.0
        _finish_tween(incoming, "route")
        if outgoing != null and is_instance_valid(outgoing) and outgoing != incoming:
            outgoing.position = outgoing_rest
            outgoing.modulate.a = 1.0
            outgoing.visible = false
            _finish_tween(outgoing, "route")
        if finished.is_valid():
            finished.call()
    )

func settle_route(control: Control, show: bool) -> void:
    if control == null or not is_instance_valid(control):
        return
    _stop_tweens(control)
    control.position = control.get_meta("aether_route_rest_position", control.position)
    control.modulate.a = 1.0
    control.visible = show

func hero_rect(control: Control, target_rect: Rect2, finished: Callable = Callable()) -> void:
    if control == null or not is_instance_valid(control):
        if finished.is_valid():
            finished.call()
        return
    if reduced_motion:
        control.position = target_rect.position
        control.size = target_rect.size
        if finished.is_valid():
            finished.call()
        return
    active_springs.erase(_motion_key(control, "position"))
    active_springs.erase(_motion_key(control, "size"))
    _stop_tweens(control)
    var start_rect := Rect2(control.position, control.size)
    var tween := control.create_tween()
    active_tweens[_tween_key(control, "hero")] = tween
    tween.tween_method(
        func(progress: float):
            if is_instance_valid(control):
                var frame := hero_arc_rect(start_rect, target_rect, progress)
                control.position = frame.position
                control.size = frame.size,
        0.0,
        1.0,
        HERO_DURATION
    ).set_trans(Tween.TRANS_QUINT).set_ease(Tween.EASE_IN_OUT)
    tween.tween_callback(func():
        if is_instance_valid(control):
            control.position = target_rect.position
            control.size = target_rect.size
        _finish_tween(control, "hero")
        if finished.is_valid():
            finished.call()
    )

func hero_arc_rect(start_rect: Rect2, target_rect: Rect2, progress: float) -> Rect2:
    var t := clampf(progress, 0.0, 1.0)
    var start_center := start_rect.get_center()
    var target_center := target_rect.get_center()
    var delta := target_center - start_center
    var distance := delta.length()
    if distance < 0.001:
        return Rect2(start_rect.position.lerp(target_rect.position, t), start_rect.size.lerp(target_rect.size, t))
    var arc_height := clampf(distance * 0.12, HERO_MIN_ARC, HERO_MAX_ARC)
    var control := (start_center + target_center) * 0.5
    if absf(delta.x) >= absf(delta.y):
        control.y = minf(start_center.y, target_center.y) - arc_height
    else:
        control.x = maxf(start_center.x, target_center.x) + arc_height
    var one_minus_t := 1.0 - t
    var center := one_minus_t * one_minus_t * start_center \
        + 2.0 * one_minus_t * t * control \
        + t * t * target_center
    var size := start_rect.size.lerp(target_rect.size, t)
    return Rect2(center - size * 0.5, size)

func modal_in(scrim: CanvasItem, dialog: Control, background: Control = null) -> void:
    if scrim == null or dialog == null:
        return
    _stop_tweens(scrim)
    _stop_tweens(dialog)
    scrim.modulate.a = 0.0
    dialog.modulate.a = 0.0
    _update_pivot(dialog)
    var rest_position := dialog.position
    dialog.set_meta("aether_modal_rest_position", rest_position)
    if not reduced_motion:
        dialog.position = rest_position + Vector2(0, 8)
        dialog.scale = Vector2(0.965, 0.965)
        spring_property(dialog, "position", rest_position, 0.34, 1.0)
        spring_property(dialog, "scale", REST_SCALE, 0.32, 1.0)
    else:
        dialog.scale = REST_SCALE
    _fade(scrim, 1.0, 0.18 if not reduced_motion else 0.12, "modal")
    _fade(dialog, 1.0, 0.20 if not reduced_motion else 0.12, "modal")
    if background != null and is_instance_valid(background):
        _update_pivot(background)
        spring_property(background, "scale", Vector2(0.992, 0.992), 0.38, 1.0)
        _fade(background, 0.94, 0.20, "modal_background")

func modal_out(scrim: CanvasItem, dialog: Control, background: Control = null, finished: Callable = Callable()) -> void:
    if scrim == null or dialog == null:
        if finished.is_valid():
            finished.call()
        return
    var rest_position: Vector2 = dialog.get_meta("aether_modal_rest_position", dialog.position)
    if not reduced_motion:
        spring_property(dialog, "position", rest_position + Vector2(0, 8), 0.24, 1.0)
        spring_property(dialog, "scale", Vector2(0.965, 0.965), 0.24, 1.0)
    _fade(scrim, 0.0, 0.14 if not reduced_motion else 0.10, "modal")
    _fade(dialog, 0.0, 0.14 if not reduced_motion else 0.10, "modal", finished)
    if background != null and is_instance_valid(background):
        spring_property(background, "scale", REST_SCALE, 0.34, 1.0)
        _fade(background, 1.0, 0.16, "modal_background")

func loading_in(panel: Control, card: Control) -> void:
    if panel == null or card == null:
        return
    panel.visible = true
    panel.modulate.a = 0.0
    _update_pivot(card)
    if not reduced_motion:
        card.scale = Vector2(0.975, 0.975)
        spring_property(card, "scale", REST_SCALE, 0.32, 1.0)
    _fade(panel, 1.0, 0.18 if not reduced_motion else 0.12, "loading")

func loading_out(panel: Control, card: Control, finished: Callable = Callable()) -> void:
    if panel == null or not is_instance_valid(panel):
        if finished.is_valid():
            finished.call()
        return
    if card != null and is_instance_valid(card) and not reduced_motion:
        spring_property(card, "scale", Vector2(0.985, 0.985), 0.22, 1.0)
    _fade(panel, 0.0, 0.14 if not reduced_motion else 0.10, "loading", func():
        panel.visible = false
        panel.modulate.a = 1.0
        if card != null and is_instance_valid(card):
            card.scale = REST_SCALE
        if finished.is_valid():
            finished.call()
    )

func reveal(control: Control, delay: float = 0.0) -> void:
    if control == null or not is_instance_valid(control):
        return
    _stop_tweens(control)
    _update_pivot(control)
    control.modulate.a = 0.0
    control.scale = REST_SCALE if reduced_motion else Vector2(0.985, 0.985)
    var tween := control.create_tween().set_parallel(true)
    active_tweens[_tween_key(control, "reveal")] = tween
    var duration := 0.12 if reduced_motion else 0.22
    tween.tween_property(control, "modulate:a", 1.0, duration).set_delay(delay).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    if not reduced_motion:
        tween.tween_property(control, "scale", REST_SCALE, duration).set_delay(delay).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    tween.chain().tween_callback(func(): _finish_tween(control, "reveal"))

func set_visible(control: Control, show: bool) -> void:
    if control == null or not is_instance_valid(control):
        return
    _update_pivot(control)
    if show:
        control.visible = true
        if control.modulate.a >= 0.99:
            control.modulate.a = 0.0
            control.scale = REST_SCALE if reduced_motion else Vector2(0.985, 0.985)
    if not reduced_motion:
        spring_property(control, "scale", REST_SCALE if show else Vector2(0.985, 0.985), 0.28 if show else 0.22, 1.0)
    _fade(control, 1.0 if show else 0.0, 0.18 if show else 0.14, "visibility", func():
        if not show:
            control.visible = false
            control.modulate.a = 1.0
            control.scale = REST_SCALE
    )

func _press_in(control: Control) -> void:
    if reduced_motion:
        return
    var target: Vector2 = control.get_meta("aether_press_scale", PRESS_SCALE)
    _animate_scale(control, target, PRESS_RESPONSE)

func _press_out(control: Control) -> void:
    if reduced_motion:
        control.scale = REST_SCALE
        return
    var damping := float(control.get_meta("aether_release_damping", 1.0))
    _animate_scale(control, REST_SCALE, RELEASE_RESPONSE, damping)

func _set_lift_hover(control: Control, highlight: CanvasItem, active: bool, rest_alpha: float, hover_alpha: float) -> void:
    if control == null or not is_instance_valid(control):
        return
    control.set_meta("aether_hovered", active)
    _animate_scale(control, HOVER_SCALE if active else REST_SCALE, HOVER_RESPONSE)
    if highlight == null or not is_instance_valid(highlight):
        return
    if reduced_motion:
        highlight.modulate.a = hover_alpha if active else rest_alpha
        return
    _fade(highlight, hover_alpha if active else rest_alpha, 0.16, "hover")

func _animate_scale(control: Control, target: Vector2, response: float, damping: float = 1.0) -> void:
    if control == null or not is_instance_valid(control):
        return
    _update_pivot(control)
    spring_property(control, "scale", target, response, damping)

func _fade(item: CanvasItem, target: float, duration: float, channel: String, finished: Callable = Callable()) -> void:
    if item == null or not is_instance_valid(item):
        if finished.is_valid():
            finished.call()
        return
    var key := _tween_key(item, channel)
    _stop_tween_key(key)
    var tween := item.create_tween()
    active_tweens[key] = tween
    tween.tween_property(item, "modulate:a", target, duration).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    tween.tween_callback(func():
        active_tweens.erase(key)
        if finished.is_valid():
            finished.call()
    )

func _update_pivot(control: Control) -> void:
    if control != null and is_instance_valid(control):
        control.pivot_offset = control.size * 0.5

func _stop_tweens(owner: Object) -> void:
    if owner == null or not is_instance_valid(owner):
        return
    var prefix := "%d:" % owner.get_instance_id()
    for key_variant in active_tweens.keys():
        var key := String(key_variant)
        if key.begins_with(prefix):
            _stop_tween_key(key)

func _stop_tween_key(key: String) -> void:
    var tween = active_tweens.get(key)
    if tween is Tween and tween.is_valid():
        tween.kill()
    active_tweens.erase(key)

func _finish_tween(owner: Object, channel: String) -> void:
    if owner != null and is_instance_valid(owner):
        active_tweens.erase(_tween_key(owner, channel))

func _motion_key(owner: Object, property: StringName) -> String:
    return "%d:%s" % [owner.get_instance_id(), String(property)]

func _tween_key(owner: Object, channel: String) -> String:
    return "%d:%s" % [owner.get_instance_id(), channel]

func _zero_like(value):
    if value is Vector2:
        return Vector2.ZERO
    return 0.0

func _value_length(value) -> float:
    if value is Vector2:
        return value.length()
    return absf(float(value))
