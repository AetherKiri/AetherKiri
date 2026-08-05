extends Button

signal expanded_changed(expanded: bool)

var tokens
var motion
var expanded := false
var chevron: TextureRect

func setup(design_tokens, motion_system, label: String, chevron_texture: Texture2D, initial_value: bool) -> void:
    tokens = design_tokens
    motion = motion_system
    text = label
    expanded = initial_value
    alignment = HORIZONTAL_ALIGNMENT_LEFT
    clip_text = true
    focus_mode = Control.FOCUS_ALL
    mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
    custom_minimum_size.y = 52

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
    chevron.offset_left = -31
    chevron.offset_top = -9
    chevron.offset_right = -13
    chevron.offset_bottom = 9
    chevron.pivot_offset = Vector2(9, 9)
    chevron.rotation = PI * 0.5 if expanded else 0.0
    add_child(chevron)
    pressed.connect(func(): set_expanded(not expanded, true))

func set_expanded(value: bool, animate: bool) -> void:
    if value == expanded:
        return
    expanded = value
    var target := PI * 0.5 if expanded else 0.0
    if not animate or motion.reduced_motion:
        chevron.rotation = target
    else:
        motion.spring_property(chevron, "rotation", target, 0.28, 1.0)
    expanded_changed.emit(expanded)
