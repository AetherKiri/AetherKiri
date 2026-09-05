extends SceneTree

# Data-driven verification for the spring UI fixes (headless probe).
#
# Covered behaviours and their numeric acceptance criteria:
#  1. Select popup bounce: under-damped scale spring overshoots the target by
#     >= 1.5% (the old pair peaked at ~0.6% — invisible), stays aspect-uniform,
#     and settles exactly at 1.0.
#  2. Select menu items cascade in order: row N+1 completes its reveal strictly
#     after row N (staggered slide-in), and every row starts vertically
#     squashed (slide-from-under-the-previous-row look).
#  3. Nav pill drag geometry: the pill target clamps to the first/last button
#     span on the drag axis for both horizontal and vertical rails.
#  4. Nav pill jelly is axis-aware: horizontal rails stretch X, vertical rails
#     stretch Y.
#  5. Sidebar width spring (0.42/0.60) overshoots the collapsed width and
#     settles exactly — the jelly feel is a physical property of the spring.
#  6. Slider scrubbing never scales the control (no distorted knob) while the
#     track thickens and relaxes.
#  7. Switches share identical geometry and shape styles across color modes.
#  8. Shell scroll containers disable native touch pan (deadzone) so the
#     custom momentum/rubber-band system owns the gesture.
#  9. Settings row builders emit one font family and one size scale.

const AetherDesignTokens = preload("res://scripts/ui/aether_design_tokens.gd")
const AetherMotion = preload("res://scripts/ui/aether_motion.gd")
const AetherSelect = preload("res://scripts/ui/aether_select.gd")
const AetherSlider = preload("res://scripts/ui/aether_slider.gd")
const AetherSwitch = preload("res://scripts/ui/aether_switch.gd")
const MainScript = preload("res://scripts/main.gd")

func _init() -> void:
    call_deferred("_run")

func _run() -> void:
    root.size = Vector2i(720, 520)
    var scene := Control.new()
    scene.size = Vector2(720, 520)
    root.add_child(scene)
    current_scene = scene

    var tokens = AetherDesignTokens.new()
    tokens.configure("dark")
    var motion = AetherMotion.new()
    root.add_child(motion)
    var main = MainScript.new()

    # --- 1. popup bounce ---------------------------------------------------
    var select = AetherSelect.new()
    select.setup(
        tokens,
        motion,
        load("res://assets/ui/icons/chevron-down.svg"),
        load("res://assets/ui/icons/check.svg")
    )
    for index in range(6):
        select.add_item("Option %d" % (index + 1))
    select.position = Vector2(20, 60)
    select.size = Vector2(300, 44)
    scene.add_child(select)
    select._open_popup()
    await process_frame
    await process_frame
    var popup: Control = select.popup_panel
    var peak_scale := 0.0
    var start_scale := popup.scale.x
    var popup_key := "%d:scale" % popup.get_instance_id()
    for _step in range(720):
        motion._process(1.0 / 120.0)
        peak_scale = maxf(peak_scale, popup.scale.x)
        if not motion.active_springs.has(popup_key):
            break
    if popup.scale.x != popup.scale.y:
        _fail("popup scale lost aspect uniformity during the bounce")
        return
    if start_scale > 0.95:
        _fail("popup no longer starts compressed (start=%.3f)" % start_scale)
        return
    if peak_scale < 1.015:
        _fail("popup bounce overshoot too small to see (peak=%.4f)" % peak_scale)
        return
    if not popup.scale.is_equal_approx(Vector2.ONE):
        _fail("popup did not settle exactly at rest scale")
        return

    # --- 2. menu item cascade ---------------------------------------------
    var menu_items: Array = select.popup_menu.get_children()
    for item_index in range(menu_items.size()):
        var cascade_item: Control = menu_items[item_index]
        if float(cascade_item.get_meta("aether_cascade_start_scale_y", 1.0)) >= 0.95:
            _fail("menu row %d did not register its squashed slide-in start" % item_index)
            return
        var expected_delay := float(mini(item_index, AetherSelect.MENU_ITEM_MAX_STAGGER)) * AetherSelect.MENU_ITEM_STAGGER
        if not is_equal_approx(float(cascade_item.get_meta("aether_cascade_delay", -1.0)), expected_delay):
            _fail("menu row %d lost its incremental stagger delay" % item_index)
            return
    var reveal_frame := {}
    for frame in range(600):
        await process_frame
        for item_index in range(menu_items.size()):
            var item: Control = menu_items[item_index]
            if not reveal_frame.has(item_index) and item.modulate.a >= 0.95:
                reveal_frame[item_index] = frame
        if reveal_frame.size() == menu_items.size():
            break
    if reveal_frame.size() != menu_items.size():
        _fail("menu rows never completed their cascade reveal")
        return
    for item_index in range(menu_items.size() - 1):
        if int(reveal_frame[item_index]) > int(reveal_frame[item_index + 1]):
            _fail("menu rows did not reveal in incremental order at row %d" % item_index)
            return
    select._close_popup()
    for _frame in range(120):
        await process_frame

    # --- 3. nav pill drag geometry ----------------------------------------
    var rail := Control.new()
    rail.size = Vector2(500, 200)
    scene.add_child(rail)
    var pill := PanelContainer.new()
    pill.size = Vector2(60, 36)
    rail.add_child(pill)
    var button_a := Button.new()
    button_a.position = Vector2(40, 10)
    button_a.size = Vector2(120, 48)
    rail.add_child(button_a)
    var button_b := Button.new()
    button_b.position = Vector2(340, 10)
    button_b.size = Vector2(120, 48)
    rail.add_child(button_b)
    var specs := [{"button": button_a, "action": Callable(), "route": "a"}, {"button": button_b, "action": Callable(), "route": "b"}]
    var clamped_low: float = main._nav_pill_drag_target(pill, specs, 0, -1000.0)
    var clamped_high: float = main._nav_pill_drag_target(pill, specs, 0, 1000.0)
    var expected_low: float = button_a.get_global_rect().get_center().x - pill.size.x * 0.5
    var expected_high: float = button_b.get_global_rect().get_center().x - pill.size.x * 0.5
    if not is_equal_approx(clamped_low, expected_low) or not is_equal_approx(clamped_high, expected_high):
        _fail("horizontal pill drag did not clamp to the nav button span")
        return
    var vertical_low: float = main._nav_pill_drag_target(pill, specs, 1, 1000.0)
    var expected_row_y: float = button_a.get_global_rect().get_center().y - pill.size.y * 0.5
    if not is_equal_approx(vertical_low, expected_row_y):
        _fail("vertical drag target did not lock onto the nav button row")
        return

    # --- 4. axis-aware jelly ----------------------------------------------
    main._jelly_pill(pill, true)
    var jelly_key := "%d:scale" % pill.get_instance_id()
    var jelly_target: Vector2 = main.ui_motion.active_springs[jelly_key]["target"]
    if jelly_target.x <= 1.0 or jelly_target.y >= 1.0:
        _fail("horizontal pill jelly did not stretch along x")
        return
    main._jelly_pill(pill, false)
    jelly_target = main.ui_motion.active_springs[jelly_key]["target"]
    if jelly_target.y <= 1.0 or jelly_target.x >= 1.0:
        _fail("vertical pill jelly did not stretch along y")
        return
    main.ui_motion.active_springs.erase(jelly_key)

    # --- 5. sidebar width spring overshoot + settle ------------------------
    var sidebar := PanelContainer.new()
    sidebar.size = Vector2(248, 600)
    scene.add_child(sidebar)
    motion.spring_property(sidebar, "size", Vector2(80, 600), 0.42, 0.60)
    var sidebar_key := "%d:size" % sidebar.get_instance_id()
    var min_width := 248.0
    for _step in range(900):
        motion._process(1.0 / 120.0)
        min_width = minf(min_width, sidebar.size.x)
        if not motion.active_springs.has(sidebar_key):
            break
    if min_width >= 79.0:
        _fail("sidebar width spring had no visible overshoot (min=%.2f)" % min_width)
        return
    if not sidebar.size.is_equal_approx(Vector2(80, 600)):
        _fail("sidebar width spring did not settle exactly on its target")
        return

    # --- 6. slider scrub: no distortion, thickness instead -----------------
    var slider = AetherSlider.new()
    slider.min_value = 0.0
    slider.max_value = 1.0
    slider.step = 0.1
    slider.setup(tokens, 0.4)
    slider.position = Vector2(20, 300)
    slider.size = Vector2(300, 40)
    scene.add_child(slider)
    slider.drag_started.emit()
    for _frame in range(140):
        await process_frame
    if not slider.scale.is_equal_approx(Vector2.ONE):
        _fail("slider scrubbing scaled the control and distorted the knob")
        return
    var thick: float = slider.track_style.content_margin_top
    if thick <= AetherSlider.TRACK_THIN + 1.0:
        _fail("slider track did not thicken while scrubbing (%.2f)" % thick)
        return
    slider.drag_ended.emit(false)
    for _frame in range(200):
        await process_frame
    if slider.track_style.content_margin_top > AetherSlider.TRACK_THIN + 0.3:
        _fail("slider track did not relax after scrubbing")
        return

    # --- 7. switch geometry across modes ----------------------------------
    var dark_tokens = AetherDesignTokens.new()
    dark_tokens.configure("dark")
    var light_tokens = AetherDesignTokens.new()
    light_tokens.configure("classic")
    var dark_switch = AetherSwitch.new()
    dark_switch.setup(dark_tokens, motion, true)
    var light_switch = AetherSwitch.new()
    light_switch.setup(light_tokens, motion, true)
    if dark_switch.TRACK_SIZE != light_switch.TRACK_SIZE or dark_switch.KNOB_SIZE != light_switch.KNOB_SIZE:
        _fail("switch geometry differs between color modes")
        return
    var dark_track: StyleBoxFlat = dark_switch.get_theme_stylebox("normal")
    var light_track: StyleBoxFlat = light_switch.get_theme_stylebox("normal")
    if dark_track.corner_radius_top_left != light_track.corner_radius_top_left:
        _fail("switch track shape differs between color modes")
        return
    var dark_knob: StyleBoxFlat = dark_switch.knob.get_theme_stylebox("panel")
    var light_knob: StyleBoxFlat = light_switch.knob.get_theme_stylebox("panel")
    if dark_knob.corner_radius_top_left != light_knob.corner_radius_top_left:
        _fail("switch knob shape differs between color modes")
        return
    if light_knob.border_color.a >= 1.0 or light_knob.border_color.r > 0.5:
        _fail("light-mode switch knob kept the dark-mode edge treatment")
        return

    # --- 8. shell scroll containers own touch gestures ---------------------
    var shell_scroll := ScrollContainer.new()
    main._configure_shell_scroll(shell_scroll)
    if shell_scroll.scroll_deadzone < 10000:
        _fail("shell scroll still lets the native touch pan fight the momentum system")
        return

    # --- 9. settings rows share one font family + size scale --------------
    main.settings_compact_layout = false
    var placeholder := Button.new()
    var rows := {
        "row": main._settings_row("标题", "副标题", placeholder, false),
        "value": main._settings_value_row("标题", "值"),
        "action": main._settings_action_row("标题", "副标题", "操作", Callable()),
        "fps": main._settings_fps_row(),
    }
    for row_kind in rows.keys():
        var labels := _collect_labels(rows[row_kind])
        if labels.size() < 2:
            _fail("%s row did not produce a title and a subtitle label" % row_kind)
            return
        var title_label: Label = labels[0]
        var sub_label: Label = labels[1]
        if title_label.get_theme_font("font") != MainScript.BODY_FONT:
            _fail("%s row title did not use the unified body font" % row_kind)
            return
        if int(title_label.get_theme_font_size("font_size")) != 15:
            _fail("%s row title font size left the unified scale" % row_kind)
            return
        if int(sub_label.get_theme_font_size("font_size")) != 12:
            _fail("%s row subtitle font size left the unified scale" % row_kind)
            return

    main.free()
    print("aether_spring_ui_probe_test: PASS")
    quit(0)

func _collect_labels(node: Node) -> Array[Label]:
    var labels: Array[Label] = []
    if node is Label:
        labels.append(node)
        return labels
    for child in node.get_children():
        labels.append_array(_collect_labels(child))
    return labels

func _fail(message: String) -> void:
    push_error("aether_spring_ui_probe_test: %s" % message)
    quit(1)
