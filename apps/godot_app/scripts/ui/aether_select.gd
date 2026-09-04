extends Button

signal item_selected(index: int)

const CONTROL_HEIGHT := 40.0
const MENU_GAP := 6.0
const MENU_PADDING := 6.0
const ITEM_HEIGHT := 40.0

var tokens
var motion
var chevron_texture: Texture2D
var check_texture: Texture2D
var items: Array[Dictionary] = []
var selected_index := -1
var overlay: Control
var popup_panel: PanelContainer
var chevron: TextureRect
var popup_tween: Tween

var item_count: int:
    get:
        return items.size()

func setup(design_tokens, motion_system, next_chevron: Texture2D, next_check: Texture2D) -> void:
    tokens = design_tokens
    motion = motion_system
    chevron_texture = next_chevron
    check_texture = next_check
    focus_mode = Control.FOCUS_ALL
    alignment = HORIZONTAL_ALIGNMENT_LEFT
    mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
    custom_minimum_size = Vector2(220, CONTROL_HEIGHT)
    add_theme_font_size_override("font_size", 14)
    add_theme_color_override("font_color", tokens.text_primary)
    add_theme_color_override("font_hover_color", tokens.text_primary)
    add_theme_color_override("font_pressed_color", tokens.text_primary)
    add_theme_color_override("font_focus_color", tokens.text_primary)
    add_theme_color_override("font_disabled_color", tokens.text_tertiary)
    add_theme_stylebox_override("normal", _field_box(tokens.glass_material, Color.TRANSPARENT, 0))
    add_theme_stylebox_override("hover", _field_box(tokens.surface_hover, Color.TRANSPARENT, 0))
    add_theme_stylebox_override("pressed", _field_box(tokens.accent_fill, Color.TRANSPARENT, 0))
    add_theme_stylebox_override("focus", tokens.focus_style(12))
    add_theme_stylebox_override("disabled", _field_box(Color(tokens.surface_raised.r, tokens.surface_raised.g, tokens.surface_raised.b, 0.34), Color.TRANSPARENT, 0))

    chevron = TextureRect.new()
    chevron.mouse_filter = Control.MOUSE_FILTER_IGNORE
    chevron.texture = chevron_texture
    chevron.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    chevron.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    chevron.modulate = tokens.text_secondary
    chevron.anchor_left = 1.0
    chevron.anchor_top = 0.5
    chevron.anchor_right = 1.0
    chevron.anchor_bottom = 0.5
    chevron.offset_left = -34
    chevron.offset_top = -9
    chevron.offset_right = -16
    chevron.offset_bottom = 9
    chevron.pivot_offset = Vector2(9, 9)
    add_child(chevron)

    pressed.connect(_toggle_popup)
    motion.bind_pressable(self)

func add_item(label: String) -> void:
    items.append({"label": label, "metadata": null})
    if selected_index < 0:
        select(0)

func set_item_metadata(index: int, metadata) -> void:
    if index >= 0 and index < items.size():
        items[index]["metadata"] = metadata

func get_item_metadata(index: int):
    return items[index].get("metadata") if index >= 0 and index < items.size() else null

func select(index: int) -> void:
    if index < 0 or index >= items.size():
        return
    selected_index = index
    text = String(items[index].get("label", ""))

func _toggle_popup() -> void:
    if overlay != null and is_instance_valid(overlay):
        _close_popup()
    else:
        _open_popup()

func _open_popup() -> void:
    if items.is_empty() or not is_inside_tree():
        return
    var scene := get_tree().current_scene as Control
    if scene == null:
        return
    overlay = Control.new()
    overlay.name = "AetherSelectOverlay"
    overlay.set_anchors_preset(Control.PRESET_FULL_RECT)
    overlay.mouse_filter = Control.MOUSE_FILTER_STOP
    overlay.z_index = 200
    scene.add_child(overlay)
    overlay.move_to_front()

    var dismiss_area := ColorRect.new()
    dismiss_area.color = Color.TRANSPARENT
    dismiss_area.set_anchors_preset(Control.PRESET_FULL_RECT)
    dismiss_area.mouse_filter = Control.MOUSE_FILTER_STOP
    dismiss_area.gui_input.connect(func(event: InputEvent):
        var dismiss: bool = event is InputEventMouseButton and event.pressed
        dismiss = dismiss or (event is InputEventScreenTouch and event.pressed)
        if dismiss:
            _close_popup()
    )
    overlay.add_child(dismiss_area)

    popup_panel = PanelContainer.new()
    popup_panel.mouse_filter = Control.MOUSE_FILTER_STOP
    popup_panel.add_theme_stylebox_override("panel", _popup_box())
    overlay.add_child(popup_panel)

    var menu := VBoxContainer.new()
    menu.add_theme_constant_override("separation", 0)
    popup_panel.add_child(menu)
    for index in range(items.size()):
        menu.add_child(_menu_item(index))

    var menu_height := MENU_PADDING * 2.0 + ITEM_HEIGHT * float(items.size())
    var trigger_rect := get_global_rect()
    var viewport_size := get_viewport_rect().size
    var menu_width := minf(maxf(size.x, 220.0), maxf(196.0, viewport_size.x - 24.0))
    var menu_position := Vector2(trigger_rect.position.x, trigger_rect.end.y + MENU_GAP)
    if menu_position.x + menu_width > viewport_size.x - 12.0:
        menu_position.x = viewport_size.x - menu_width - 12.0
    if menu_position.y + menu_height > viewport_size.y - 12.0:
        menu_position.y = trigger_rect.position.y - menu_height - MENU_GAP
    popup_panel.position = Vector2(maxf(12.0, menu_position.x), maxf(12.0, menu_position.y))
    popup_panel.size = Vector2(menu_width, menu_height)
    popup_panel.pivot_offset = Vector2(menu_width - 20.0, 0.0 if menu_position.y > trigger_rect.position.y else menu_height)
    _animate_popup(true)
    _animate_chevron(true)
    call_deferred("_focus_selected_item")

func _menu_item(index: int) -> Button:
    var button := Button.new()
    button.text = String(items[index].get("label", ""))
    button.alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.focus_mode = Control.FOCUS_ALL
    button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
    button.custom_minimum_size = Vector2(0, ITEM_HEIGHT)
    button.add_theme_font_size_override("font_size", 14)
    var selected := index == selected_index
    var foreground: Color = tokens.text_primary if selected else tokens.text_secondary
    button.add_theme_color_override("font_color", foreground)
    button.add_theme_color_override("font_hover_color", tokens.text_primary)
    button.add_theme_color_override("font_pressed_color", tokens.text_primary)
    button.add_theme_color_override("font_focus_color", tokens.text_primary)
    button.add_theme_stylebox_override("normal", tokens.button_style(tokens.accent_fill if selected else Color.TRANSPARENT, Color.TRANSPARENT, 6))
    button.add_theme_stylebox_override("hover", tokens.button_style(tokens.surface_hover, Color.TRANSPARENT, 6))
    button.add_theme_stylebox_override("pressed", tokens.button_style(tokens.accent_fill, Color.TRANSPARENT, 6))
    button.add_theme_stylebox_override("focus", tokens.button_style(tokens.accent_fill, Color.TRANSPARENT, 6))
    if selected:
        button.icon = check_texture
        button.expand_icon = true
        button.icon_alignment = HORIZONTAL_ALIGNMENT_RIGHT
        button.add_theme_constant_override("icon_max_width", 17)
        button.add_theme_color_override("icon_normal_color", tokens.accent)
        button.add_theme_color_override("icon_hover_color", tokens.accent)
    button.pressed.connect(func(): _choose(index))
    motion.bind_pressable(button)
    return button

func _choose(index: int) -> void:
    var changed := index != selected_index
    select(index)
    _close_popup()
    if changed:
        item_selected.emit(index)

func _close_popup() -> void:
    if overlay == null or not is_instance_valid(overlay):
        return
    _animate_chevron(false)
    _animate_popup(false)

func _animate_popup(show: bool) -> void:
    if popup_panel == null or not is_instance_valid(popup_panel):
        return
    if popup_tween != null and popup_tween.is_valid():
        popup_tween.kill()
    var rest_position: Vector2 = popup_panel.get_meta("aether_rest_position", popup_panel.position)
    popup_panel.set_meta("aether_rest_position", rest_position)
    if show:
        popup_panel.modulate.a = 0.0
        if not motion.reduced_motion:
            popup_panel.scale = Vector2(0.94, 0.94)
            popup_panel.position = rest_position + Vector2(0, -6)
    var duration := 0.12 if motion.reduced_motion else (0.18 if show else 0.14)
    popup_tween = create_tween().set_parallel(true)
    popup_tween.tween_property(popup_panel, "modulate:a", 1.0 if show else 0.0, duration).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    if not motion.reduced_motion:
        motion.spring_property(popup_panel, "scale", Vector2.ONE if show else Vector2(0.94, 0.94), 0.26 if show else 0.18, 0.9 if show else 1.0)
        motion.spring_property(popup_panel, "position", rest_position, 0.30 if show else 0.20, 1.0)
    if not show:
        popup_tween.chain().tween_callback(_free_overlay)

func _animate_chevron(open: bool) -> void:
    var target := PI if open else 0.0
    if motion.reduced_motion:
        chevron.rotation = target
        return
    motion.spring_property(chevron, "rotation", target, 0.28, 1.0)

func _focus_selected_item() -> void:
    if popup_panel == null or not is_instance_valid(popup_panel):
        return
    var menu := popup_panel.get_child(0)
    if selected_index >= 0 and selected_index < menu.get_child_count():
        menu.get_child(selected_index).grab_focus()

func _free_overlay() -> void:
    if overlay != null and is_instance_valid(overlay):
        overlay.queue_free()
    overlay = null
    popup_panel = null
    grab_focus()

func _unhandled_key_input(event: InputEvent) -> void:
    if overlay != null and is_instance_valid(overlay) and event.is_action_pressed("ui_cancel"):
        _close_popup()
        get_viewport().set_input_as_handled()

func _exit_tree() -> void:
    if overlay != null and is_instance_valid(overlay):
        overlay.queue_free()

func _field_box(fill: Color, border: Color, border_width: int) -> StyleBoxFlat:
    var style: StyleBoxFlat = tokens.button_style(fill, Color.TRANSPARENT, 12)
    style.content_margin_left = 14
    style.content_margin_right = 42
    style.shadow_color = Color.TRANSPARENT
    style.shadow_size = 0
    style.border_width_left = 0
    style.border_width_top = 0
    style.border_width_right = 0
    style.border_width_bottom = 0
    return style

func _popup_box() -> StyleBoxFlat:
    var style: StyleBoxFlat = tokens.panel(tokens.surface, 8)
    style.content_margin_left = MENU_PADDING
    style.content_margin_top = MENU_PADDING
    style.content_margin_right = MENU_PADDING
    style.content_margin_bottom = MENU_PADDING
    style.shadow_color = Color(tokens.shadow.r, tokens.shadow.g, tokens.shadow.b, 0.32 if tokens.mode == "dark" else 0.14)
    style.shadow_size = 9 if tokens.mode == "dark" else 6
    style.shadow_offset = Vector2(0, 4 if tokens.mode == "dark" else 3)
    return style
