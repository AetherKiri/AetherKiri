extends SceneTree

const AetherDesignTokens = preload("res://scripts/ui/aether_design_tokens.gd")
const AetherMotion = preload("res://scripts/ui/aether_motion.gd")
const AetherWidgets = preload("res://scripts/ui/aether_widgets.gd")
const AetherSwitch = preload("res://scripts/ui/aether_switch.gd")
const AetherSegmentedControl = preload("res://scripts/ui/aether_segmented_control.gd")
const AetherDisclosure = preload("res://scripts/ui/aether_disclosure.gd")
const AetherSelect = preload("res://scripts/ui/aether_select.gd")

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
    var widgets = AetherWidgets.new(tokens, motion)

    var card_normal: StyleBoxFlat = tokens.card_style(false, false)
    var card_hover: StyleBoxFlat = tokens.card_style(true, false)
    if card_normal.border_width_left != 0 or card_hover.border_width_left != 0:
        _fail("library card reintroduced a decorative outline")
        return
    if card_normal.shadow_size != 0 or card_hover.shadow_size != 0:
        _fail("library card reintroduced a decorative glow")
        return
    var detail_outline: StyleBoxFlat = tokens.detail_outline_style()
    if detail_outline.border_width_left != 1 or detail_outline.border_color != tokens.separator:
        _fail("game detail cover lost its dedicated outline")
        return
    if tokens.material_panel().border_width_left != 0:
        _fail("content panel reintroduced a decorative outline")
        return
    var focus_style: StyleBoxFlat = tokens.focus_style()
    if focus_style.border_width_left != 0 or focus_style.shadow_size != 0:
        _fail("keyboard focus reintroduced a decorative highlight")
        return

    var primary := Button.new()
    primary.size = Vector2(160, 44)
    scene.add_child(primary)
    widgets.primary_button(primary)
    for state in ["normal", "hover", "pressed", "focus", "disabled"]:
        if not primary.has_theme_stylebox_override(state):
            _fail("primary button is missing its %s state" % state)
            return
    primary.emit_signal("button_down")
    motion._process(1.0 / 120.0)
    if primary.scale.x >= 1.0 or primary.scale.x <= motion.PRESS_SCALE.x:
        _fail("button press feedback skipped its intermediate frame")
        return
    primary.emit_signal("button_up")

    var fab := Button.new()
    scene.add_child(fab)
    widgets.floating_action_button(fab)
    var fab_style := fab.get_theme_stylebox("normal") as StyleBoxFlat
    if fab.custom_minimum_size != Vector2(56, 56) or fab_style == null or fab_style.corner_radius_top_left < 56:
        _fail("floating action button did not keep its circular 56px geometry")
        return
    var fab_hover := fab.get_theme_stylebox("hover") as StyleBoxFlat
    var fab_pressed := fab.get_theme_stylebox("pressed") as StyleBoxFlat
    if fab_hover == null or fab_pressed == null or fab_style.shadow_size != fab_hover.shadow_size or fab_style.shadow_size != fab_pressed.shadow_size:
        _fail("floating action button changed shadow geometry between states")
        return
    if fab_style.shadow_size != 0:
        _fail("floating action button introduced an extra shadow layer")
        return
    if not is_equal_approx(float(fab.get_meta("aether_release_damping", 1.0)), 0.82):
        _fail("floating action button did not receive tactile release physics")
        return

    var toggle = AetherSwitch.new()
    toggle.setup(tokens, motion, false)
    toggle.position = Vector2(20, 70)
    scene.add_child(toggle)
    var switch_start: float = float(toggle.knob.position.x)
    toggle._sync(true, true)
    motion._process(1.0 / 120.0)
    var switch_target: float = float(toggle.TRACK_SIZE.x - toggle.TRACK_INSET - toggle.KNOB_SIZE.x)
    if toggle.knob.position.x <= switch_start or toggle.knob.position.x >= switch_target:
        _fail("switch knob skipped its physical intermediate position")
        return

    var segmented = AetherSegmentedControl.new()
    segmented.setup(tokens, motion, PackedStringArray(["Godot Native", "Debug CPU"]), 0)
    segmented.position = Vector2(20, 120)
    segmented.size = Vector2(300, 44)
    scene.add_child(segmented)
    var segmented_track_style := segmented.get_child(0).get_theme_stylebox("panel") as StyleBoxFlat
    var segmented_indicator_style := segmented.indicator.get_theme_stylebox("panel") as StyleBoxFlat
    if segmented_track_style.border_width_left != 0 or segmented_indicator_style.border_width_left != 0 or segmented_indicator_style.shadow_size != 0:
        _fail("segmented control reintroduced a decorative highlight")
        return
    segmented._layout_indicator(false)
    var segment_start: float = float(segmented.indicator.position.x)
    segmented._select(1, true)
    motion._process(1.0 / 120.0)
    var segment_target: float = float(segmented.TRACK_INSET + (segmented.size.x - segmented.TRACK_INSET * 2.0) * 0.5)
    if segmented.indicator.position.x <= segment_start or segmented.indicator.position.x >= segment_target:
        _fail("segmented indicator skipped its intermediate position")
        return

    var disclosure = AetherDisclosure.new()
    disclosure.setup(
        tokens,
        motion,
        "Advanced",
        load("res://assets/ui/icons/chevron-right.svg"),
        false
    )
    disclosure.position = Vector2(20, 180)
    disclosure.size = Vector2(300, 52)
    scene.add_child(disclosure)
    widgets.disclosure_button(disclosure)
    disclosure.set_expanded(true, true)
    motion._process(1.0 / 120.0)
    if disclosure.chevron.rotation <= 0.0 or disclosure.chevron.rotation >= PI * 0.5:
        _fail("disclosure chevron skipped its intermediate rotation")
        return

    var select = AetherSelect.new()
    select.setup(
        tokens,
        motion,
        load("res://assets/ui/icons/chevron-down.svg"),
        load("res://assets/ui/icons/check.svg")
    )
    for label in ["System", "Simplified Chinese", "Traditional Chinese", "English", "Japanese"]:
        select.add_item(label)
    select.position = Vector2(20, 420)
    select.size = Vector2(300, 44)
    scene.add_child(select)
    select._open_popup()
    await process_frame
    if select.popup_panel == null or select.popup_panel.position.y >= select.get_global_rect().position.y:
        _fail("select did not flip above a constrained viewport boundary")
        return
    if not is_equal_approx(select.popup_panel.scale.x, select.popup_panel.scale.y):
        _fail("select popup animation distorted its aspect ratio")
        return
    var popup_style := select.popup_panel.get_theme_stylebox("panel") as StyleBoxFlat
    if popup_style == null or popup_style.shadow_size > 9 or popup_style.shadow_offset.y > 4.0:
        _fail("select popup introduced an excessive shadow")
        return

    var escape := InputEventAction.new()
    escape.action = "ui_cancel"
    escape.pressed = true
    select._unhandled_key_input(escape)
    for _frame in range(90):
        await process_frame
    if select.overlay != null:
        _fail("Escape did not dismiss the open select")
        return

    select._open_popup()
    await process_frame
    select._choose(2)
    for _frame in range(90):
        await process_frame
    if select.overlay != null or select.selected_index != 2:
        _fail("select choice did not commit and release its overlay")
        return

    print("aether_widget_interaction_test: PASS")
    quit(0)

func _fail(message: String) -> void:
    push_error("aether_widget_interaction_test: %s" % message)
    quit(1)
