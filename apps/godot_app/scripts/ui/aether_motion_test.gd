extends SceneTree

const AetherMotion = preload("res://scripts/ui/aether_motion.gd")

func _init() -> void:
    call_deferred("_run")

func _run() -> void:
    var motion = AetherMotion.new()
    root.add_child(motion)
    var control := Control.new()
    control.scale = Vector2.ONE
    root.add_child(control)

    motion.spring_property(control, "scale", Vector2(0.9, 0.9), 0.32, 1.0)
    for _frame in range(4):
        motion._process(1.0 / 120.0)
    if control.scale.x >= 1.0 or control.scale.x <= 0.9:
        _fail("spring did not move continuously toward its target")
        return

    var key := "%d:scale" % control.get_instance_id()
    var velocity_before: Vector2 = motion.active_springs[key].get("velocity", Vector2.ZERO)
    motion.spring_property(control, "scale", Vector2.ONE, 0.24, 1.0)
    var velocity_after: Vector2 = motion.active_springs[key].get("velocity", Vector2.ZERO)
    if not velocity_after.is_equal_approx(velocity_before):
        _fail("retargeting discarded the presentation velocity")
        return

    for _frame in range(240):
        motion._process(1.0 / 120.0)
    if not control.scale.is_equal_approx(Vector2.ONE) or motion.active_springs.has(key):
        _fail("spring did not settle exactly at its target")
        return

    var loading_panel := Control.new()
    var loading_card := Control.new()
    loading_panel.add_child(loading_card)
    root.add_child(loading_panel)
    motion.loading_in(loading_panel, loading_card)
    for _frame in range(90):
        await process_frame
    if not loading_panel.visible or not is_equal_approx(loading_panel.modulate.a, 1.0):
        _fail("loading material did not finish entering")
        return
    motion.loading_out(loading_panel, loading_card)
    for _frame in range(90):
        await process_frame
    if loading_panel.visible or not is_equal_approx(loading_panel.modulate.a, 1.0):
        _fail("loading material did not hide and reset after exit")
        return

    var scrim := ColorRect.new()
    var dialog := Control.new()
    var background := Control.new()
    root.add_child(background)
    root.add_child(scrim)
    root.add_child(dialog)
    motion.modal_in(scrim, dialog, background)
    for _frame in range(90):
        await process_frame
    if not dialog.scale.is_equal_approx(Vector2.ONE) or background.scale.x >= 1.0:
        _fail("modal did not settle while recessing its background")
        return
    var dismissal := {"completed": false}
    motion.modal_out(scrim, dialog, background, func(): dismissal["completed"] = true)
    for _frame in range(90):
        await process_frame
    if not bool(dismissal["completed"]) or not background.scale.is_equal_approx(Vector2.ONE):
        _fail("modal exit did not restore its background")
        return

    motion.reduced_motion = true
    motion.spring_property(control, "scale", Vector2(0.75, 0.75), 0.32, 0.8)
    if not control.scale.is_equal_approx(Vector2(0.75, 0.75)) or motion.active_springs.has(key):
        _fail("reduced motion did not resolve immediately")
        return

    print("aether_motion_test: PASS")
    quit(0)

func _fail(message: String) -> void:
    push_error("aether_motion_test: %s" % message)
    quit(1)
