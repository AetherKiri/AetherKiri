extends RefCounted

const DARK := "dark"
const LIGHT := "classic"

const RADIUS_SMALL := 6
const RADIUS_MEDIUM := 8
const RADIUS_CARD := 12
const SIDEBAR_WIDTH := 232.0
const SIDEBAR_COLLAPSED_WIDTH := 88.0
const CONTENT_MAX_WIDTH := 1180.0
const PAGE_GUTTER := 32.0
const PAGE_GUTTER_COMPACT := 20.0
const CONTROL_HEIGHT := 48.0
const TOOLBAR_HEIGHT := 72.0

var mode := DARK
var background := Color("181715")
var background_raised := Color("1f1e1b")
var sidebar_material := Color("181715")
var glass_material := Color("252320")
var surface := Color("252320")
var surface_raised := Color("302d29")
var surface_hover := Color("3a3530")
var text_primary := Color("faf9f5")
var text_secondary := Color("a09d96")
var text_tertiary := Color("6c6a64")
var accent := Color("cc785c")
var accent_fill := Color(0.80, 0.47, 0.36, 0.18)
var success := Color("5db872")
var warning := Color("d4a017")
var danger := Color("c64545")
var separator := Color(1.0, 1.0, 1.0, 0.09)
var highlight := Color(1.0, 1.0, 1.0, 0.13)
var shadow := Color(0.0, 0.0, 0.0, 0.34)

func configure(next_mode: String) -> void:
    mode = LIGHT if next_mode == LIGHT else DARK
    if mode == LIGHT:
        background = Color("faf9f5")
        background_raised = Color("f5f0e8")
        sidebar_material = Color("f5f0e8")
        glass_material = Color("faf9f5")
        surface = Color("efe9de")
        surface_raised = Color("e8e0d2")
        surface_hover = Color("e1d7c7")
        text_primary = Color("141413")
        text_secondary = Color("6c6a64")
        text_tertiary = Color("8e8b82")
        accent = Color("cc785c")
        accent_fill = Color(0.80, 0.47, 0.36, 0.14)
        success = Color("5db872")
        warning = Color("d4a017")
        danger = Color("c64545")
        separator = Color("e6dfd8")
        highlight = Color(1.0, 1.0, 1.0, 0.58)
        shadow = Color(0.08, 0.08, 0.07, 0.12)
        return

    background = Color("181715")
    background_raised = Color("1f1e1b")
    sidebar_material = Color("181715")
    glass_material = Color("252320")
    surface = Color("252320")
    surface_raised = Color("302d29")
    surface_hover = Color("3a3530")
    text_primary = Color("faf9f5")
    text_secondary = Color("a09d96")
    text_tertiary = Color("6c6a64")
    accent = Color("cc785c")
    accent_fill = Color(0.80, 0.47, 0.36, 0.18)
    success = Color("5db872")
    warning = Color("d4a017")
    danger = Color("c64545")
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
    var style := panel(fill, RADIUS_CARD, border, 1)
    style.shadow_color = shadow
    style.shadow_size = 14 if hovered else 6
    style.shadow_offset = Vector2(0, 6 if hovered else 2)
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
