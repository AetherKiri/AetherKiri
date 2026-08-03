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

    var orphan := Control.new()
    root.add_child(orphan)
    motion.spring_property(orphan, "scale", Vector2(0.9, 0.9), 0.32, 1.0)
    var orphan_key := "%d:scale" % orphan.get_instance_id()
    orphan.queue_free()
    await process_frame
    await process_frame
    if motion.active_springs.has(orphan_key):
        _fail("freed spring owner was not retired")
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

    motion.reduced_motion = false
    var hero := Control.new()
    hero.position = Vector2(12, 18)
    hero.size = Vector2(120, 80)
    root.add_child(hero)
    var hero_finished := {"value": false}
    motion.hero_rect(hero, Rect2(240, 96, 252, 354), func(): hero_finished["value"] = true)
    for _frame in range(90):
        await process_frame
    if not hero.position.is_equal_approx(Vector2(240, 96)) or not hero.size.is_equal_approx(Vector2(252, 354)):
        _fail("hero transition did not settle on the destination rect")
    if not bool(hero_finished["value"]):
        _fail("hero transition did not complete")
        return

    hero.position = Vector2(1120, 180)
    hero.size = Vector2(144, 166)
    var right_start := Rect2(hero.position, hero.size)
    var right_target := Rect2(36, 92, 252, 354)
    var arc_midpoint: Rect2 = motion.hero_arc_rect(right_start, right_target, 0.5)
    var linear_midpoint := right_start.get_center().lerp(right_target.get_center(), 0.5)
    if arc_midpoint.get_center().distance_to(linear_midpoint) < 20.0:
        _fail("right-column hero transition did not follow a visible arc")
        return
    var reverse_midpoint: Rect2 = motion.hero_arc_rect(right_target, right_start, 0.5)
    if not reverse_midpoint.position.is_equal_approx(arc_midpoint.position) or not reverse_midpoint.size.is_equal_approx(arc_midpoint.size):
        _fail("hero return transition did not retrace the forward arc")
        return
    var right_column_finished := {"count": 0}
    motion.hero_rect(hero, right_target, func(): right_column_finished["count"] += 1)
    for _frame in range(90):
        await process_frame
    if not hero.position.is_equal_approx(Vector2(36, 92)) or not hero.size.is_equal_approx(Vector2(252, 354)):
        _fail("right-column hero transition did not settle on the detail cover")
        return
    if int(right_column_finished["count"]) != 1:
        _fail("right-column hero transition did not finish exactly once")
        return

    var outgoing := Control.new()
    var incoming := Control.new()
    outgoing.position = Vector2(0, 0)
    incoming.position = Vector2(0, 0)
    incoming.visible = false
    root.add_child(outgoing)
    root.add_child(incoming)
    motion.route_transition(outgoing, incoming)
    await process_frame
    if incoming.position.y <= 0.0 or incoming.modulate.a >= 1.0:
        _fail("route transition skipped its lifted fade-in frame")
        return
    for _frame in range(90):
        await process_frame
    if outgoing.visible or not incoming.visible or not incoming.position.is_equal_approx(Vector2.ZERO) or not is_equal_approx(incoming.modulate.a, 1.0):
        _fail("route transition did not settle both pages")
        return
    motion.route_transition(incoming, outgoing)
    await process_frame
    motion.route_transition(outgoing, incoming)
    for _frame in range(90):
        await process_frame
    if outgoing.visible or not incoming.visible or not is_equal_approx(incoming.modulate.a, 1.0):
        _fail("interrupted route transition left a stale page visible")
        return

    print("aether_motion_test: PASS")
    quit(0)

func _fail(message: String) -> void:
    push_error("aether_motion_test: %s" % message)
    quit(1)
