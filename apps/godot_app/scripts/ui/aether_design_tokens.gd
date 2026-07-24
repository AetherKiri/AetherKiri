extends RefCounted

const DARK := "dark"
const LIGHT := "classic"

const RADIUS_SMALL := 6
const RADIUS_MEDIUM := 8
const SIDEBAR_WIDTH := 232.0
const CONTENT_MAX_WIDTH := 1180.0
const PAGE_GUTTER := 32.0
const PAGE_GUTTER_COMPACT := 20.0
const CONTROL_HEIGHT := 48.0
const TOOLBAR_HEIGHT := 72.0

var mode := DARK
var background := Color(0.043, 0.047, 0.055, 1.0)
var background_raised := Color(0.061, 0.065, 0.076, 1.0)
var sidebar_material := Color(0.090, 0.094, 0.108, 0.88)
var glass_material := Color(0.125, 0.129, 0.145, 0.76)
var surface := Color(0.105, 0.109, 0.125, 1.0)
var surface_raised := Color(0.137, 0.141, 0.161, 1.0)
var surface_hover := Color(0.174, 0.180, 0.204, 1.0)
var text_primary := Color(0.965, 0.965, 0.976, 1.0)
var text_secondary := Color(0.667, 0.675, 0.714, 1.0)
var text_tertiary := Color(0.494, 0.502, 0.541, 1.0)
var accent := Color(0.039, 0.518, 1.0, 1.0)
var accent_fill := Color(0.039, 0.518, 1.0, 0.18)
var success := Color(0.188, 0.820, 0.345, 1.0)
var warning := Color(1.0, 0.624, 0.039, 1.0)
var danger := Color(1.0, 0.271, 0.227, 1.0)
var separator := Color(1.0, 1.0, 1.0, 0.09)
var highlight := Color(1.0, 1.0, 1.0, 0.13)
var shadow := Color(0.0, 0.0, 0.0, 0.34)

func configure(next_mode: String) -> void:
    mode = LIGHT if next_mode == LIGHT else DARK
    if mode == LIGHT:
        background = Color(0.949, 0.953, 0.961, 1.0)
        background_raised = Color(0.976, 0.976, 0.984, 1.0)
        sidebar_material = Color(0.941, 0.945, 0.957, 0.90)
        glass_material = Color(1.0, 1.0, 1.0, 0.78)
        surface = Color(1.0, 1.0, 1.0, 1.0)
        surface_raised = Color(0.929, 0.933, 0.945, 1.0)
        surface_hover = Color(0.886, 0.894, 0.914, 1.0)
        text_primary = Color(0.110, 0.114, 0.125, 1.0)
        text_secondary = Color(0.380, 0.388, 0.420, 1.0)
        text_tertiary = Color(0.525, 0.533, 0.565, 1.0)
        accent = Color(0.0, 0.478, 1.0, 1.0)
        accent_fill = Color(0.0, 0.478, 1.0, 0.12)
        success = Color(0.196, 0.690, 0.278, 1.0)
        warning = Color(1.0, 0.584, 0.0, 1.0)
        danger = Color(1.0, 0.231, 0.188, 1.0)
        separator = Color(0.0, 0.0, 0.0, 0.10)
        highlight = Color(1.0, 1.0, 1.0, 0.72)
        shadow = Color(0.0, 0.0, 0.0, 0.14)
        return

    background = Color(0.043, 0.047, 0.055, 1.0)
    background_raised = Color(0.061, 0.065, 0.076, 1.0)
    sidebar_material = Color(0.090, 0.094, 0.108, 0.88)
    glass_material = Color(0.125, 0.129, 0.145, 0.76)
    surface = Color(0.105, 0.109, 0.125, 1.0)
    surface_raised = Color(0.137, 0.141, 0.161, 1.0)
    surface_hover = Color(0.174, 0.180, 0.204, 1.0)
    text_primary = Color(0.965, 0.965, 0.976, 1.0)
    text_secondary = Color(0.667, 0.675, 0.714, 1.0)
    text_tertiary = Color(0.494, 0.502, 0.541, 1.0)
    accent = Color(0.039, 0.518, 1.0, 1.0)
    accent_fill = Color(0.039, 0.518, 1.0, 0.18)
    success = Color(0.188, 0.820, 0.345, 1.0)
    warning = Color(1.0, 0.624, 0.039, 1.0)
    danger = Color(1.0, 0.271, 0.227, 1.0)
    separator = Color(1.0, 1.0, 1.0, 0.09)
    highlight = Color(1.0, 1.0, 1.0, 0.13)
    shadow = Color(0.0, 0.0, 0.0, 0.34)

func panel(fill: Color, radius: int = RADIUS_MEDIUM, border: Color = Color.TRANSPARENT, border_width: int = 0) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = fill
    style.border_color = border
    style.border_width_left = border_width
    style.border_width_top = border_width
    style.border_width_right = border_width
    style.border_width_bottom = border_width
    style.corner_radius_top_left = radius
    style.corner_radius_top_right = radius
    style.corner_radius_bottom_left = radius
    style.corner_radius_bottom_right = radius
    return style

func material_panel(elevated: bool = false) -> StyleBoxFlat:
    var style := panel(glass_material if not elevated else surface, RADIUS_MEDIUM, separator, 1)
    style.content_margin_left = 16
    style.content_margin_top = 14
    style.content_margin_right = 16
    style.content_margin_bottom = 14
    if elevated:
        style.shadow_color = shadow
        style.shadow_size = 18
        style.shadow_offset = Vector2(0, 8)
    return style

func card_style(hovered: bool = false, pressed: bool = false) -> StyleBoxFlat:
    var fill := surface_raised if hovered else surface
    if pressed:
        fill = Color(
            surface_raised.r + accent_fill.r * 0.08,
            surface_raised.g + accent_fill.g * 0.08,
            surface_raised.b + accent_fill.b * 0.08,
            1.0
        )
    var border := accent if pressed else (highlight if hovered else separator)
    var style := panel(fill, RADIUS_MEDIUM, border, 1)
    style.shadow_color = shadow
    style.shadow_size = 18 if hovered else 10
    style.shadow_offset = Vector2(0, 8 if hovered else 4)
    return style

func sidebar_panel() -> StyleBoxFlat:
    var style := panel(sidebar_material, 0, separator, 1)
    style.border_width_left = 0
    style.border_width_top = 0
    style.border_width_bottom = 0
    style.content_margin_left = 18
    style.content_margin_top = 20
    style.content_margin_right = 18
    style.content_margin_bottom = 20
    return style

func button_style(fill: Color, border: Color = Color.TRANSPARENT, radius: int = RADIUS_MEDIUM) -> StyleBoxFlat:
    var style := panel(fill, radius, border, 1 if border.a > 0.0 else 0)
    style.content_margin_left = 14
    style.content_margin_top = 10
    style.content_margin_right = 14
    style.content_margin_bottom = 10
    return style

func focus_style(radius: int = RADIUS_MEDIUM) -> StyleBoxFlat:
    var style := panel(Color.TRANSPARENT, radius, accent, 2)
    style.expand_margin_left = 2
    style.expand_margin_top = 2
    style.expand_margin_right = 2
    style.expand_margin_bottom = 2
    return style
