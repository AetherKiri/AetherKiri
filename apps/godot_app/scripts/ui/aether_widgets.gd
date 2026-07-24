extends RefCounted

const CONTROL_HEIGHT := 44.0
const ICON_BUTTON_SIZE := 44.0

var tokens
var motion

func _init(design_tokens, motion_system) -> void:
    tokens = design_tokens
    motion = motion_system

func primary_button(button: Button) -> Button:
    _prepare_button(button, 15)
    button.add_theme_color_override("font_color", Color.WHITE)
    button.add_theme_color_override("font_hover_color", Color.WHITE)
    button.add_theme_color_override("font_pressed_color", Color.WHITE)
    button.add_theme_color_override("font_focus_color", Color.WHITE)
    button.add_theme_color_override("font_disabled_color", tokens.text_tertiary)
    _set_button_boxes(
        button,
        _primary_box(tokens.accent, false),
        _primary_box(tokens.accent.lightened(0.055), true),
        _primary_box(tokens.accent.darkened(0.10), false),
        _focus_box(8),
        _control_box(Color(tokens.surface_raised.r, tokens.surface_raised.g, tokens.surface_raised.b, 0.46), tokens.separator, false)
    )
    return button

func destructive_button(button: Button) -> Button:
    _prepare_button(button, 15)
    button.add_theme_color_override("font_color", Color.WHITE)
    button.add_theme_color_override("font_hover_color", Color.WHITE)
    button.add_theme_color_override("font_pressed_color", Color.WHITE)
    button.add_theme_color_override("font_focus_color", Color.WHITE)
    button.add_theme_color_override("font_disabled_color", Color(1, 1, 1, 0.42))
    _set_button_boxes(
        button,
        _primary_box(tokens.danger, false),
        _primary_box(tokens.danger.lightened(0.055), true),
        _primary_box(tokens.danger.darkened(0.10), false),
        _focus_box(8),
        _control_box(Color(tokens.danger.r, tokens.danger.g, tokens.danger.b, 0.20), tokens.separator, false)
    )
    return button

func secondary_button(button: Button, destructive: bool = false) -> Button:
    _prepare_button(button, 15)
    var foreground: Color = tokens.danger if destructive else tokens.text_primary
    button.add_theme_color_override("font_color", foreground)
    button.add_theme_color_override("font_hover_color", foreground)
    button.add_theme_color_override("font_pressed_color", foreground)
    button.add_theme_color_override("font_focus_color", foreground)
    button.add_theme_color_override("font_disabled_color", Color(foreground.r, foreground.g, foreground.b, 0.38))
    var tint: Color = tokens.danger if destructive else tokens.accent
    var hover_fill := Color(tint.r, tint.g, tint.b, 0.12 if destructive else 0.10)
    var press_fill := Color(tint.r, tint.g, tint.b, 0.18)
    _set_button_boxes(
        button,
        _control_box(tokens.glass_material, tokens.separator, false),
        _control_box(hover_fill, Color(tint.r, tint.g, tint.b, 0.48), true),
        _control_box(press_fill, Color(tint.r, tint.g, tint.b, 0.72), false),
        _focus_box(8),
        _control_box(Color(tokens.surface_raised.r, tokens.surface_raised.g, tokens.surface_raised.b, 0.32), tokens.separator, false)
    )
    return button

func toolbar_button(button: Button, selected: bool = false) -> Button:
    _prepare_button(button, 14)
    button.custom_minimum_size = Vector2(ICON_BUTTON_SIZE, ICON_BUTTON_SIZE)
    var foreground: Color = tokens.accent if selected else tokens.text_secondary
    button.add_theme_color_override("font_color", foreground)
    button.add_theme_color_override("font_hover_color", tokens.accent if selected else tokens.text_primary)
    button.add_theme_color_override("font_pressed_color", tokens.accent)
    button.add_theme_color_override("font_focus_color", foreground)
    button.add_theme_color_override("icon_normal_color", foreground)
    button.add_theme_color_override("icon_hover_color", tokens.accent if selected else tokens.text_primary)
    button.add_theme_color_override("icon_pressed_color", tokens.accent)
    button.add_theme_color_override("icon_focus_color", foreground)
    _set_button_boxes(
        button,
        _control_box(tokens.accent_fill if selected else Color.TRANSPARENT, Color.TRANSPARENT, false),
        _control_box(tokens.surface_hover, tokens.highlight, true),
        _control_box(tokens.accent_fill, Color(tokens.accent.r, tokens.accent.g, tokens.accent.b, 0.42), false),
        _focus_box(8),
        _control_box(Color.TRANSPARENT, Color.TRANSPARENT, false)
    )
    return button

func navigation_button(button: Button, selected: bool = false) -> Button:
    _prepare_button(button, 15)
    button.custom_minimum_size.y = 46.0
    var foreground: Color = tokens.accent if selected else tokens.text_secondary
    button.add_theme_color_override("font_color", foreground)
    button.add_theme_color_override("font_hover_color", tokens.accent if selected else tokens.text_primary)
    button.add_theme_color_override("font_pressed_color", tokens.accent)
    button.add_theme_color_override("font_focus_color", foreground)
    button.add_theme_color_override("icon_normal_color", foreground)
    button.add_theme_color_override("icon_hover_color", tokens.accent if selected else tokens.text_primary)
    button.add_theme_color_override("icon_pressed_color", tokens.accent)
    button.add_theme_color_override("icon_focus_color", foreground)
    _set_button_boxes(
        button,
        tokens.button_style(tokens.accent_fill if selected else Color.TRANSPARENT, Color.TRANSPARENT, 8),
        tokens.button_style(Color(tokens.accent.r, tokens.accent.g, tokens.accent.b, 0.23) if selected else tokens.surface_raised, Color.TRANSPARENT, 8),
        tokens.button_style(tokens.accent_fill, Color.TRANSPARENT, 8),
        _focus_box(8),
        tokens.button_style(Color.TRANSPARENT, Color.TRANSPARENT, 8)
    )
    return button

func disclosure_button(button: Button) -> Button:
    _prepare_button(button, 15)
    button.add_theme_color_override("font_color", tokens.text_primary)
    button.add_theme_color_override("font_hover_color", tokens.text_primary)
    button.add_theme_color_override("font_pressed_color", tokens.text_primary)
    _set_button_boxes(
        button,
        _control_box(Color.TRANSPARENT, Color.TRANSPARENT, false),
        _control_box(tokens.surface_hover, Color.TRANSPARENT, false),
        _control_box(tokens.accent_fill, Color.TRANSPARENT, false),
        _focus_box(8),
        _control_box(Color.TRANSPARENT, Color.TRANSPARENT, false)
    )
    return button

func line_edit(input: LineEdit) -> LineEdit:
    input.custom_minimum_size.y = CONTROL_HEIGHT
    input.add_theme_font_size_override("font_size", 15)
    input.add_theme_color_override("font_color", tokens.text_primary)
    input.add_theme_color_override("font_placeholder_color", tokens.text_tertiary)
    input.add_theme_color_override("caret_color", tokens.accent)
    input.add_theme_color_override("selection_color", tokens.accent_fill)
    input.add_theme_constant_override("minimum_character_width", 12)
    input.add_theme_stylebox_override("normal", _field_box(false))
    input.add_theme_stylebox_override("focus", _field_box(true))
    input.add_theme_stylebox_override("read_only", _disabled_field_box())
    return input

func _prepare_button(button: Button, font_size: int) -> void:
    button.focus_mode = Control.FOCUS_ALL
    button.custom_minimum_size.y = maxf(button.custom_minimum_size.y, CONTROL_HEIGHT)
    button.add_theme_font_size_override("font_size", font_size)
    button.add_theme_constant_override("h_separation", 8)
    motion.bind_pressable(button)

func _set_button_boxes(button: Button, normal: StyleBox, hover: StyleBox, pressed: StyleBox, focus: StyleBox, disabled: StyleBox) -> void:
    button.add_theme_stylebox_override("normal", normal)
    button.add_theme_stylebox_override("hover", hover)
    button.add_theme_stylebox_override("pressed", pressed)
    button.add_theme_stylebox_override("focus", focus)
    button.add_theme_stylebox_override("disabled", disabled)

func _primary_box(fill: Color, elevated: bool) -> StyleBoxFlat:
    var style: StyleBoxFlat = tokens.button_style(fill, Color(1, 1, 1, 0.20), 8)
    style.border_width_bottom = 0
    var shadow_alpha := (0.34 if elevated else 0.24) if tokens.mode == "dark" else (0.15 if elevated else 0.10)
    style.shadow_color = Color(tokens.shadow.r, tokens.shadow.g, tokens.shadow.b, shadow_alpha)
    style.shadow_size = (10 if elevated else 7) if tokens.mode == "dark" else (6 if elevated else 4)
    style.shadow_offset = Vector2(0, 4 if elevated else 3)
    return style

func _control_box(fill: Color, border: Color, elevated: bool) -> StyleBoxFlat:
    var style: StyleBoxFlat = tokens.button_style(fill, border, 8)
    var shadow_alpha := (0.24 if elevated else 0.12) if tokens.mode == "dark" else (0.09 if elevated else 0.05)
    style.shadow_color = Color(tokens.shadow.r, tokens.shadow.g, tokens.shadow.b, shadow_alpha)
    style.shadow_size = (8 if elevated else 4) if tokens.mode == "dark" else (5 if elevated else 2)
    style.shadow_offset = Vector2(0, 3 if elevated else 1)
    return style

func _field_box(focused: bool) -> StyleBoxFlat:
    var border: Color = tokens.accent if focused else tokens.separator
    var width := 2 if focused else 1
    var style: StyleBoxFlat = tokens.panel(tokens.background_raised, 8, border, width)
    style.content_margin_left = 13
    style.content_margin_top = 9
    style.content_margin_right = 13
    style.content_margin_bottom = 9
    style.shadow_color = Color(tokens.shadow.r, tokens.shadow.g, tokens.shadow.b, 0.18 if tokens.mode == "dark" else 0.07)
    style.shadow_size = (5 if focused else 3) if tokens.mode == "dark" else (3 if focused else 2)
    style.shadow_offset = Vector2(0, 1)
    return style

func _disabled_field_box() -> StyleBoxFlat:
    return _control_box(Color(tokens.surface_raised.r, tokens.surface_raised.g, tokens.surface_raised.b, 0.34), tokens.separator, false)

func _focus_box(radius: int) -> StyleBoxFlat:
    var style: StyleBoxFlat = tokens.focus_style(radius)
    style.border_color = Color(tokens.accent.r, tokens.accent.g, tokens.accent.b, 0.92)
    style.shadow_color = Color(tokens.accent.r, tokens.accent.g, tokens.accent.b, 0.20)
    style.shadow_size = 5
    return style
