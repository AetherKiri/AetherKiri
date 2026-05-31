extends Control

const BACKENDS := ["Godot Native", "GPU Bridge", "Debug CPU"]
const SETTINGS_KEY := "aether_kiri/render_backend"
const GAME_PATH_KEY := "aether_kiri/game_path"
const GAME_LIST_FILE := "user://aetherkiri_games.json"
const SETTINGS_FILE := "user://aetherkiri_settings.cfg"

const ENGINE_RESULT_OK := 0
const STARTUP_IDLE := 0
const STARTUP_RUNNING := 1
const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3

const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3
const POINTER_SCROLL := 4

var backend: OptionButton
var game_path: LineEdit
var restart_notice: Label
var viewport: TextureRect
var perf: Label
var log_view: TextEdit
var shell_root: Control
var home_view: Control
var settings_view: ScrollContainer
var detail_view: Control
var detail_scroll: ScrollContainer
var game_view: Control
var modal_layer: Control
var loading_panel: PanelContainer
var game_scroll: ScrollContainer
var game_list: GridContainer
var home_actions: HBoxContainer
var empty_state: Control
var save_button: Button
var bg_rect: ColorRect
var selected_game := {}
var known_games: Array[Dictionary] = []
var show_perf_monitor := true
var lock_landscape := true
var frame_limit_enabled := false
var target_fps := 80
var plugin_trace := false
var mock_enabled := true
var console_log_file := true
var trace_log := false
var export_scripts := false
var dirty_settings := false
var active_game_path := ""
var active_game_started_msec := 0
var detail_touch_scroll_active := false
var rounded_card_shader: Shader

var player: AetherKiriPlayer
var selected_backend := "Godot Native"
var game_running := false
var render_errors := 0
var last_renderer_info_logged := ""
var last_texture_size := Vector2i.ZERO
var capture_after_open_path := ""
var capture_after_open_done := false
var capture_after_open_delay_sec := 0.0
var capture_after_open_ready_usec := 0
var auto_probe_clicks: Array[Vector2] = []
var auto_probe_running := false
var auto_probe_done := false
var log_drain_accum := 0.0
var perf_accum := 0.0
var perf_log_accum := 0.0
var state_log_accum := 0.0
var perf_log_interval := PERF_LOG_INTERVAL
var frame_spike_ms := 0.0
var verbose_render_log := false
var perf_log_file: FileAccess
var log_lines: PackedStringArray = []
const LOG_DRAIN_INTERVAL := 0.25
const PERF_UPDATE_INTERVAL := 0.25
const PERF_LOG_INTERVAL := 2.0
const MAX_LOG_LINES := 240
const RENDER_SURFACE_SIZE := Vector2i(1280, 720)
const COLOR_BG := Color(0.944, 0.932, 0.895, 1.0)
const COLOR_CARD := Color(0.985, 0.98, 0.955, 1.0)
const COLOR_TEXT := Color(0.12, 0.11, 0.10, 1.0)
const COLOR_MUTED := Color(0.46, 0.45, 0.42, 1.0)
const COLOR_ACCENT := Color(0.78, 0.35, 0.22, 1.0)
const COLOR_ACCENT_SOFT := Color(0.90, 0.72, 0.64, 1.0)
const COLOR_LINE := Color(0.84, 0.82, 0.76, 1.0)
const HOME_CARD_SIZE := Vector2(260, 350)

func _build_ui() -> void:
    bg_rect = ColorRect.new()
    bg_rect.color = COLOR_BG
    bg_rect.set_anchors_preset(Control.PRESET_FULL_RECT)
    add_child(bg_rect)

    game_path = LineEdit.new()
    game_path.visible = false
    add_child(game_path)

    backend = OptionButton.new()
    backend.visible = false
    add_child(backend)

    viewport = TextureRect.new()
    viewport.name = "GameViewport"
    viewport.set_anchors_preset(Control.PRESET_FULL_RECT)
    viewport.mouse_filter = Control.MOUSE_FILTER_STOP
    viewport.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    viewport.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    viewport.visible = false
    add_child(viewport)

    game_view = Control.new()
    game_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    game_view.mouse_filter = Control.MOUSE_FILTER_IGNORE
    game_view.visible = false
    add_child(game_view)

    shell_root = Control.new()
    shell_root.set_anchors_preset(Control.PRESET_FULL_RECT)
    add_child(shell_root)

    _build_home_view()
    _build_settings_view()
    _build_detail_view()
    _build_modal_layer()

    perf = Label.new()
    perf.position = Vector2(24, 18)
    perf.add_theme_font_size_override("font_size", 13)
    perf.add_theme_color_override("font_color", Color(1, 1, 1, 0.92))
    perf.visible = false
    game_view.add_child(perf)

    restart_notice = Label.new()
    restart_notice.position = Vector2(24, 44)
    restart_notice.add_theme_font_size_override("font_size", 14)
    restart_notice.add_theme_color_override("font_color", Color(1, 0.82, 0.65, 1))
    restart_notice.visible = false
    game_view.add_child(restart_notice)

    _build_loading_panel()
    _fit_full_rects()

func _load_shell_settings() -> void:
    var cfg := ConfigFile.new()
    if cfg.load(SETTINGS_FILE) != OK:
        return
    selected_backend = String(cfg.get_value("rendering", "backend", selected_backend))
    show_perf_monitor = bool(cfg.get_value("rendering", "perf_overlay", show_perf_monitor))
    frame_limit_enabled = bool(cfg.get_value("rendering", "fps_limit_enabled", frame_limit_enabled))
    target_fps = int(cfg.get_value("rendering", "target_fps", target_fps))
    lock_landscape = bool(cfg.get_value("rendering", "force_landscape", lock_landscape))
    plugin_trace = bool(cfg.get_value("developer", "plugin_trace", plugin_trace))
    mock_enabled = bool(cfg.get_value("developer", "mock_enabled", mock_enabled))
    console_log_file = bool(cfg.get_value("developer", "console_log_file", console_log_file))
    trace_log = bool(cfg.get_value("developer", "trace_log", trace_log))
    export_scripts = bool(cfg.get_value("developer", "export_scripts", export_scripts))

func _save_shell_settings() -> void:
    var cfg := ConfigFile.new()
    cfg.set_value("rendering", "backend", selected_backend)
    cfg.set_value("rendering", "perf_overlay", show_perf_monitor)
    cfg.set_value("rendering", "fps_limit_enabled", frame_limit_enabled)
    cfg.set_value("rendering", "target_fps", target_fps)
    cfg.set_value("rendering", "force_landscape", lock_landscape)
    cfg.set_value("developer", "plugin_trace", plugin_trace)
    cfg.set_value("developer", "mock_enabled", mock_enabled)
    cfg.set_value("developer", "console_log_file", console_log_file)
    cfg.set_value("developer", "trace_log", trace_log)
    cfg.set_value("developer", "export_scripts", export_scripts)
    cfg.save(SETTINGS_FILE)
    ProjectSettings.set_setting(SETTINGS_KEY, selected_backend)
    ProjectSettings.save()
    _apply_engine_options()
    _apply_shell_runtime_settings()
    dirty_settings = false
    if save_button != null:
        save_button.disabled = true

func _mark_settings_dirty() -> void:
    dirty_settings = true
    if save_button != null:
        save_button.disabled = false

func _apply_engine_options() -> void:
    if player == null:
        return
    player.set_engine_option("fps_limit", str(target_fps) if frame_limit_enabled else "0")
    player.set_engine_option("plugin_trace", "1" if plugin_trace else "0")
    player.set_engine_option("mock_enabled", "1" if mock_enabled else "0")
    player.set_engine_option("console_log_file", "1" if console_log_file else "0")
    player.set_engine_option("trace_log", "1" if trace_log else "0")
    player.set_engine_option("export_scripts", "1" if export_scripts else "0")

func _apply_shell_runtime_settings() -> void:
    if OS.get_name() == "iOS" or OS.get_name() == "Android":
        var orientation := DisplayServer.SCREEN_LANDSCAPE if lock_landscape else DisplayServer.SCREEN_SENSOR
        DisplayServer.screen_set_orientation(orientation)

func _fit_full_rects() -> void:
    var window_size := get_viewport_rect().size
    anchor_left = 0.0
    anchor_top = 0.0
    anchor_right = 0.0
    anchor_bottom = 0.0
    position = Vector2.ZERO
    size = window_size
    var controls: Array[Control] = [bg_rect, viewport, game_view, shell_root, home_view, settings_view, detail_view, detail_scroll, modal_layer]
    for control in controls:
        if control == null:
            continue
        control.set_anchors_preset(Control.PRESET_FULL_RECT)
        control.offset_left = 0.0
        control.offset_top = 0.0
        control.offset_right = 0.0
        control.offset_bottom = 0.0
    _layout_home_view(window_size)

func _layout_home_view(window_size: Vector2) -> void:
    if game_scroll == null or game_list == null:
        return
    var margin := 32.0
    var list_top := 164.0
    var bottom_reserved := 132.0
    var list_width := maxf(260.0, window_size.x - margin * 2.0)
    var list_height := maxf(160.0, window_size.y - list_top - bottom_reserved)
    game_scroll.position = Vector2(margin, list_top)
    game_scroll.size = Vector2(list_width, list_height)
    game_scroll.custom_minimum_size = game_scroll.size

    var gap := 18.0
    var columns := maxi(1, int(floor((list_width + gap) / (HOME_CARD_SIZE.x + gap))))
    game_list.columns = columns
    game_list.custom_minimum_size = Vector2(list_width, 0)

    if home_actions != null:
        home_actions.anchor_left = 1.0
        home_actions.anchor_top = 1.0
        home_actions.anchor_right = 1.0
        home_actions.anchor_bottom = 1.0
        home_actions.offset_left = -390.0
        home_actions.offset_top = -108.0
        home_actions.offset_right = -32.0
        home_actions.offset_bottom = -44.0
        home_actions.move_to_front()

func _build_home_view() -> void:
    home_view = Control.new()
    home_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    shell_root.add_child(home_view)

    var title := Label.new()
    title.text = "AetherKiri"
    title.position = Vector2(38, 96)
    title.add_theme_font_size_override("font_size", 28)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    home_view.add_child(title)

    var settings_button := _icon_button("⚙")
    settings_button.anchor_left = 1.0
    settings_button.anchor_right = 1.0
    settings_button.position = Vector2(-86, 92)
    settings_button.pressed.connect(_show_settings)
    home_view.add_child(settings_button)

    game_scroll = ScrollContainer.new()
    game_scroll.position = Vector2(32, 164)
    game_scroll.size = Vector2(390, 500)
    game_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    game_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
    home_view.add_child(game_scroll)

    game_list = GridContainer.new()
    game_list.columns = 1
    game_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    game_list.add_theme_constant_override("h_separation", 18)
    game_list.add_theme_constant_override("v_separation", 18)
    game_scroll.add_child(game_list)

    empty_state = VBoxContainer.new()
    empty_state.anchor_left = 0.5
    empty_state.anchor_top = 0.5
    empty_state.anchor_right = 0.5
    empty_state.anchor_bottom = 0.5
    empty_state.position = Vector2(-260, -120)
    empty_state.size = Vector2(520, 240)
    empty_state.add_theme_constant_override("separation", 18)
    home_view.add_child(empty_state)

    var empty_icon := Label.new()
    empty_icon.text = "▧"
    empty_icon.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    empty_icon.add_theme_font_size_override("font_size", 64)
    empty_icon.add_theme_color_override("font_color", COLOR_ACCENT_SOFT)
    empty_state.add_child(empty_icon)

    var empty_title := Label.new()
    empty_title.text = "尚未添加任何游戏"
    empty_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    empty_title.add_theme_font_size_override("font_size", 30)
    empty_title.add_theme_color_override("font_color", COLOR_TEXT)
    empty_state.add_child(empty_title)

    var empty_help := Label.new()
    empty_help.text = _empty_help_text()
    empty_help.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    empty_help.add_theme_font_size_override("font_size", 22)
    empty_help.add_theme_color_override("font_color", COLOR_MUTED)
    empty_state.add_child(empty_help)

    home_actions = HBoxContainer.new()
    home_actions.anchor_left = 1.0
    home_actions.anchor_top = 1.0
    home_actions.anchor_right = 1.0
    home_actions.anchor_bottom = 1.0
    home_actions.position = Vector2(-390, -108)
    home_actions.size = Vector2(358, 64)
    home_actions.add_theme_constant_override("separation", 18)
    home_view.add_child(home_actions)

    var primary := _pill_button("⟳  刷新" if OS.get_name() == "iOS" else "＋  导入")
    primary.custom_minimum_size = Vector2(154, 58)
    primary.pressed.connect(_on_refresh_or_import)
    home_actions.add_child(primary)

    var guide := _pill_button("?  导入指南")
    guide.custom_minimum_size = Vector2(186, 58)
    guide.pressed.connect(_show_import_guide)
    home_actions.add_child(guide)

func _build_settings_view() -> void:
    settings_view = ScrollContainer.new()
    settings_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    settings_view.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    settings_view.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
    settings_view.visible = false
    shell_root.add_child(settings_view)

func _rebuild_settings_view() -> void:
    for child in settings_view.get_children():
        settings_view.remove_child(child)
        child.queue_free()

    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", 32)
    margin.add_theme_constant_override("margin_top", 24)
    margin.add_theme_constant_override("margin_right", 32)
    margin.add_theme_constant_override("margin_bottom", 40)
    settings_view.add_child(margin)

    var page := VBoxContainer.new()
    page.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    page.add_theme_constant_override("separation", 26)
    margin.add_child(page)

    var top := HBoxContainer.new()
    top.custom_minimum_size = Vector2(0, 120)
    page.add_child(top)

    var back := _icon_button("‹")
    back.custom_minimum_size = Vector2(78, 78)
    back.pressed.connect(_show_home)
    top.add_child(back)

    var title := Label.new()
    title.text = "设置"
    title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    title.add_theme_font_size_override("font_size", 26)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    top.add_child(title)

    save_button = _pill_button("▣  保存")
    save_button.disabled = not dirty_settings
    save_button.custom_minimum_size = Vector2(150, 72)
    save_button.pressed.connect(_save_shell_settings)
    top.add_child(save_button)

    page.add_child(_section_title("▰  渲染"))
    var render_card := _settings_card()
    page.add_child(render_card)
    render_card.add_child(_settings_block("渲染管线", "未运行游戏时立即生效；运行中切换需重启当前游戏", _backend_segment()))
    render_card.add_child(_settings_toggle_row("性能监控", "显示帧率和图形 API 信息", show_perf_monitor, "perf"))
    render_card.add_child(_settings_toggle_row("帧率限制", "开启后使用下方目标帧率；关闭时交给显示刷新率", frame_limit_enabled, "fps_limit"))
    if frame_limit_enabled:
        render_card.add_child(_settings_fps_row())
    if OS.get_name() == "iOS" or OS.get_name() == "Android":
        render_card.add_child(_settings_toggle_row("锁定横屏", "游戏运行时强制横屏显示（手机推荐开启）", lock_landscape, "landscape"))

    page.add_child(_section_title("▱  开发者"))
    var dev_card := _settings_card()
    page.add_child(dev_card)
    dev_card.add_child(_settings_toggle_row("插件调用追踪", "将所有插件原生调用记录到 plugin_trace.log 用于调试", plugin_trace, "plugin_trace"))
    dev_card.add_child(_settings_toggle_row("Mock 绕过", "为缺失插件返回 mock 对象以抑制错误。关闭可暴露真实错误用于调试。", mock_enabled, "mock"))
    dev_card.add_child(_settings_toggle_row("控制台日志文件", "将引擎控制台日志写入 krkr.console.log 文件", console_log_file, "console_log"))
    dev_card.add_child(_settings_toggle_row("追踪日志", "启用 spdlog trace 级别详细日志，输出最大调试信息", trace_log, "trace_log"))
    dev_card.add_child(_settings_toggle_row("导出 TJS 脚本", "游戏加载时自动从 XP3 中导出反汇编的 TJS 字节码脚本", export_scripts, "export_tjs"))

    page.add_child(_section_title("ⓘ  关于"))
    var about_card := _settings_card()
    page.add_child(about_card)
    about_card.add_child(_settings_value_row("版本", "0.2.0-beta.1"))
    about_card.add_child(_settings_value_row("作者", "reAAAq（由 KYoRi 适配）"))
    about_card.add_child(_settings_value_row("邮箱", "wangguanzhiabcd@126.com"))
    about_card.add_child(_settings_value_row("GitHub (Original)", "github.com/reAAAq/KrKr2-Next"))
    about_card.add_child(_settings_value_row("AetherKiri（当前分支项目）", "github.com/KYoiRyi/AetherKiri"))

func _build_detail_view() -> void:
    detail_view = Control.new()
    detail_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    detail_view.visible = false
    shell_root.add_child(detail_view)

    detail_scroll = ScrollContainer.new()
    detail_scroll.set_anchors_preset(Control.PRESET_FULL_RECT)
    detail_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    detail_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
    detail_view.add_child(detail_scroll)

func _build_modal_layer() -> void:
    modal_layer = Control.new()
    modal_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.visible = false
    add_child(modal_layer)

func _build_loading_panel() -> void:
    loading_panel = PanelContainer.new()
    loading_panel.set_anchors_preset(Control.PRESET_FULL_RECT)
    loading_panel.visible = false
    loading_panel.add_theme_stylebox_override("panel", _panel_style(0, Color(0.08, 0.075, 0.065, 0.96), Color(0, 0, 0, 0), 0))
    add_child(loading_panel)

    var margin := MarginContainer.new()
    margin.add_theme_constant_override("margin_left", 34)
    margin.add_theme_constant_override("margin_top", 30)
    margin.add_theme_constant_override("margin_right", 34)
    margin.add_theme_constant_override("margin_bottom", 30)
    loading_panel.add_child(margin)

    var box := VBoxContainer.new()
    box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    box.size_flags_vertical = Control.SIZE_EXPAND_FILL
    box.add_theme_constant_override("separation", 16)
    margin.add_child(box)

    var title := Label.new()
    title.text = "正在启动游戏..."
    title.add_theme_font_size_override("font_size", 28)
    title.add_theme_color_override("font_color", Color(0.95, 0.93, 0.86, 1))
    box.add_child(title)

    log_view = TextEdit.new()
    log_view.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    log_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
    log_view.editable = false
    log_view.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
    log_view.scroll_fit_content_height = false
    log_view.add_theme_font_size_override("font_size", 18)
    log_view.add_theme_color_override("font_color", Color(0.90, 0.90, 0.82, 1))
    log_view.add_theme_color_override("background_color", Color(0, 0, 0, 0))
    box.add_child(log_view)

func _panel_style(radius: int, fill: Color, border: Color, border_width: int = 1) -> StyleBoxFlat:
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
    style.content_margin_left = 24
    style.content_margin_top = 22
    style.content_margin_right = 24
    style.content_margin_bottom = 22
    return style

func _empty_style() -> StyleBoxEmpty:
    return StyleBoxEmpty.new()

func _rounded_card_material() -> ShaderMaterial:
    if rounded_card_shader == null:
        rounded_card_shader = Shader.new()
        rounded_card_shader.code = """
shader_type canvas_item;
uniform float radius = 18.0;
void fragment() {
    vec4 color = texture(TEXTURE, UV);
    vec2 size = 1.0 / TEXTURE_PIXEL_SIZE;
    vec2 p = UV * size;
    vec2 half_size = size * 0.5;
    vec2 q = abs(p - half_size) - (half_size - vec2(radius));
    float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
    color.a *= 1.0 - smoothstep(0.0, 1.5, d);
    COLOR = color;
}
"""
    var material := ShaderMaterial.new()
    material.shader = rounded_card_shader
    material.set_shader_parameter("radius", 18.0)
    return material

func _apply_button_style(button: Button, normal: StyleBox, hover: StyleBox, pressed: StyleBox, disabled: StyleBox = null) -> void:
    button.add_theme_stylebox_override("normal", normal)
    button.add_theme_stylebox_override("hover", hover)
    button.add_theme_stylebox_override("pressed", pressed)
    button.add_theme_stylebox_override("focus", _empty_style())
    if disabled != null:
        button.add_theme_stylebox_override("disabled", disabled)
    button.add_theme_color_override("font_hover_color", button.get_theme_color("font_color"))
    button.add_theme_color_override("font_pressed_color", button.get_theme_color("font_color"))
    button.add_theme_color_override("font_focus_color", button.get_theme_color("font_color"))
    button.add_theme_color_override("font_disabled_color", Color(1, 1, 1, 0.72))

func _pill_button(text: String) -> Button:
    var button := Button.new()
    button.text = text
    button.alignment = HORIZONTAL_ALIGNMENT_CENTER
    button.clip_text = true
    button.add_theme_font_size_override("font_size", 22)
    button.add_theme_color_override("font_color", Color.WHITE)
    _apply_button_style(
        button,
        _panel_style(18, COLOR_ACCENT, COLOR_ACCENT, 0),
        _panel_style(18, COLOR_ACCENT.lightened(0.05), COLOR_ACCENT, 0),
        _panel_style(18, COLOR_ACCENT.darkened(0.08), COLOR_ACCENT, 0),
        _panel_style(18, Color(0.78, 0.76, 0.70, 1), Color(0.78, 0.76, 0.70, 1), 0)
    )
    return button

func _icon_button(text: String) -> Button:
    var button := Button.new()
    button.text = text
    button.alignment = HORIZONTAL_ALIGNMENT_CENTER
    button.custom_minimum_size = Vector2(64, 64)
    button.add_theme_font_size_override("font_size", 38)
    button.add_theme_color_override("font_color", COLOR_TEXT)
    button.add_theme_color_override("font_hover_color", COLOR_TEXT)
    button.add_theme_color_override("font_pressed_color", COLOR_TEXT)
    button.add_theme_color_override("font_focus_color", COLOR_TEXT)
    _apply_button_style(
        button,
        _panel_style(32, Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0),
        _panel_style(32, Color(0.86, 0.84, 0.78, 0.42), Color(0, 0, 0, 0), 0),
        _panel_style(32, Color(0.80, 0.78, 0.72, 0.55), Color(0, 0, 0, 0), 0)
    )
    return button

func _section_title(text: String) -> Label:
    var label := Label.new()
    label.text = text
    label.custom_minimum_size = Vector2(0, 34)
    label.add_theme_font_size_override("font_size", 22)
    label.add_theme_color_override("font_color", COLOR_ACCENT)
    return label

func _settings_card() -> VBoxContainer:
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 0)
    return box

func _settings_block(title: String, subtitle: String, control: Control) -> VBoxContainer:
    var box := VBoxContainer.new()
    box.custom_minimum_size = Vector2(0, 126)
    box.add_theme_constant_override("separation", 8)
    var title_label := Label.new()
    title_label.text = title
    title_label.add_theme_font_size_override("font_size", 22)
    title_label.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title_label)
    if not subtitle.is_empty():
        var sub := Label.new()
        sub.text = subtitle
        sub.add_theme_font_size_override("font_size", 18)
        sub.add_theme_color_override("font_color", COLOR_MUTED)
        box.add_child(sub)
    box.add_child(control)
    return box

func _settings_toggle_row(title: String, subtitle: String, initial: bool, key: String) -> HBoxContainer:
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 112)
    var labels := VBoxContainer.new()
    labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    var title_label := Label.new()
    title_label.text = title
    title_label.add_theme_font_size_override("font_size", 24)
    title_label.add_theme_color_override("font_color", COLOR_TEXT)
    labels.add_child(title_label)
    var sub := Label.new()
    sub.text = subtitle
    sub.add_theme_font_size_override("font_size", 20)
    sub.add_theme_color_override("font_color", COLOR_MUTED)
    labels.add_child(sub)
    row.add_child(labels)

    var toggle := CheckButton.new()
    toggle.button_pressed = initial
    toggle.custom_minimum_size = Vector2(92, 60)
    toggle.toggled.connect(func(value: bool): _on_setting_toggle(key, value))
    row.add_child(toggle)
    return row

func _settings_value_row(title: String, value: String) -> HBoxContainer:
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 90)
    var label := Label.new()
    label.text = title
    label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    label.add_theme_font_size_override("font_size", 24)
    label.add_theme_color_override("font_color", COLOR_TEXT)
    row.add_child(label)
    var value_label := Label.new()
    value_label.text = value
    value_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    value_label.add_theme_font_size_override("font_size", 20)
    value_label.add_theme_color_override("font_color", COLOR_ACCENT)
    row.add_child(value_label)
    return row

func _settings_fps_row() -> HBoxContainer:
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 90)
    var labels := VBoxContainer.new()
    labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    var title_label := Label.new()
    title_label.text = "目标帧率"
    title_label.add_theme_font_size_override("font_size", 24)
    title_label.add_theme_color_override("font_color", COLOR_TEXT)
    labels.add_child(title_label)
    var sub := Label.new()
    sub.text = "限制 C++ 引擎 tick/render 频率；最低 80 FPS"
    sub.add_theme_font_size_override("font_size", 20)
    sub.add_theme_color_override("font_color", COLOR_MUTED)
    labels.add_child(sub)
    row.add_child(labels)

    var fps_select := OptionButton.new()
    fps_select.custom_minimum_size = Vector2(170, 58)
    var options := [80, 90, 120, 144]
    var selected_index := 0
    for i in range(options.size()):
        fps_select.add_item("%d FPS" % options[i])
        fps_select.set_item_metadata(i, options[i])
        if options[i] == target_fps:
            selected_index = i
    fps_select.select(selected_index)
    fps_select.item_selected.connect(func(index: int):
        target_fps = int(fps_select.get_item_metadata(index))
        _mark_settings_dirty()
        _apply_engine_options()
    )
    row.add_child(fps_select)
    return row

func _segment_button(text: String, selected: bool) -> Button:
    var button := Button.new()
    button.text = text
    button.alignment = HORIZONTAL_ALIGNMENT_CENTER
    button.clip_text = true
    button.toggle_mode = true
    button.button_pressed = selected
    button.custom_minimum_size = Vector2(240, 58)
    button.add_theme_font_size_override("font_size", 20)
    button.add_theme_color_override("font_color", COLOR_TEXT)
    var selected_style := _panel_style(28, COLOR_ACCENT_SOFT, COLOR_ACCENT_SOFT, 0)
    var selected_hover_style := _panel_style(28, COLOR_ACCENT_SOFT.lightened(0.04), COLOR_ACCENT_SOFT, 0)
    var normal_style := _panel_style(28, Color(0.86, 0.84, 0.78, 1), Color(0.86, 0.84, 0.78, 1), 0)
    var normal_hover_style := _panel_style(28, Color(0.89, 0.87, 0.81, 1), Color(0.89, 0.87, 0.81, 1), 0)
    _apply_button_style(
        button,
        selected_style if selected else normal_style,
        selected_hover_style if selected else normal_hover_style,
        selected_style
    )
    return button

func _backend_segment() -> HBoxContainer:
    var row := HBoxContainer.new()
    row.add_theme_constant_override("separation", 8)
    var native := _segment_button("Godot Native", selected_backend != "Debug CPU")
    native.pressed.connect(func(): _select_backend("Godot Native"))
    row.add_child(native)
    var cpu := _segment_button("Debug CPU", selected_backend == "Debug CPU")
    cpu.pressed.connect(func(): _select_backend("Debug CPU"))
    row.add_child(cpu)
    return row

func _theme_segment() -> HBoxContainer:
    var row := HBoxContainer.new()
    row.add_theme_constant_override("separation", 8)
    row.add_child(_segment_button("✦  跟随系统", true))
    row.add_child(_segment_button("◐  深色", false))
    row.add_child(_segment_button("☀  浅色", false))
    return row

func _on_setting_toggle(key: String, value: bool) -> void:
    if key == "perf":
        show_perf_monitor = value
        perf.visible = game_running and show_perf_monitor
    elif key == "fps_limit":
        frame_limit_enabled = value
    elif key == "landscape":
        lock_landscape = value
    elif key == "plugin_trace":
        plugin_trace = value
    elif key == "mock":
        mock_enabled = value
    elif key == "console_log":
        console_log_file = value
    elif key == "trace_log":
        trace_log = value
    elif key == "export_tjs":
        export_scripts = value
    _mark_settings_dirty()
    _apply_engine_options()
    _apply_shell_runtime_settings()
    if key == "fps_limit":
        call_deferred("_rebuild_settings_view")

func _select_backend(value: String) -> void:
    var index := BACKENDS.find(value)
    if index < 0:
        return
    backend.select(index)
    _on_backend_selected(index)
    _mark_settings_dirty()
    call_deferred("_rebuild_settings_view")

func _empty_help_text() -> String:
    if OS.get_name() == "iOS":
        return "使用「文件」App 将游戏文件夹复制到：\n我的 iPhone / iPad > AetherKiri > Games\n然后点击「刷新」"
    return "点击「导入」选择游戏目录或 XP3 文件"

func _show_home() -> void:
    if dirty_settings:
        _save_shell_settings()
    home_view.visible = true
    settings_view.visible = false
    detail_view.visible = false
    modal_layer.visible = false
    _refresh_games()

func _show_settings() -> void:
    _rebuild_settings_view()
    home_view.visible = false
    settings_view.visible = true
    detail_view.visible = false
    modal_layer.visible = false

func _show_detail(game: Dictionary) -> void:
    selected_game = game
    home_view.visible = false
    settings_view.visible = false
    detail_view.visible = true
    modal_layer.visible = false
    for child in detail_scroll.get_children():
        child.queue_free()

    var content := Control.new()
    content.custom_minimum_size = Vector2(1280, 920)
    content.mouse_filter = Control.MOUSE_FILTER_PASS
    detail_scroll.add_child(content)

    var back := _icon_button("‹")
    back.position = Vector2(30, 42)
    back.pressed.connect(_show_home)
    content.add_child(back)

    var cover := PanelContainer.new()
    cover.position = Vector2(510, 100)
    cover.size = Vector2(260, 190)
    cover.add_theme_stylebox_override("panel", _panel_style(18, Color(0.90, 0.89, 0.84, 1), Color(0, 0, 0, 0.04), 1))
    content.add_child(cover)
    var cover_texture := _load_cover_texture(game)
    if cover_texture != null:
        var image := TextureRect.new()
        image.texture = cover_texture
        image.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
        image.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
        cover.add_child(image)
    else:
        var icon := Label.new()
        icon.text = "▣"
        icon.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
        icon.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
        icon.add_theme_font_size_override("font_size", 44)
        icon.add_theme_color_override("font_color", COLOR_ACCENT_SOFT)
        cover.add_child(icon)

    var title := Label.new()
    title.text = _game_display_title(game)
    title.position = Vector2(320, 310)
    title.size = Vector2(640, 54)
    title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    title.add_theme_font_size_override("font_size", 34)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    content.add_child(title)

    var info := VBoxContainer.new()
    info.position = Vector2(38, 370)
    info.size = Vector2(max(600, int(size.x) - 76), 170)
    info.add_theme_constant_override("separation", 12)
    content.add_child(info)
    info.add_child(_detail_line("□", String(game.get("path", ""))))
    info.add_child(_detail_line("◷", "上次游玩：%s" % (_game_subtitle(game).split(" · ")[0])))
    info.add_child(_detail_line("◴", "已玩 %s" % _format_play_duration(int(game.get("playDurationSeconds", 0)))))
    info.add_child(_detail_line("▤", String(game.get("type", "Directory"))))

    var start := _pill_button("▶  启动游戏")
    start.position = Vector2(38, 500)
    start.size = Vector2(1204, 72)
    start.pressed.connect(_start_selected_game)
    content.add_child(start)

    var tools := VBoxContainer.new()
    tools.position = Vector2(32, 600)
    tools.size = Vector2(1216, 260)
    tools.add_theme_constant_override("separation", 1)
    content.add_child(tools)
    tools.add_child(_detail_action("▧", "设置封面", func(): _set_cover_for_selected()))
    tools.add_child(_detail_action("✎", "重命名", func(): _rename_selected_game()))
    tools.add_child(_detail_action("⌫", "移除游戏", func(): _confirm_remove_selected()))

func _detail_line(icon: String, text: String) -> HBoxContainer:
    var row := HBoxContainer.new()
    row.add_theme_constant_override("separation", 16)
    var i := Label.new()
    i.text = icon
    i.add_theme_font_size_override("font_size", 22)
    i.add_theme_color_override("font_color", COLOR_MUTED)
    row.add_child(i)
    var label := Label.new()
    label.text = text
    label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    label.add_theme_font_size_override("font_size", 21)
    label.add_theme_color_override("font_color", COLOR_MUTED)
    row.add_child(label)
    return row

func _detail_action(icon: String, text: String, callback: Callable = Callable()) -> Button:
    var button := Button.new()
    button.text = "%s   %s    ›" % [icon, text]
    button.alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.clip_text = true
    button.custom_minimum_size = Vector2(0, 76)
    button.add_theme_font_size_override("font_size", 24)
    button.add_theme_color_override("font_color", COLOR_TEXT)
    _apply_button_style(
        button,
        _panel_style(14, COLOR_CARD, Color(0, 0, 0, 0.06), 1),
        _panel_style(14, Color(1.0, 0.995, 0.975, 1), Color(0, 0, 0, 0.08), 1),
        _panel_style(14, Color(0.95, 0.94, 0.90, 1), Color(0, 0, 0, 0.08), 1)
    )
    if callback.is_valid():
        button.pressed.connect(callback)
    return button

func _show_import_guide() -> void:
    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.55)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)

    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-320, -250)
    dialog.size = Vector2(640, 500)
    dialog.add_theme_stylebox_override("panel", _panel_style(22, COLOR_CARD, Color(0, 0, 0, 0.04), 1))
    modal_layer.add_child(dialog)

    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 22)
    dialog.add_child(box)
    var title := Label.new()
    title.text = "导入游戏"
    title.add_theme_font_size_override("font_size", 30)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title)
    var body := Label.new()
    body.text = "请使用「文件」App 将游戏文件夹复制到本应用的目录：\n\n1. 打开 iPhone / iPad 上的「文件」App\n2. 前往：我的 iPhone / iPad > AetherKiri > Games\n3. 将游戏文件夹复制到 Games 目录\n4. 返回本应用，点击「刷新」检测新游戏\n\n游戏目录：Games/"
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.add_theme_font_size_override("font_size", 22)
    body.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(body)
    var ok := _pill_button("知道了")
    ok.custom_minimum_size = Vector2(140, 62)
    ok.pressed.connect(func(): modal_layer.visible = false)
    box.add_child(ok)

func _show_message(message: String) -> void:
    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.38)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)
    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-260, -120)
    dialog.size = Vector2(520, 240)
    dialog.add_theme_stylebox_override("panel", _panel_style(20, COLOR_CARD, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 18)
    dialog.add_child(box)
    var label := Label.new()
    label.text = message
    label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    label.add_theme_font_size_override("font_size", 22)
    label.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(label)
    var ok := _pill_button("知道了")
    ok.pressed.connect(func(): modal_layer.visible = false)
    box.add_child(ok)

func _offer_scrape_after_add(game: Dictionary) -> void:
    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.38)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)
    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-280, -150)
    dialog.size = Vector2(560, 300)
    dialog.add_theme_stylebox_override("panel", _panel_style(20, COLOR_CARD, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 18)
    dialog.add_child(box)
    var title := Label.new()
    title.text = "刮削元数据"
    title.add_theme_font_size_override("font_size", 28)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title)
    var body := Label.new()
    body.text = "已添加「%s」。是否现在进入详情页设置封面、名称和元数据？" % _game_display_title(game)
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.add_theme_font_size_override("font_size", 21)
    body.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(body)
    var buttons := HBoxContainer.new()
    buttons.add_theme_constant_override("separation", 12)
    box.add_child(buttons)
    var no := Button.new()
    no.text = "稍后"
    no.flat = true
    no.pressed.connect(func(): modal_layer.visible = false)
    buttons.add_child(no)
    var yes := _pill_button("打开详情")
    yes.pressed.connect(func():
        modal_layer.visible = false
        _show_detail(game)
    )
    buttons.add_child(yes)

func _set_cover_for_selected() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    var dialog := FileDialog.new()
    dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
    dialog.access = FileDialog.ACCESS_FILESYSTEM
    dialog.title = "选择封面图片"
    dialog.add_filter("*.png, *.jpg, *.jpeg, *.webp;Image")
    dialog.file_selected.connect(func(cover_path: String):
        _update_game(path, {"coverPath": cover_path})
        _show_detail(selected_game)
    )
    add_child(dialog)
    dialog.popup_centered(Vector2i(900, 640))

func _rename_selected_game() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.38)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)
    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-280, -150)
    dialog.size = Vector2(560, 300)
    dialog.add_theme_stylebox_override("panel", _panel_style(20, COLOR_CARD, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 18)
    dialog.add_child(box)
    var title := Label.new()
    title.text = "重命名"
    title.add_theme_font_size_override("font_size", 28)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title)
    var input := LineEdit.new()
    input.text = _game_display_title(selected_game)
    input.custom_minimum_size = Vector2(460, 52)
    box.add_child(input)
    var save := _pill_button("保存")
    save.pressed.connect(func():
        var new_title := input.text.strip_edges()
        if not new_title.is_empty():
            modal_layer.visible = false
            _update_game(path, {"title": new_title})
            _show_detail(selected_game)
    )
    box.add_child(save)

func _confirm_remove_selected() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.38)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)
    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-280, -140)
    dialog.size = Vector2(560, 280)
    dialog.add_theme_stylebox_override("panel", _panel_style(20, COLOR_CARD, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 18)
    dialog.add_child(box)
    var label := Label.new()
    label.text = "从列表移除「%s」？不会删除磁盘上的游戏文件。" % _game_display_title(selected_game)
    label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    label.add_theme_font_size_override("font_size", 22)
    label.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(label)
    var remove := _pill_button("移除")
    remove.pressed.connect(func():
        modal_layer.visible = false
        _remove_game(path)
    )
    box.add_child(remove)

func _on_refresh_or_import() -> void:
    if OS.get_name() == "iOS":
        _refresh_games()
        return
    _show_import_picker()

func _show_import_picker() -> void:
    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.45)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)
    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-260, -160)
    dialog.size = Vector2(520, 320)
    dialog.add_theme_stylebox_override("panel", _panel_style(20, COLOR_CARD, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 14)
    dialog.add_child(box)
    var title := Label.new()
    title.text = "导入游戏"
    title.add_theme_font_size_override("font_size", 28)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title)
    var dir_button := _pill_button("选择游戏目录")
    dir_button.pressed.connect(func():
        modal_layer.visible = false
        _open_import_dialog(false)
    )
    box.add_child(dir_button)
    var xp3_button := _pill_button("选择 XP3 文件")
    xp3_button.pressed.connect(func():
        modal_layer.visible = false
        _open_import_dialog(true)
    )
    box.add_child(xp3_button)
    var cancel := Button.new()
    cancel.text = "取消"
    cancel.flat = true
    cancel.pressed.connect(func(): modal_layer.visible = false)
    box.add_child(cancel)

func _open_import_dialog(xp3: bool) -> void:
    var dialog := FileDialog.new()
    dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE if xp3 else FileDialog.FILE_MODE_OPEN_DIR
    dialog.access = FileDialog.ACCESS_FILESYSTEM
    dialog.title = "选择 XP3 文件" if xp3 else "选择游戏目录"
    if xp3:
        dialog.add_filter("*.xp3, *.XP3;KiriKiri XP3 archive")
    dialog.dir_selected.connect(func(path: String):
        _add_game_path(path)
    )
    dialog.file_selected.connect(func(path: String):
        _add_game_path(path)
    )
    add_child(dialog)
    dialog.popup_centered(Vector2i(900, 640))

func _refresh_games() -> void:
    known_games = _load_game_list()
    if OS.get_name() == "iOS":
        known_games = _scan_ios_games_dir(known_games)
        _save_game_list(known_games)
    known_games = _sorted_games(known_games)
    for child in game_list.get_children():
        child.queue_free()
    empty_state.visible = known_games.is_empty()
    game_scroll.visible = not known_games.is_empty()
    for game in known_games:
        game_list.add_child(_game_card(game))

func _load_game_list() -> Array[Dictionary]:
    var file := FileAccess.open(GAME_LIST_FILE, FileAccess.READ)
    if file == null:
        var fallback: String = String(ProjectSettings.get_setting(GAME_PATH_KEY, ""))
        var initial_games: Array[Dictionary] = []
        if not fallback.is_empty() and _path_exists(fallback):
            initial_games.append(_game_info_from_path(fallback))
        return initial_games
    var parsed = JSON.parse_string(file.get_as_text())
    if not parsed is Array:
        return []
    var games: Array[Dictionary] = []
    for item in parsed:
        if item is Dictionary and item.has("path") and _path_exists(String(item.get("path", ""))):
            games.append(item)
    return games

func _save_game_list(games: Array[Dictionary]) -> void:
    var file := FileAccess.open(GAME_LIST_FILE, FileAccess.WRITE)
    if file != null:
        file.store_string(JSON.stringify(games))

func _scan_ios_games_dir(existing: Array[Dictionary]) -> Array[Dictionary]:
    var root := ProjectSettings.globalize_path("user://Games")
    DirAccess.make_dir_recursive_absolute(root)
    var by_name := {}
    var next: Array[Dictionary] = []
    for game in existing:
        var name := _game_display_title(game)
        by_name[name] = game
        if not String(game.get("path", "")).begins_with(root) and _path_exists(String(game.get("path", ""))):
            next.append(game)
    var dir := DirAccess.open(root)
    if dir == null:
        return next
    dir.list_dir_begin()
    var entry := dir.get_next()
    while not entry.is_empty():
        if not entry.begins_with("."):
            var path := root.path_join(entry)
            if dir.current_is_dir() or entry.to_lower().ends_with(".xp3"):
                var game: Dictionary = by_name.get(entry, _game_info_from_path(path))
                game["path"] = path
                next.append(game)
        entry = dir.get_next()
    return _dedupe_games(next)

func _add_game_path(path: String) -> bool:
    if not _path_exists(path):
        _show_message("游戏路径不存在")
        return false
    var games := _load_game_list()
    for game in games:
        if String(game.get("path", "")) == path:
            _show_message("游戏已存在：%s" % _game_display_title(game))
            return false
    var game := _game_info_from_path(path)
    games.append(game)
    _save_game_list(_dedupe_games(games))
    ProjectSettings.set_setting(GAME_PATH_KEY, path)
    ProjectSettings.save()
    game_path.text = path
    _refresh_games()
    _offer_scrape_after_add(game)
    return true

func _dedupe_games(games: Array[Dictionary]) -> Array[Dictionary]:
    var seen := {}
    var result: Array[Dictionary] = []
    for game in games:
        var path := String(game.get("path", ""))
        if path.is_empty() or seen.has(path):
            continue
        seen[path] = true
        result.append(game)
    return result

func _sorted_games(games: Array[Dictionary]) -> Array[Dictionary]:
    games.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
        var at := int(a.get("lastPlayed", 0))
        var bt := int(b.get("lastPlayed", 0))
        if at != bt:
            return at > bt
        return _game_display_title(a) < _game_display_title(b)
    )
    return games

func _path_exists(path: String) -> bool:
    return DirAccess.dir_exists_absolute(path) or FileAccess.file_exists(path)

func _game_info_from_path(path: String) -> Dictionary:
    var name := path.get_file()
    if name.to_lower().ends_with(".xp3"):
        name = name.substr(0, name.length() - 4)
    return {
        "name": name,
        "path": path,
        "type": "Archive" if path.to_lower().ends_with(".xp3") else "Directory",
        "lastPlayed": 0,
        "playDurationSeconds": 0,
        "coverPath": "",
        "developer": "",
        "title": "",
    }

func _game_display_title(game: Dictionary) -> String:
    var title := String(game.get("title", ""))
    if not title.is_empty():
        return title
    return String(game.get("name", String(game.get("path", "")).get_file()))

func _format_play_duration(seconds: int) -> String:
    if seconds < 60:
        return "0m"
    var minutes := seconds / 60
    if minutes < 60:
        return "%dm" % minutes
    var hours := minutes / 60
    var mins := minutes % 60
    if mins == 0:
        return "%dh" % hours
    return "%dh %dm" % [hours, mins]

func _game_subtitle(game: Dictionary) -> String:
    var parts: PackedStringArray = []
    var last_played := int(game.get("lastPlayed", 0))
    if last_played > 0:
        var elapsed: int = max(0, int(Time.get_unix_time_from_system()) - last_played)
        if elapsed < 86400:
            parts.append("今天")
        else:
            parts.append("%d 天前" % max(1, elapsed / 86400))
    var duration := int(game.get("playDurationSeconds", 0))
    if duration >= 60:
        parts.append("已玩 %s" % _format_play_duration(duration))
    return " · ".join(parts) if not parts.is_empty() else "尚未游玩"

func _mark_game_played(path: String) -> Dictionary:
    var games := _load_game_list()
    var updated := {}
    for i in range(games.size()):
        if String(games[i].get("path", "")) == path:
            games[i]["lastPlayed"] = int(Time.get_unix_time_from_system())
            updated = games[i]
            break
    _save_game_list(games)
    return updated

func _add_play_duration(path: String, seconds: int) -> void:
    if seconds <= 0:
        return
    var games := _load_game_list()
    for i in range(games.size()):
        if String(games[i].get("path", "")) == path:
            games[i]["playDurationSeconds"] = int(games[i].get("playDurationSeconds", 0)) + min(seconds, 86400)
            break
    _save_game_list(games)

func _update_game(path: String, values: Dictionary) -> void:
    var games := _load_game_list()
    for i in range(games.size()):
        if String(games[i].get("path", "")) == path:
            for key in values.keys():
                games[i][key] = values[key]
            selected_game = games[i]
            break
    _save_game_list(games)
    _refresh_games()

func _remove_game(path: String) -> void:
    var games := _load_game_list()
    var next: Array[Dictionary] = []
    for game in games:
        if String(game.get("path", "")) != path:
            next.append(game)
    _save_game_list(next)
    selected_game = {}
    _show_home()

func _game_card(game: Dictionary) -> Button:
    var button := Button.new()
    button.custom_minimum_size = HOME_CARD_SIZE
    button.clip_text = true
    button.clip_contents = true
    button.text = ""
    button.add_theme_stylebox_override("normal", _panel_style(18, Color(0.88, 0.87, 0.82, 1), Color(0, 0, 0, 0.10), 1))
    button.add_theme_stylebox_override("hover", _panel_style(18, Color(0.91, 0.90, 0.85, 1), Color(0, 0, 0, 0.14), 1))
    button.add_theme_stylebox_override("pressed", _panel_style(18, Color(0.82, 0.81, 0.76, 1), Color(0, 0, 0, 0.16), 1))
    button.add_theme_stylebox_override("focus", _empty_style())
    button.pressed.connect(func(): _show_detail(game))

    var frame := Control.new()
    frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
    frame.clip_contents = true
    frame.set_anchors_preset(Control.PRESET_FULL_RECT)
    button.add_child(frame)

    var cover_texture := _load_cover_texture(game)
    if cover_texture != null:
        var cover := TextureRect.new()
        cover.texture = cover_texture
        cover.mouse_filter = Control.MOUSE_FILTER_IGNORE
        cover.set_anchors_preset(Control.PRESET_FULL_RECT)
        cover.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
        cover.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
        cover.material = _rounded_card_material()
        frame.add_child(cover)
    else:
        var placeholder := PanelContainer.new()
        placeholder.mouse_filter = Control.MOUSE_FILTER_IGNORE
        placeholder.set_anchors_preset(Control.PRESET_FULL_RECT)
        placeholder.add_theme_stylebox_override("panel", _panel_style(18, Color(0.91, 0.90, 0.85, 1), Color(0, 0, 0, 0.02), 1))
        frame.add_child(placeholder)

        var icon := Label.new()
        icon.text = "▣"
        icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
        icon.set_anchors_preset(Control.PRESET_FULL_RECT)
        icon.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
        icon.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
        icon.add_theme_font_size_override("font_size", 38)
        icon.add_theme_color_override("font_color", COLOR_ACCENT_SOFT)
        frame.add_child(icon)

    var shade := PanelContainer.new()
    shade.mouse_filter = Control.MOUSE_FILTER_IGNORE
    shade.anchor_left = 0.0
    shade.anchor_top = 1.0
    shade.anchor_right = 1.0
    shade.anchor_bottom = 1.0
    shade.offset_left = 0.0
    shade.offset_top = -104.0
    shade.offset_right = 0.0
    shade.offset_bottom = 0.0
    shade.add_theme_stylebox_override("panel", _panel_style(18, Color(0.0, 0.0, 0.0, 0.38), Color(0, 0, 0, 0), 0))
    frame.add_child(shade)

    var text_margin := MarginContainer.new()
    text_margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
    text_margin.anchor_left = 0.0
    text_margin.anchor_top = 1.0
    text_margin.anchor_right = 1.0
    text_margin.anchor_bottom = 1.0
    text_margin.offset_left = 0.0
    text_margin.offset_top = -104.0
    text_margin.offset_right = 0.0
    text_margin.offset_bottom = 0.0
    text_margin.add_theme_constant_override("margin_left", 18)
    text_margin.add_theme_constant_override("margin_top", 18)
    text_margin.add_theme_constant_override("margin_right", 18)
    text_margin.add_theme_constant_override("margin_bottom", 16)
    frame.add_child(text_margin)

    var labels := VBoxContainer.new()
    labels.mouse_filter = Control.MOUSE_FILTER_IGNORE
    labels.add_theme_constant_override("separation", 4)
    text_margin.add_child(labels)

    var title := Label.new()
    title.text = _game_display_title(game)
    title.mouse_filter = Control.MOUSE_FILTER_IGNORE
    title.clip_text = true
    title.add_theme_font_size_override("font_size", 22)
    title.add_theme_color_override("font_color", Color.WHITE)
    labels.add_child(title)

    var sub := Label.new()
    sub.text = _game_subtitle(game)
    sub.mouse_filter = Control.MOUSE_FILTER_IGNORE
    sub.clip_text = true
    sub.add_theme_font_size_override("font_size", 17)
    sub.add_theme_color_override("font_color", Color(1, 1, 1, 0.76))
    labels.add_child(sub)
    return button

func _load_cover_texture(game: Dictionary) -> Texture2D:
    var cover_path := String(game.get("coverPath", ""))
    if cover_path.is_empty() or not FileAccess.file_exists(cover_path):
        return null
    var image := Image.new()
    if image.load(cover_path) != OK:
        return null
    return ImageTexture.create_from_image(image)

func _start_selected_game() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    selected_game = _mark_game_played(path)
    active_game_path = path
    active_game_started_msec = Time.get_ticks_msec()
    game_path.text = path
    shell_root.visible = false
    viewport.visible = true
    viewport.move_to_front()
    game_view.visible = true
    loading_panel.visible = true
    loading_panel.move_to_front()
    perf.visible = show_perf_monitor
    restart_notice.visible = true
    _on_open_game()

func _finalize_active_game_session() -> void:
    if active_game_path.is_empty() or active_game_started_msec <= 0:
        return
    var elapsed := int((Time.get_ticks_msec() - active_game_started_msec) / 1000)
    _add_play_duration(active_game_path, elapsed)
    active_game_path = ""
    active_game_started_msec = 0

func _ready() -> void:
    DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_TRANSPARENT, false)
    get_viewport().transparent_bg = false
    RenderingServer.set_default_clear_color(COLOR_BG)
    var perf_interval_env := OS.get_environment("AETHERKIRI_PERF_LOG_INTERVAL")
    if not perf_interval_env.is_empty():
        perf_log_interval = maxf(0.05, perf_interval_env.to_float())
    frame_spike_ms = maxf(0.0, OS.get_environment("AETHERKIRI_FRAME_SPIKE_MS").to_float())
    verbose_render_log = OS.get_environment("AETHERKIRI_VERBOSE_RENDER_LOG") == "1"

    var live_fps_log_path := OS.get_environment("AETHERKIRI_LIVE_FPS_LOG")
    if live_fps_log_path.is_empty():
        live_fps_log_path = _default_output_path("aetherkiri-live-fps.log")
    perf_log_file = FileAccess.open(live_fps_log_path, FileAccess.WRITE)
    if perf_log_file != null:
        perf_log_file.store_line("live fps log started")
        perf_log_file.flush()

    selected_backend = OS.get_environment("AETHERKIRI_BACKEND")
    if selected_backend.is_empty():
        selected_backend = ProjectSettings.get_setting(SETTINGS_KEY, "Godot Native")
    _load_shell_settings()
    if not selected_backend in BACKENDS:
        selected_backend = "Godot Native"

    _build_ui()

    player = AetherKiriPlayer.new()
    add_child(player)

    for item in BACKENDS:
        backend.add_item(item)

    var index := BACKENDS.find(selected_backend)
    backend.select(max(index, 0))

    var configured_game_path := OS.get_environment("AETHERKIRI_GAME_PATH")
    if configured_game_path.is_empty():
        configured_game_path = ProjectSettings.get_setting(GAME_PATH_KEY, "")
    game_path.text = configured_game_path

    backend.item_selected.connect(_on_backend_selected)
    viewport.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    viewport.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED

    _append_log("AetherKiri shell ready. Initializing engine...")
    call_deferred("_finish_ready_after_first_frame")

func _finish_ready_after_first_frame() -> void:
    await get_tree().process_frame

    var user_dir := OS.get_user_data_dir()
    var cache_dir := user_dir.path_join("cache")
    DirAccess.make_dir_recursive_absolute(cache_dir)
    if not player.initialize_engine(user_dir, cache_dir):
        render_errors += 1
        _append_log("Engine init failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ])
    else:
        _append_log("AetherKiri engine initialized.")

    _apply_backend(false)
    _apply_engine_options()
    _apply_shell_runtime_settings()
    _append_log("Debug CPU is a fallback backend and is not part of performance acceptance.")
    _write_probe_marker("ready")
    _refresh_games()

    capture_after_open_path = OS.get_environment("AETHERKIRI_CAPTURE_AFTER_OPEN")
    capture_after_open_delay_sec = maxf(
        0.0,
        OS.get_environment("AETHERKIRI_CAPTURE_DELAY_SEC").to_float()
    )
    auto_probe_clicks = _parse_click_points(OS.get_environment("AETHERKIRI_AUTO_PROBE_CLICKS"))
    if OS.get_environment("AETHERKIRI_AUTO_OPEN") == "1":
        call_deferred("_on_open_game")
    if not OS.get_environment("AETHERKIRI_CAPTURE_UI").is_empty():
        call_deferred("_capture_ui_after_ready")

func _capture_ui_after_ready() -> void:
    var action := OS.get_environment("AETHERKIRI_CAPTURE_UI_ACTION")
    if action == "settings":
        _show_settings()
    elif action == "guide":
        _show_import_guide()
    elif action == "detail" and not known_games.is_empty():
        _show_detail(known_games[0])
    var mouse := OS.get_environment("AETHERKIRI_CAPTURE_UI_MOUSE")
    if not mouse.is_empty():
        var parts := mouse.split(",", false)
        if parts.size() == 2:
            Input.warp_mouse(Vector2(parts[0].to_float(), parts[1].to_float()))
    await get_tree().process_frame
    await get_tree().process_frame
    await get_tree().process_frame
    var path := OS.get_environment("AETHERKIRI_CAPTURE_UI")
    var image := get_viewport().get_texture().get_image()
    image.save_png(path)
    print("ui_capture output=%s stats=%s" % [path, JSON.stringify(_image_stats(image))])
    if OS.get_environment("AETHERKIRI_QUIT_AFTER_CAPTURE") == "1":
        get_tree().quit(0)

func _process(delta: float) -> void:
    _fit_full_rects()
    var startup_state := STARTUP_IDLE
    if game_running:
        log_drain_accum += delta
        if log_drain_accum >= LOG_DRAIN_INTERVAL:
            log_drain_accum = 0.0
            _drain_logs()

        startup_state = player.get_startup_state()
        if startup_state == STARTUP_SUCCEEDED:
            restart_notice.text = ""
            loading_panel.visible = false
            var tick_start := Time.get_ticks_usec()
            var tick_result := player.tick(delta)
            var tick_ms := float(Time.get_ticks_usec() - tick_start) / 1000.0
            if tick_result != ENGINE_RESULT_OK:
                render_errors += 1
                var tick_error_line := "Tick failed: %s %s" % [
                    player.get_last_result(),
                    player.get_last_error(),
                ]
                _append_log(tick_error_line)
                print(tick_error_line)
                if perf_log_file != null:
                    perf_log_file.store_line(tick_error_line)
                    perf_log_file.flush()
                game_running = false
            else:
                var update_start := Time.get_ticks_usec()
                _update_frame()
                var update_ms := float(Time.get_ticks_usec() - update_start) / 1000.0
                _log_live_perf(delta, tick_ms, update_ms)
                _log_frame_spike(delta, tick_ms, update_ms)
        elif startup_state == STARTUP_FAILED:
            restart_notice.text = "Game startup failed."
            loading_panel.visible = false
            shell_root.visible = true
            viewport.visible = false
            game_view.visible = false
            game_running = false
            render_errors += 1
            _append_log("Startup failed: %s" % player.get_last_error())

    perf_accum += delta
    state_log_accum += delta
    if game_running and state_log_accum >= 1.0:
        state_log_accum = 0.0
        var state_line := "main_state startup=%d last_result=%s last_error=\"%s\" texture=%s size=%dx%d" % [
            startup_state,
            player.get_last_result(),
            player.get_last_error(),
            player.get_frame_texture_backend(),
            last_texture_size.x,
            last_texture_size.y,
        ]
        print(state_line)
        if perf_log_file != null:
            perf_log_file.store_line(state_line)
            perf_log_file.flush()
    if perf_accum >= PERF_UPDATE_INTERVAL:
        perf_accum = 0.0
        var frame_ms := delta * 1000.0
        var renderer := player.get_renderer_info() if game_running else selected_backend
        var renderer_summary := _renderer_summary(renderer)
        if verbose_render_log and game_running and not renderer.is_empty() and renderer_summary != last_renderer_info_logged:
            last_renderer_info_logged = renderer_summary
            _append_log("Renderer info: %s" % renderer)
        var fallback := _renderer_fallback(renderer)
        var texture_backend := player.get_frame_texture_backend() if game_running else "none"
        perf.text = "Backend: %s | FPS: %d | Frame: %.2f ms | Texture: %s | Size: %dx%d | Fallback: %s | Errors: %d" % [
            renderer_summary,
            Engine.get_frames_per_second(),
            frame_ms,
            texture_backend,
            last_texture_size.x,
            last_texture_size.y,
            fallback,
            render_errors,
        ]
func _log_live_perf(delta: float, tick_ms: float, update_ms: float) -> void:
    perf_log_accum += delta
    if perf_log_accum < perf_log_interval:
        return
    perf_log_accum = 0.0
    var line := "live_perf fps=%d frame_ms=%.2f tick_ms=%.2f update_ms=%.2f texture=%s size=%dx%d renderer=\"%s\" errors=%d" % [
        Engine.get_frames_per_second(),
        delta * 1000.0,
        tick_ms,
        update_ms,
        player.get_frame_texture_backend(),
        last_texture_size.x,
        last_texture_size.y,
        player.get_renderer_info(),
        render_errors,
    ]
    print(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _log_frame_spike(delta: float, tick_ms: float, update_ms: float) -> void:
    if frame_spike_ms <= 0.0:
        return
    var frame_ms := delta * 1000.0
    var work_ms := tick_ms + update_ms
    if frame_ms < frame_spike_ms and work_ms < frame_spike_ms:
        return
    var line := "frame_spike fps=%d frame_ms=%.2f tick_ms=%.2f update_ms=%.2f texture=%s size=%dx%d renderer=\"%s\" errors=%d" % [
        Engine.get_frames_per_second(),
        frame_ms,
        tick_ms,
        update_ms,
        player.get_frame_texture_backend(),
        last_texture_size.x,
        last_texture_size.y,
        player.get_renderer_info(),
        render_errors,
    ]
    print(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _notification(what: int) -> void:
    if what == NOTIFICATION_RESIZED:
        _fit_full_rects()
        return
    if player == null:
        return
    if what == NOTIFICATION_WM_CLOSE_REQUEST:
        _finalize_active_game_session()
        viewport.texture = null
        player.release_frame_texture()
        player.destroy_engine()

func _on_backend_selected(index: int) -> void:
    selected_backend = BACKENDS[index]
    ProjectSettings.set_setting(SETTINGS_KEY, selected_backend)
    ProjectSettings.save()
    if game_running:
        restart_notice.text = "Restart current game session to apply renderer."
        _append_log("Renderer change queued: %s" % selected_backend)
        return
    _apply_backend(true)

func _apply_backend(log_selection: bool) -> void:
    var result := player.set_render_backend(selected_backend)
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        _append_log("Renderer selection failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ])
        return
    restart_notice.text = ""
    if log_selection:
        _append_log("Renderer selected: %s" % selected_backend)
    if selected_backend == "GPU Bridge":
        _append_log("GPU Bridge imports the native GPU render target for display.")
    if selected_backend == "Debug CPU":
        _append_log("Debug CPU fallback enabled by user selection.")

func _renderer_fallback(renderer: String) -> String:
    if renderer.is_empty():
        return "pending" if game_running else "none"
    var marker := "fallback="
    var start := renderer.find(marker)
    if start < 0:
        return "unknown" if game_running else "none"
    start += marker.length()
    var end := renderer.find(" ", start)
    if end < 0:
        end = renderer.length()
    return renderer.substr(start, end - start)

func _renderer_summary(renderer: String) -> String:
    if renderer.is_empty():
        return selected_backend
    if renderer.contains("backend=godot_native"):
        return "Godot Native GPU"
    if renderer.contains("backend=gpu_bridge"):
        return "GPU Bridge"
    if renderer.contains("backend=debug_cpu"):
        return "Debug CPU"
    return selected_backend


func _on_open_game() -> void:
    var path := game_path.text.strip_edges()
    _write_probe_marker("open_game path=%s" % path)
    if path.is_empty():
        render_errors += 1
        _append_log("Game path is empty.")
        return

    ProjectSettings.set_setting(GAME_PATH_KEY, path)
    ProjectSettings.save()
    _apply_backend(false)
    player.set_surface_size(RENDER_SURFACE_SIZE.x, RENDER_SURFACE_SIZE.y)

    var async_open := OS.get_environment("AETHERKIRI_SYNC_OPEN") != "1"
    var result := player.open_game(path, async_open)
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        _write_probe_marker("open_game_failed result=%s error=%s" % [
            player.get_last_result(),
            player.get_last_error(),
        ])
        _append_log("Game launch failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ])
        return

    game_running = true
    log_lines.clear()
    if log_view != null:
        log_view.text = ""
        log_view.scroll_vertical = 0
    last_texture_size = Vector2i.ZERO
    capture_after_open_done = false
    capture_after_open_ready_usec = 0
    auto_probe_running = false
    auto_probe_done = false
    last_renderer_info_logged = ""
    restart_notice.text = "Starting..."
    _append_log("Game launch requested with backend: %s" % selected_backend)
    _append_log("Path: %s" % path)

func _drain_logs() -> void:
    var logs := player.drain_startup_logs()
    if logs.is_empty():
        return
    for line in logs.split("\n", false):
        _append_log(line)

func _update_frame() -> void:
    var texture: Texture2D = player.update_frame_texture()
    if texture != null:
        viewport.texture = texture
        viewport.queue_redraw()
        last_texture_size = Vector2i(texture.get_width(), texture.get_height())
        if not auto_probe_clicks.is_empty() and not auto_probe_running and not auto_probe_done:
            auto_probe_running = true
            call_deferred("_run_auto_probe")
        if not capture_after_open_path.is_empty() and not capture_after_open_done:
            if capture_after_open_ready_usec == 0:
                capture_after_open_ready_usec = Time.get_ticks_usec() + int(capture_after_open_delay_sec * 1000000.0)
            if Time.get_ticks_usec() < capture_after_open_ready_usec:
                return
            capture_after_open_done = true
            var frame_stats := {
                "source": "viewport_texture",
                "texture_width": last_texture_size.x,
                "texture_height": last_texture_size.y,
                "texture_backend": player.get_frame_texture_backend(),
            }
            call_deferred("_capture_main_view", frame_stats)

func _capture_main_view(frame_stats: Dictionary) -> void:
    await get_tree().process_frame
    await get_tree().process_frame
    var image := get_viewport().get_texture().get_image()
    var screenshot_stats := _image_stats(image)
    var output_path := capture_after_open_path
    if output_path.is_empty():
        output_path = _default_output_path("main_render_probe.png")
    image.save_png(output_path)
    _write_probe_marker("capture output=%s stats=%s" % [
        output_path,
        JSON.stringify(screenshot_stats),
    ])
    print("main probe renderer=\"%s\" texture_backend=%s texture_width=%d frame_stats=%s screenshot=%s screenshot_stats=%s" % [
        player.get_renderer_info(),
        player.get_frame_texture_backend(),
        last_texture_size.x,
        JSON.stringify(frame_stats),
        output_path,
        JSON.stringify(screenshot_stats),
    ])
    if OS.get_environment("AETHERKIRI_QUIT_AFTER_CAPTURE") == "1":
        var visible := int(screenshot_stats.get("visible", 0))
        get_tree().quit(0 if visible > 0 else 2)

func _run_auto_probe() -> void:
    await _auto_probe_wait_frames(_env_int("AETHERKIRI_AUTO_PROBE_WARMUP_FRAMES", 180))
    await _save_auto_probe_step(0, "startup")
    var step := 1
    for pos in auto_probe_clicks:
        _send_probe_click(pos)
        await _auto_probe_wait_frames(_env_int("AETHERKIRI_AUTO_PROBE_AFTER_CLICK_FRAMES", 180))
        await _save_auto_probe_step(step, "click_%d_%d" % [int(pos.x), int(pos.y)])
        step += 1
    auto_probe_done = true
    auto_probe_running = false
    _write_probe_marker("auto_probe_done steps=%d renderer=%s" % [
        step,
        player.get_renderer_info(),
    ])
    if OS.get_environment("AETHERKIRI_QUIT_AFTER_AUTO_PROBE") == "1":
        get_tree().quit(0)

func _auto_probe_wait_frames(frames: int) -> void:
    for i in range(max(1, frames)):
        await get_tree().process_frame

func _save_auto_probe_step(index: int, label: String) -> void:
    await get_tree().process_frame
    await get_tree().process_frame
    var image := get_viewport().get_texture().get_image()
    var path := _default_output_path("aetherkiri-auto-step-%02d-%s.png" % [index, label])
    image.save_png(path)
    _write_probe_marker("auto_step index=%d label=%s output=%s stats=%s renderer=%s" % [
        index,
        label,
        path,
        JSON.stringify(_image_stats(image)),
        player.get_renderer_info(),
    ])

func _send_probe_click(window_pos: Vector2) -> void:
    var mapped := _map_probe_window_point(window_pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        _write_probe_marker("auto_click_skipped window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_UP, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    _write_probe_marker("auto_click window=%s mapped=%s" % [window_pos, mapped])

func _map_probe_window_point(pos: Vector2) -> Vector2:
    var tex_size := Vector2(max(1.0, float(last_texture_size.x)), max(1.0, float(last_texture_size.y)))
    var panel_size := Vector2(
        float(_env_int("AETHERKIRI_AUTO_PROBE_COORD_W", 1600)),
        float(_env_int("AETHERKIRI_AUTO_PROBE_COORD_H", 900))
    )
    var scale: float = min(panel_size.x / tex_size.x, panel_size.y / tex_size.y)
    if scale <= 0.0:
        return Vector2(-1.0, -1.0)
    var drawn_size := tex_size * scale
    var offset := (panel_size - drawn_size) * 0.5
    var inside := pos - offset
    if inside.x < 0.0 or inside.y < 0.0 or inside.x > drawn_size.x or inside.y > drawn_size.y:
        return Vector2(-1.0, -1.0)
    return inside / scale

func _frame_stats(frame: Dictionary) -> Dictionary:
    var data: PackedByteArray = frame.get("rgba", PackedByteArray())
    var visible := 0
    var sampled := 0
    var step: int = max(4, int(data.size() / 20000) & ~3)
    for i in range(0, data.size() - 3, step):
        sampled += 1
        if data[i + 3] > 0 and (data[i] > 8 or data[i + 1] > 8 or data[i + 2] > 8):
            visible += 1
    return {
        "bytes": data.size(),
        "sampled": sampled,
        "visible": visible,
    }

func _image_stats(image: Image) -> Dictionary:
    var visible := 0
    var sampled := 0
    var width := image.get_width()
    var height := image.get_height()
    var step_x: int = max(1, width / 160)
    var step_y: int = max(1, height / 90)
    for y in range(0, height, step_y):
        for x in range(0, width, step_x):
            sampled += 1
            var color := image.get_pixel(x, y)
            if color.a > 0.01 and (color.r > 0.03 or color.g > 0.03 or color.b > 0.03):
                visible += 1
    return {
        "width": width,
        "height": height,
        "sampled": sampled,
        "visible": visible,
    }

func _default_game_path() -> String:
    if OS.get_name() == "iOS":
        return ProjectSettings.globalize_path("user://Games")
    return ""

func _default_output_path(file_name: String) -> String:
    if OS.get_name() == "iOS":
        return "user://".path_join(file_name)
    return "/tmp".path_join(file_name)

func _parse_click_points(spec: String) -> Array[Vector2]:
    var clicks: Array[Vector2] = []
    if spec.is_empty():
        return clicks
    for item in spec.split(";"):
        var parts := item.split(",")
        if parts.size() == 2:
            clicks.push_back(Vector2(float(parts[0]), float(parts[1])))
    return clicks

func _env_int(name: String, fallback: int) -> int:
    var value := OS.get_environment(name)
    if value.is_empty():
        return fallback
    return int(value)

func _write_probe_marker(line: String) -> void:
    if OS.get_name() != "iOS":
        return
    var marker := FileAccess.open(_default_output_path("aetherkiri-device-probe.log"), FileAccess.READ_WRITE)
    if marker == null:
        marker = FileAccess.open(_default_output_path("aetherkiri-device-probe.log"), FileAccess.WRITE)
    if marker == null:
        return
    marker.seek_end()
    marker.store_line("%d %s" % [Time.get_ticks_msec(), line])
    marker.flush()

func _input(event: InputEvent) -> void:
    if game_running and viewport.visible:
        if _handle_game_pointer_event(event):
            get_viewport().set_input_as_handled()
            return

    if detail_view == null or detail_scroll == null or not detail_view.visible:
        return

    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        detail_touch_scroll_active = touch.pressed
        return

    if event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        _scroll_detail_by(-drag.relative.y)
        get_viewport().set_input_as_handled()
        return

    if event is InputEventPanGesture:
        var pan := event as InputEventPanGesture
        _scroll_detail_by(pan.delta.y)
        get_viewport().set_input_as_handled()
        return

    if event is InputEventMouseButton:
        var button := event as InputEventMouseButton
        if button.button_index == MOUSE_BUTTON_WHEEL_UP and button.pressed:
            _scroll_detail_by(-72.0)
            get_viewport().set_input_as_handled()
        elif button.button_index == MOUSE_BUTTON_WHEEL_DOWN and button.pressed:
            _scroll_detail_by(72.0)
            get_viewport().set_input_as_handled()
        return

    if event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
        var motion := event as InputEventMouseMotion
        if absf(motion.relative.y) > 1.0:
            _scroll_detail_by(-motion.relative.y)
            get_viewport().set_input_as_handled()

func _scroll_detail_by(delta: float) -> void:
    var bar := detail_scroll.get_v_scroll_bar()
    if bar == null:
        return
    var next := clampf(float(detail_scroll.scroll_vertical) + delta, bar.min_value, bar.max_value)
    detail_scroll.scroll_vertical = int(next)

func _on_viewport_input(event: InputEvent) -> void:
    _handle_game_pointer_event(event)

func _handle_game_pointer_event(event: InputEvent) -> bool:
    if event is InputEventMouseButton:
        var mouse_button := event as InputEventMouseButton
        var mapped := _map_viewport_point(mouse_button.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return false
        var event_type := POINTER_DOWN if mouse_button.pressed else POINTER_UP
        if mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP or mouse_button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
            event_type = POINTER_SCROLL
        var button := _map_mouse_button(mouse_button.button_index)
        if event_type == POINTER_DOWN:
            player.send_pointer_event(
                POINTER_MOVE,
                0,
                mapped.x,
                mapped.y,
                0.0,
                0.0,
                button
            )
            _pump_pointer_event_tick(1.0 / 60.0)
        player.send_pointer_event(
            event_type,
            0,
            mapped.x,
            mapped.y,
            0.0,
            -1.0 if mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0,
            button
        )
        if event_type != POINTER_SCROLL:
            _pump_pointer_event_tick(1.0 / 60.0)
        return true
    elif event is InputEventMouseMotion:
        var motion := event as InputEventMouseMotion
        var mapped := _map_viewport_point(motion.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return false
        var rel := _map_viewport_delta(motion.relative)
        player.send_pointer_event(
            POINTER_MOVE,
            0,
            mapped.x,
            mapped.y,
            rel.x,
            rel.y,
            0
        )
        return true
    elif event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        var mapped := _map_viewport_point(touch.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return false
        var event_type := POINTER_DOWN if touch.pressed else POINTER_UP
        if event_type == POINTER_DOWN:
            player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
            _pump_pointer_event_tick(1.0 / 60.0)
        player.send_pointer_event(event_type, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
        _pump_pointer_event_tick(1.0 / 60.0)
        return true
    elif event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        var mapped := _map_viewport_point(drag.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return false
        var rel := _map_viewport_delta(drag.relative)
        player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, rel.x, rel.y, 0)
        return true
    return false

func _map_viewport_point(pos: Vector2) -> Vector2:
    if viewport.texture == null:
        return pos
    var tex_size: Vector2 = Vector2(
        max(1.0, float(viewport.texture.get_width())),
        max(1.0, float(viewport.texture.get_height()))
    )
    var panel_size: Vector2 = viewport.size
    var scale: float = min(panel_size.x / tex_size.x, panel_size.y / tex_size.y)
    if scale <= 0.0:
        return Vector2(-1.0, -1.0)
    var drawn_size: Vector2 = tex_size * scale
    var offset: Vector2 = (panel_size - drawn_size) * 0.5
    var inside: Vector2 = pos - offset
    if inside.x < 0.0 or inside.y < 0.0 or inside.x > drawn_size.x or inside.y > drawn_size.y:
        return Vector2(-1.0, -1.0)
    return inside / scale

func _pump_pointer_event_tick(delta: float) -> void:
    if not game_running:
        return
    if player.get_startup_state() != STARTUP_SUCCEEDED:
        return
    var result := player.tick(delta)
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        print("Pointer event pump failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ])

func _map_viewport_delta(delta: Vector2) -> Vector2:
    if viewport.texture == null:
        return delta
    var tex_size: Vector2 = Vector2(
        max(1.0, float(viewport.texture.get_width())),
        max(1.0, float(viewport.texture.get_height()))
    )
    var panel_size: Vector2 = viewport.size
    var scale: float = min(panel_size.x / tex_size.x, panel_size.y / tex_size.y)
    return delta / max(0.0001, scale)

func _map_mouse_button(button_index: MouseButton) -> int:
    if button_index == MOUSE_BUTTON_RIGHT:
        return 1
    if button_index == MOUSE_BUTTON_MIDDLE:
        return 2
    return 0

func _unhandled_input(event: InputEvent) -> void:
    if not game_running:
        return
    if event is InputEventKey:
        var key := event as InputEventKey
        player.send_key_event(key.pressed, key.keycode, key.get_modifiers_mask(), key.unicode)

func _append_log(line: String) -> void:
    _write_probe_marker("log %s" % line)
    log_lines.append(line)
    while log_lines.size() > MAX_LOG_LINES:
        log_lines.remove_at(0)
    log_view.text = "\n".join(log_lines)
    call_deferred("_scroll_log_to_bottom")

func _scroll_log_to_bottom() -> void:
    if log_view == null:
        return
    log_view.scroll_vertical = max(0, log_view.get_line_count())
