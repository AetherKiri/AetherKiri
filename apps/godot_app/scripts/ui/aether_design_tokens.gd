extends RefCounted

const DARK := "dark"
const LIGHT := "classic"

const RADIUS_SMALL := 8
const RADIUS_MEDIUM := 12
const RADIUS_CARD := 18
const SIDEBAR_WIDTH := 248.0
const SIDEBAR_COLLAPSED_WIDTH := 80.0
const CONTENT_MAX_WIDTH := 1180.0
const PAGE_GUTTER := 32.0
const PAGE_GUTTER_COMPACT := 20.0
const CONTROL_HEIGHT := 48.0
const TOOLBAR_HEIGHT := 72.0

var mode := DARK
var background := Color("f5f5f7")
var background_raised := Color("ffffff")
var sidebar_material := Color("ffffff")
var glass_material := Color("ffffff")
var surface := Color("fbfbfd")
var surface_raised := Color("ffffff")
var surface_hover := Color("e8e8ed")
var text_primary := Color("1d1d1f")
var text_secondary := Color("6e6e73")
var text_tertiary := Color("86868b")
var accent := Color("0071e3")
var accent_fill := Color(0.0, 0.443, 0.89, 0.12)
var success := Color("16a34a")
var warning := Color("eab308")
var danger := Color("dc2626")
var separator := Color("d2d2d7")
var shadow := Color(0.0, 0.0, 0.0, 0.08)

func configure(next_mode: String) -> void:
    mode = LIGHT if next_mode == LIGHT else DARK
    if mode == DARK:
        background = Color("000000")
        background_raised = Color("1d1d1f")
        sidebar_material = Color("000000")
        glass_material = Color("1d1d1f")
        surface = Color("272729")
        surface_raised = Color("2a2a2c")
        surface_hover = Color("424245")
        text_primary = Color("f5f5f7")
        text_secondary = Color("a1a1a6")
        text_tertiary = Color("86868b")
        accent = Color("2997ff")
        accent_fill = Color(0.161, 0.592, 1.0, 0.18)
        success = Color("16a34a")
        warning = Color("eab308")
        danger = Color("dc2626")
        separator = Color(1.0, 1.0, 1.0, 0.16)
        shadow = Color(0.0, 0.0, 0.0, 0.28)
        return

    background = Color("f5f5f7")
    background_raised = Color("ffffff")
    sidebar_material = Color("ffffff")
    glass_material = Color("ffffff")
    surface = Color("fbfbfd")
    surface_raised = Color("ffffff")
    surface_hover = Color("e8e8ed")
    text_primary = Color("1d1d1f")
    text_secondary = Color("6e6e73")
    text_tertiary = Color("86868b")
    accent = Color("0071e3")
    accent_fill = Color(0.0, 0.443, 0.89, 0.12)
    success = Color("16a34a")
    warning = Color("eab308")
    danger = Color("dc2626")
    separator = Color("d2d2d7")
    shadow = Color(0.0, 0.0, 0.0, 0.08)

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
    var style := panel(glass_material if not elevated else surface_raised, RADIUS_CARD)
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
    var fill := surface_hover if hovered else surface_raised
    if pressed:
        fill = accent_fill
    return panel(fill, RADIUS_CARD)

func detail_outline_style() -> StyleBoxFlat:
    return panel(surface_raised, RADIUS_CARD, separator, 1)

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
    return panel(surface_hover, radius)
