extends Control

const BACKENDS := ["Godot Native", "GPU Bridge", "Debug CPU"]
const SETTINGS_KEY := "aether_kiri/render_backend"
const GAME_PATH_KEY := "aether_kiri/game_path"
const GAME_LIST_FILE := "user://aetherkiri_games.json"
const SETTINGS_FILE := "user://aetherkiri_settings.cfg"
const ANDROID_READ_EXTERNAL_STORAGE := "android.permission.READ_EXTERNAL_STORAGE"
const ANDROID_MANAGE_EXTERNAL_STORAGE := "android.permission.MANAGE_EXTERNAL_STORAGE"

const ENGINE_RESULT_OK := 0
const STARTUP_IDLE := 0
const STARTUP_RUNNING := 1
const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3

const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3
const POINTER_SCROLL := 4
const KEY_BACKSPACE := 0x08
const KEY_ENTER := 0x0D
const SOFT_KEYBOARD_MARKER := " "

var backend: OptionButton
var game_path: LineEdit
var restart_notice: Label
var viewport: TextureRect
var perf: Label
var runtime_menu_button: Button
var runtime_overlay: PanelContainer
var runtime_overlay_box: VBoxContainer
var game_menu_dialog: PanelContainer
var game_menu_list: VBoxContainer
var game_menu_title: Label
var game_menu_back_button: Button
var debug_panel: PanelContainer
var debug_text: Label
var soft_keyboard_input: LineEdit
var virtual_cursor: Label
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
var home_title: Label
var home_settings_button: Button
var empty_state: Control
var save_button: Button
var loading_margin: MarginContainer
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
var log_alerts := false
var error_dialog_logs := false
var dirty_settings := false
var active_game_path := ""
var active_game_started_msec := 0
var detail_touch_scroll_active := false
var rounded_card_shader: Shader
var upscale_shader: Shader
var opaque_frame_shader: Shader
var shown_system_alerts := {}
var android_storage_permission_requested := false
var runtime_overlay_visible := false
var game_menu_stack: Array[Dictionary] = []
var debug_panel_visible := false
var game_paused := false
var soft_keyboard_visible := false
var soft_keyboard_shadow_text := ""
var virtual_cursor_enabled := false
var virtual_cursor_position := Vector2.ZERO
var virtual_cursor_initialized := false
var virtual_cursor_active := false
var virtual_cursor_dragged := false

var player = null
var selected_backend := "Godot Native"
var upscale_algorithm := "sharp"
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
var startup_poll_accum := 0.0
var cached_startup_state := STARTUP_IDLE
var perf_log_interval := PERF_LOG_INTERVAL
var frame_spike_ms := 0.0
var frame_probe_enabled := false
var frame_probe_interval := 1.0
var frame_probe_accum := 0.0
var verbose_render_log := false
var web_auto_start_attempted := false
var perf_log_file: FileAccess
var log_lines: PackedStringArray = []
var suppress_mouse_until_msec := 0
var current_surface_size := Vector2i.ZERO
var render_surface_max_size := RENDER_SURFACE_MAX_SIZE
var cached_device_form := ""
const LOG_DRAIN_INTERVAL := 0.50
const STARTUP_POLL_INTERVAL := 0.16
const PERF_UPDATE_INTERVAL := 0.25
const PERF_LOG_INTERVAL := 2.0
const MAX_LOG_LINES := 240
const RENDER_SURFACE_SIZE := Vector2i(1280, 720)
const RENDER_SURFACE_MAX_SIZE := Vector2i(3200, 1800)
const INITIAL_WINDOW_SIZE := Vector2i(2240, 1260)
const DEFAULT_UI_DPI_SCALE := 1.35
const TOUCH_MOUSE_SUPPRESS_MS := 700
const VIRTUAL_CURSOR_HOTSPOT := Vector2(6, 6)
const PHONE_SHORT_SIDE_MAX := 1180.0
const PHONE_ASPECT_MIN := 1.35
const COLOR_BG := Color(0.944, 0.932, 0.895, 1.0)
const COLOR_GAME_BG := Color(0, 0, 0, 1)
const COLOR_CARD := Color(0.985, 0.98, 0.955, 1.0)
const COLOR_TEXT := Color(0.12, 0.11, 0.10, 1.0)
const COLOR_MUTED := Color(0.46, 0.45, 0.42, 1.0)
const COLOR_ACCENT := Color(0.78, 0.35, 0.22, 1.0)
const COLOR_ACCENT_SOFT := Color(0.90, 0.72, 0.64, 1.0)
const COLOR_LINE := Color(0.84, 0.82, 0.76, 1.0)
const HOME_CARD_SIZE := Vector2(260, 350)

func _apply_ui_font() -> void:
    theme = Theme.new()

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
    viewport.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
    viewport.visible = false
    add_child(viewport)
    _apply_upscale_algorithm()

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

    _build_runtime_controls()
    _build_loading_panel()
    _fit_full_rects()

func _load_shell_settings() -> void:
    var cfg := ConfigFile.new()
    if cfg.load(SETTINGS_FILE) != OK:
        return
    selected_backend = String(cfg.get_value("rendering", "backend", selected_backend))
    upscale_algorithm = String(cfg.get_value("rendering", "upscale_algorithm", upscale_algorithm))
    if not upscale_algorithm in ["sharp", "nearest", "linear"]:
        upscale_algorithm = "sharp"
    show_perf_monitor = bool(cfg.get_value("rendering", "perf_overlay", show_perf_monitor))
    frame_limit_enabled = bool(cfg.get_value("rendering", "fps_limit_enabled", frame_limit_enabled))
    target_fps = int(cfg.get_value("rendering", "target_fps", target_fps))
    lock_landscape = bool(cfg.get_value("rendering", "force_landscape", lock_landscape))
    plugin_trace = bool(cfg.get_value("developer", "plugin_trace", plugin_trace))
    mock_enabled = bool(cfg.get_value("developer", "mock_enabled", mock_enabled))
    console_log_file = bool(cfg.get_value("developer", "console_log_file", console_log_file))
    trace_log = bool(cfg.get_value("developer", "trace_log", trace_log))
    export_scripts = bool(cfg.get_value("developer", "export_scripts", export_scripts))
    log_alerts = bool(cfg.get_value("developer", "log_alerts", log_alerts))
    error_dialog_logs = bool(cfg.get_value("developer", "error_dialog_logs", error_dialog_logs))

func _save_shell_settings() -> void:
    var cfg := ConfigFile.new()
    cfg.set_value("rendering", "backend", selected_backend)
    cfg.set_value("rendering", "upscale_algorithm", upscale_algorithm)
    cfg.set_value("rendering", "perf_overlay", show_perf_monitor)
    cfg.set_value("rendering", "fps_limit_enabled", frame_limit_enabled)
    cfg.set_value("rendering", "target_fps", target_fps)
    cfg.set_value("rendering", "force_landscape", lock_landscape)
    cfg.set_value("developer", "plugin_trace", plugin_trace)
    cfg.set_value("developer", "mock_enabled", mock_enabled)
    cfg.set_value("developer", "console_log_file", console_log_file)
    cfg.set_value("developer", "trace_log", trace_log)
    cfg.set_value("developer", "export_scripts", export_scripts)
    cfg.set_value("developer", "log_alerts", log_alerts)
    cfg.set_value("developer", "error_dialog_logs", error_dialog_logs)
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
    player.set_engine_option("error_dialog_logs", "1" if error_dialog_logs else "0")

func _apply_shell_runtime_settings() -> void:
    if not _is_touch_platform():
        return
    if (game_running or not active_game_path.is_empty()) and lock_landscape:
        DisplayServer.screen_set_orientation(DisplayServer.SCREEN_SENSOR_LANDSCAPE)
        return
    if _is_phone_layout():
        DisplayServer.screen_set_orientation(DisplayServer.SCREEN_SENSOR_PORTRAIT)
        return
    DisplayServer.screen_set_orientation(DisplayServer.SCREEN_SENSOR)

func _window_safe_area() -> Rect2:
    if not _is_touch_platform():
        return Rect2(Vector2.ZERO, get_viewport_rect().size)
    var safe := DisplayServer.get_display_safe_area()
    if safe.size.x <= 0 or safe.size.y <= 0:
        return Rect2(Vector2.ZERO, get_viewport_rect().size)
    return Rect2(Vector2(safe.position), Vector2(safe.size))

func _safe_insets() -> Vector4:
    var window_size := get_viewport_rect().size
    var safe := _window_safe_area()
    return Vector4(
        maxf(0.0, safe.position.x),
        maxf(0.0, safe.position.y),
        maxf(0.0, window_size.x - safe.position.x - safe.size.x),
        maxf(0.0, window_size.y - safe.position.y - safe.size.y)
    )

func _safe_content_rect(extra_margin: float = 0.0) -> Rect2:
    var window_size := get_viewport_rect().size
    var insets := _safe_insets()
    var left := insets.x + extra_margin
    var top := insets.y + extra_margin
    var right := insets.z + extra_margin
    var bottom := insets.w + extra_margin
    return Rect2(
        Vector2(left, top),
        Vector2(maxf(1.0, window_size.x - left - right), maxf(1.0, window_size.y - top - bottom))
    )

func _is_phone_layout() -> bool:
    if not _is_touch_platform():
        return false
    var window_size := get_viewport_rect().size
    if window_size.x <= 0.0 or window_size.y <= 0.0:
        window_size = Vector2(DisplayServer.window_get_size())
    var short_side := minf(window_size.x, window_size.y)
    var long_side := maxf(window_size.x, window_size.y)
    return short_side <= PHONE_SHORT_SIDE_MAX and (long_side / maxf(short_side, 1.0)) >= PHONE_ASPECT_MIN

func _device_form() -> String:
    if _is_phone_layout():
        return "phone"
    if _is_touch_platform():
        return "large_touch"
    return "large"

func _update_device_form() -> void:
    var next := _device_form()
    if cached_device_form == next:
        return
    cached_device_form = next
    if settings_view != null and settings_view.visible:
        call_deferred("_rebuild_settings_view")

func _fit_full_rects() -> void:
    var window_size := get_viewport_rect().size
    _update_device_form()
    anchor_left = 0.0
    anchor_top = 0.0
    anchor_right = 0.0
    anchor_bottom = 0.0
    position = Vector2.ZERO
    size = window_size
    var controls: Array[Control] = [bg_rect, game_view, shell_root, home_view, settings_view, detail_view, detail_scroll, modal_layer]
    for control in controls:
        if control == null:
            continue
        control.set_anchors_preset(Control.PRESET_FULL_RECT)
        control.offset_left = 0.0
        control.offset_top = 0.0
        control.offset_right = 0.0
        control.offset_bottom = 0.0
    _layout_game_viewport(window_size)
    _layout_home_view(window_size)
    _layout_loading_panel()
    _layout_game_overlay()

func _layout_game_viewport(window_size: Vector2) -> void:
    if viewport == null:
        return
    viewport.anchor_left = 0.0
    viewport.anchor_top = 0.0
    viewport.anchor_right = 0.0
    viewport.anchor_bottom = 0.0
    viewport.offset_left = 0.0
    viewport.offset_top = 0.0
    viewport.offset_right = 0.0
    viewport.offset_bottom = 0.0

    var tex_size := Vector2(
        max(1.0, float(last_texture_size.x)),
        max(1.0, float(last_texture_size.y))
    )
    if viewport.texture != null:
        tex_size = Vector2(
            max(1.0, float(viewport.texture.get_width())),
            max(1.0, float(viewport.texture.get_height()))
        )

    var content_rect := Rect2(Vector2.ZERO, window_size)
    if _is_phone_layout() and not game_running:
        content_rect = _safe_content_rect(0.0)
    var scale := minf(content_rect.size.x / tex_size.x, content_rect.size.y / tex_size.y)
    scale = minf(scale, _max_game_view_scale())
    if scale <= 0.0:
        scale = 1.0
    var draw_size := Vector2(
        floor(tex_size.x * scale),
        floor(tex_size.y * scale)
    )
    viewport.position = (content_rect.position + (content_rect.size - draw_size) * 0.5).floor()
    viewport.size = draw_size
    viewport.custom_minimum_size = draw_size

func _max_game_view_scale() -> float:
    var value := OS.get_environment("AETHERKIRI_GAME_VIEW_MAX_SCALE").strip_edges()
    if not value.is_empty():
        return clampf(value.to_float(), 0.25, 8.0)
    return 8.0

func _upscale_material() -> ShaderMaterial:
    if upscale_shader == null:
        upscale_shader = Shader.new()
        upscale_shader.code = """
shader_type canvas_item;
uniform float sharpness = 0.38;

float cubic_weight(float x, float offset) {
    float t = x - offset;
    float at = abs(t);
    if (at <= 1.0) {
        return 1.5 * at * at * at - 2.5 * at * at + 1.0;
    }
    if (at < 2.0) {
        return -0.5 * at * at * at + 2.5 * at * at - 4.0 * at + 2.0;
    }
    return 0.0;
}

void fragment() {
    vec2 px = TEXTURE_PIXEL_SIZE;
    vec2 source_size = vec2(1.0) / px;
    vec2 pos = UV * source_size - vec2(0.5);
    vec2 base = floor(pos);
    vec2 f = pos - base;
    vec4 center = vec4(0.0);
    float weight_sum = 0.0;
    for (int y = -1; y <= 2; y++) {
        float wy = cubic_weight(f.y, float(y));
        for (int x = -1; x <= 2; x++) {
            float wx = cubic_weight(f.x, float(x));
            float w = wx * wy;
            vec2 sample_uv = (base + vec2(float(x), float(y)) + vec2(0.5)) * px;
            sample_uv = clamp(sample_uv, px * 0.5, vec2(1.0) - px * 0.5);
            center += texture(TEXTURE, sample_uv) * w;
            weight_sum += w;
        }
    }
    center /= max(weight_sum, 0.0001);
    vec4 left = texture(TEXTURE, clamp(UV + vec2(-px.x, 0.0), px * 0.5, vec2(1.0) - px * 0.5));
    vec4 right = texture(TEXTURE, clamp(UV + vec2(px.x, 0.0), px * 0.5, vec2(1.0) - px * 0.5));
    vec4 up = texture(TEXTURE, clamp(UV + vec2(0.0, -px.y), px * 0.5, vec2(1.0) - px * 0.5));
    vec4 down = texture(TEXTURE, clamp(UV + vec2(0.0, px.y), px * 0.5, vec2(1.0) - px * 0.5));
    vec3 local_min = min(center.rgb, min(min(left.rgb, right.rgb), min(up.rgb, down.rgb)));
    vec3 local_max = max(center.rgb, max(max(left.rgb, right.rgb), max(up.rgb, down.rgb)));
    vec3 blur = (left.rgb + right.rgb + up.rgb + down.rgb) * 0.25;
    vec3 sharpened = center.rgb + (center.rgb - blur) * sharpness;
    COLOR = vec4(clamp(sharpened, local_min, local_max), 1.0);
}
"""
    var material := ShaderMaterial.new()
    material.shader = upscale_shader
    material.set_shader_parameter("sharpness", 0.38)
    return material

func _opaque_frame_material() -> ShaderMaterial:
    if opaque_frame_shader == null:
        opaque_frame_shader = Shader.new()
        opaque_frame_shader.code = """
shader_type canvas_item;

void fragment() {
    vec4 tex = texture(TEXTURE, UV);
    COLOR = vec4(tex.rgb, 1.0);
}
"""
    var material := ShaderMaterial.new()
    material.shader = opaque_frame_shader
    return material

func _apply_upscale_algorithm() -> void:
    if viewport == null:
        return
    match upscale_algorithm:
        "nearest":
            viewport.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
            viewport.material = _opaque_frame_material()
        "linear":
            viewport.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
            viewport.material = _opaque_frame_material()
        _:
            viewport.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
            viewport.material = _upscale_material()

func _set_game_background(active: bool) -> void:
    var color := COLOR_GAME_BG if active else COLOR_BG
    if bg_rect != null:
        bg_rect.color = color
    RenderingServer.set_default_clear_color(color)

func _layout_home_view(window_size: Vector2) -> void:
    if game_scroll == null or game_list == null:
        return
    var safe := _safe_content_rect(0.0)
    var is_phone := _is_phone_layout()
    var margin := 20.0 if is_phone else 32.0
    var top_bar := 92.0 if is_phone else 132.0
    var bottom_reserved := 100.0 if is_phone else 132.0
    var list_top := safe.position.y + top_bar
    var list_width := maxf(260.0, safe.size.x - margin * 2.0)
    var safe_bottom := window_size.y - safe.position.y - safe.size.y
    var list_height := maxf(160.0, window_size.y - list_top - bottom_reserved - safe_bottom)
    game_scroll.position = Vector2(safe.position.x + margin, list_top)
    game_scroll.size = Vector2(list_width, list_height)
    game_scroll.custom_minimum_size = game_scroll.size

    var gap := 14.0 if is_phone else 18.0
    var columns := 1 if is_phone else maxi(1, int(floor((list_width + gap) / (HOME_CARD_SIZE.x + gap))))
    game_list.columns = columns
    game_list.add_theme_constant_override("h_separation", gap)
    game_list.add_theme_constant_override("v_separation", gap)
    game_list.custom_minimum_size = Vector2(list_width, 0)

    if home_title != null:
        home_title.position = Vector2(safe.position.x + margin + (0.0 if is_phone else 6.0), safe.position.y + (24.0 if is_phone else 96.0))
        home_title.add_theme_font_size_override("font_size", 28 if is_phone else 28)

    if home_settings_button != null:
        home_settings_button.anchor_left = 0.0
        home_settings_button.anchor_right = 0.0
        home_settings_button.position = Vector2(safe.position.x + safe.size.x - margin - 64.0, safe.position.y + (14.0 if is_phone else 88.0))

    if home_actions != null:
        home_actions.anchor_left = 0.0
        home_actions.anchor_top = 1.0
        home_actions.anchor_right = 0.0
        home_actions.anchor_bottom = 1.0
        var action_width := minf(358.0, safe.size.x - margin * 2.0)
        home_actions.offset_left = safe.position.x + safe.size.x - margin - action_width
        home_actions.offset_top = -safe_bottom - 84.0
        home_actions.offset_right = home_actions.offset_left + action_width
        home_actions.offset_bottom = -safe_bottom - 20.0
        home_actions.move_to_front()

func _layout_loading_panel() -> void:
    if loading_margin == null:
        return
    var safe := _safe_content_rect(0.0)
    var inset := 24.0 if _is_phone_layout() else 34.0
    loading_margin.add_theme_constant_override("margin_left", int(safe.position.x + inset))
    loading_margin.add_theme_constant_override("margin_top", int(safe.position.y + inset))
    loading_margin.add_theme_constant_override("margin_right", int(_safe_insets().z + inset))
    loading_margin.add_theme_constant_override("margin_bottom", int(_safe_insets().w + inset))

func _layout_game_overlay() -> void:
    var safe := _safe_content_rect(0.0)
    if perf != null:
        perf.position = safe.position + Vector2(24, 18)
    if restart_notice != null:
        restart_notice.position = safe.position + Vector2(24, 44)
    if runtime_menu_button != null:
        runtime_menu_button.position = Vector2(safe.position.x + safe.size.x - 84.0, safe.position.y + 16.0)
    if runtime_overlay != null:
        runtime_overlay.position = Vector2(safe.position.x + safe.size.x - runtime_overlay.size.x - 16.0, safe.position.y + 86.0)
    if game_menu_dialog != null:
        var dialog_size := Vector2(minf(520.0, safe.size.x - 32.0), minf(620.0, safe.size.y - 48.0))
        game_menu_dialog.size = dialog_size
        game_menu_dialog.position = safe.position + (safe.size - dialog_size) * 0.5
    if debug_panel != null:
        var panel_size := Vector2(minf(720.0, safe.size.x - 32.0), minf(260.0, safe.size.y - 32.0))
        debug_panel.size = panel_size
        debug_panel.position = safe.position + Vector2(16, safe.size.y - panel_size.y - 16)
    if soft_keyboard_input != null:
        soft_keyboard_input.position = safe.position + Vector2(8, safe.size.y - 36.0)
    _layout_virtual_cursor()

func _build_runtime_controls() -> void:
    runtime_menu_button = _runtime_icon_button("☰")
    runtime_menu_button.visible = false
    runtime_menu_button.pressed.connect(_toggle_runtime_overlay)
    game_view.add_child(runtime_menu_button)

    runtime_overlay = PanelContainer.new()
    runtime_overlay.visible = false
    runtime_overlay.size = Vector2(360, 430)
    runtime_overlay.mouse_filter = Control.MOUSE_FILTER_STOP
    runtime_overlay.add_theme_stylebox_override("panel", _panel_style(24, Color(0, 0, 0, 0.90), Color(1, 1, 1, 0.10), 1))
    game_view.add_child(runtime_overlay)

    runtime_overlay_box = VBoxContainer.new()
    runtime_overlay_box.add_theme_constant_override("separation", 0)
    runtime_overlay.add_child(runtime_overlay_box)

    _rebuild_runtime_overlay()
    _build_game_menu_dialog()
    _build_debug_panel()
    _build_mobile_input_helpers()

func _runtime_icon_button(text: String) -> Button:
    var button := Button.new()
    button.text = text
    button.custom_minimum_size = Vector2(68, 68)
    button.add_theme_font_size_override("font_size", 36)
    button.add_theme_color_override("font_color", Color(0.86, 0.86, 0.82, 1))
    _apply_button_style(
        button,
        _panel_style(18, Color(0, 0, 0, 0.62), Color(1, 1, 1, 0.05), 1),
        _panel_style(18, Color(0.08, 0.08, 0.08, 0.82), Color(1, 1, 1, 0.12), 1),
        _panel_style(18, Color(0.16, 0.16, 0.16, 0.92), Color(1, 1, 1, 0.12), 1)
    )
    return button

func _runtime_action(text: String, callback: Callable, destructive: bool = false) -> Button:
    var button := Button.new()
    button.text = text
    button.alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.clip_text = true
    button.custom_minimum_size = Vector2(320, 62)
    button.add_theme_font_size_override("font_size", 24)
    button.add_theme_color_override("font_color", Color(0.86, 0.86, 0.82, 1) if not destructive else Color(0.92, 0.25, 0.25, 1))
    _apply_button_style(
        button,
        _panel_style(6, Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0),
        _panel_style(6, Color(1, 1, 1, 0.08), Color(0, 0, 0, 0), 0),
        _panel_style(6, Color(1, 1, 1, 0.13), Color(0, 0, 0, 0), 0)
    )
    button.pressed.connect(callback)
    return button

func _runtime_separator() -> Control:
    var line := ColorRect.new()
    line.color = Color(1, 1, 1, 0.24)
    line.custom_minimum_size = Vector2(0, 1)
    return line

func _rebuild_runtime_overlay() -> void:
    if runtime_overlay_box == null:
        return
    for child in runtime_overlay_box.get_children():
        child.queue_free()
    runtime_overlay_box.add_child(_runtime_action("▤   Game Menu", _open_game_menu))
    if _is_touch_platform():
        runtime_overlay_box.add_child(_runtime_action(("☞   Disable Mouse Cursor" if virtual_cursor_enabled else "◖   Enable Mouse Cursor"), _toggle_virtual_cursor))
        runtime_overlay_box.add_child(_runtime_action(("⌨   Hide Keyboard" if soft_keyboard_visible else "⌨   Show Keyboard"), _toggle_soft_keyboard))
    runtime_overlay_box.add_child(_runtime_separator())
    runtime_overlay_box.add_child(_runtime_action(("⚙   Hide Debug" if debug_panel_visible else "⚙   Show Debug"), _toggle_debug_panel))
    runtime_overlay_box.add_child(_runtime_action(("▶   Resume" if game_paused else "Ⅱ   Pause"), _toggle_game_pause))
    runtime_overlay_box.add_child(_runtime_separator())
    runtime_overlay_box.add_child(_runtime_action("↪   Exit Game", _exit_current_game, true))

func _toggle_runtime_overlay() -> void:
    runtime_overlay_visible = not runtime_overlay_visible
    runtime_overlay.visible = runtime_overlay_visible
    if runtime_overlay_visible:
        _rebuild_runtime_overlay()
        runtime_overlay.move_to_front()

func _hide_runtime_overlay() -> void:
    runtime_overlay_visible = false
    if runtime_overlay != null:
        runtime_overlay.visible = false

func _build_game_menu_dialog() -> void:
    game_menu_dialog = PanelContainer.new()
    game_menu_dialog.visible = false
    game_menu_dialog.mouse_filter = Control.MOUSE_FILTER_STOP
    game_menu_dialog.add_theme_stylebox_override("panel", _panel_style(14, Color(0, 0, 0, 0.94), Color(1, 1, 1, 0.12), 1))
    game_view.add_child(game_menu_dialog)

    var margin := MarginContainer.new()
    margin.add_theme_constant_override("margin_left", 12)
    margin.add_theme_constant_override("margin_top", 10)
    margin.add_theme_constant_override("margin_right", 12)
    margin.add_theme_constant_override("margin_bottom", 12)
    game_menu_dialog.add_child(margin)

    var layout := VBoxContainer.new()
    layout.add_theme_constant_override("separation", 10)
    margin.add_child(layout)

    var header := HBoxContainer.new()
    header.custom_minimum_size = Vector2(0, 54)
    layout.add_child(header)

    game_menu_back_button = _runtime_icon_button("<")
    game_menu_back_button.custom_minimum_size = Vector2(48, 48)
    game_menu_back_button.add_theme_font_size_override("font_size", 24)
    game_menu_back_button.pressed.connect(func():
        if not game_menu_stack.is_empty():
            game_menu_stack.remove_at(game_menu_stack.size() - 1)
            _rebuild_game_menu_dialog()
    )
    header.add_child(game_menu_back_button)

    game_menu_title = Label.new()
    game_menu_title.text = "Game Menu"
    game_menu_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    game_menu_title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    game_menu_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    game_menu_title.clip_text = true
    game_menu_title.add_theme_font_size_override("font_size", 22)
    game_menu_title.add_theme_color_override("font_color", Color(0.92, 0.92, 0.88, 1))
    header.add_child(game_menu_title)

    var close := _runtime_icon_button("×")
    close.custom_minimum_size = Vector2(48, 48)
    close.add_theme_font_size_override("font_size", 30)
    close.pressed.connect(func(): game_menu_dialog.visible = false)
    header.add_child(close)

    var line := _runtime_separator()
    layout.add_child(line)

    var scroll := ScrollContainer.new()
    scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
    scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    layout.add_child(scroll)

    game_menu_list = VBoxContainer.new()
    game_menu_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    game_menu_list.add_theme_constant_override("separation", 2)
    scroll.add_child(game_menu_list)

func _build_debug_panel() -> void:
    debug_panel = PanelContainer.new()
    debug_panel.visible = false
    debug_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
    debug_panel.add_theme_stylebox_override("panel", _panel_style(10, Color(0, 0, 0, 0.72), Color(1, 1, 1, 0.12), 1))
    game_view.add_child(debug_panel)

    var margin := MarginContainer.new()
    margin.add_theme_constant_override("margin_left", 14)
    margin.add_theme_constant_override("margin_top", 12)
    margin.add_theme_constant_override("margin_right", 14)
    margin.add_theme_constant_override("margin_bottom", 12)
    debug_panel.add_child(margin)

    debug_text = Label.new()
    debug_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    debug_text.add_theme_font_size_override("font_size", 14)
    debug_text.add_theme_color_override("font_color", Color(0.90, 0.92, 0.88, 1))
    margin.add_child(debug_text)

func _build_mobile_input_helpers() -> void:
    if not _is_touch_platform():
        return
    soft_keyboard_input = LineEdit.new()
    soft_keyboard_input.visible = false
    soft_keyboard_input.size = Vector2(1, 1)
    soft_keyboard_input.modulate = Color(1, 1, 1, 0.01)
    soft_keyboard_input.mouse_filter = Control.MOUSE_FILTER_IGNORE
    soft_keyboard_input.virtual_keyboard_enabled = true
    soft_keyboard_input.text_changed.connect(_on_soft_keyboard_text_changed)
    soft_keyboard_input.text_submitted.connect(func(_text: String): _send_key_tap(KEY_ENTER, KEY_ENTER))
    game_view.add_child(soft_keyboard_input)

    virtual_cursor = Label.new()
    virtual_cursor.visible = false
    virtual_cursor.text = "◢"
    virtual_cursor.mouse_filter = Control.MOUSE_FILTER_IGNORE
    virtual_cursor.custom_minimum_size = Vector2(48, 48)
    virtual_cursor.add_theme_font_size_override("font_size", 42)
    virtual_cursor.add_theme_color_override("font_color", Color(1, 1, 1, 0.92))
    virtual_cursor.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.85))
    virtual_cursor.add_theme_constant_override("shadow_offset_x", 2)
    virtual_cursor.add_theme_constant_override("shadow_offset_y", 2)
    game_view.add_child(virtual_cursor)

func _open_game_menu() -> void:
    _hide_runtime_overlay()
    if player == null:
        return
    var menu_json := String(player.get_main_menu_json())
    var parsed = JSON.parse_string(menu_json)
    if not (parsed is Array):
        _append_log("Game menu unavailable: %s" % player.get_last_error())
        return
    game_menu_stack.clear()
    game_menu_stack.append({"caption": "Game Menu", "children": _visible_menu_entries(parsed as Array)})
    _rebuild_game_menu_dialog()
    game_menu_dialog.visible = true
    game_menu_dialog.move_to_front()

func _visible_menu_entries(entries: Array) -> Array[Dictionary]:
    var result: Array[Dictionary] = []
    for item in entries:
        if not (item is Dictionary):
            continue
        var entry := item as Dictionary
        if not bool(entry.get("visible", true)):
            continue
        var raw_children = entry.get("children", [])
        entry["children"] = _visible_menu_entries(raw_children if raw_children is Array else [])
        result.append(entry)
    return result

func _rebuild_game_menu_dialog() -> void:
    if game_menu_list == null or game_menu_stack.is_empty():
        return
    for child in game_menu_list.get_children():
        child.queue_free()

    var current := game_menu_stack[game_menu_stack.size() - 1]
    game_menu_title.text = String(current.get("caption", "Game Menu"))
    game_menu_back_button.visible = game_menu_stack.size() > 1
    var entries: Array = current.get("children", [])
    if entries.is_empty():
        var empty := Label.new()
        empty.text = "No menu items"
        empty.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
        empty.add_theme_font_size_override("font_size", 18)
        empty.add_theme_color_override("font_color", Color(1, 1, 1, 0.45))
        empty.custom_minimum_size = Vector2(0, 72)
        game_menu_list.add_child(empty)
        return

    for entry in entries:
        if not (entry is Dictionary):
            continue
        var item := entry as Dictionary
        var children: Array = item.get("children", [])
        var caption := String(item.get("caption", ""))
        if caption.strip_edges().is_empty() and children.is_empty():
            game_menu_list.add_child(_runtime_separator())
            continue
        game_menu_list.add_child(_game_menu_entry_button(item))

func _game_menu_entry_button(entry: Dictionary) -> Button:
    var button := Button.new()
    var children: Array = entry.get("children", [])
    var enabled := bool(entry.get("enabled", true))
    var caption := String(entry.get("caption", "(Unnamed)"))
    var prefix := ""
    if bool(entry.get("checked", false)):
        prefix = "●  " if bool(entry.get("radio", false)) else "✓  "
    var suffix := "  >" if not children.is_empty() else ""
    button.text = "%s%s%s" % [prefix, caption, suffix]
    button.alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.clip_text = true
    button.disabled = not enabled
    button.custom_minimum_size = Vector2(0, 52)
    button.add_theme_font_size_override("font_size", 18)
    button.add_theme_color_override("font_color", Color(0.92, 0.92, 0.88, 1))
    button.add_theme_color_override("font_disabled_color", Color(1, 1, 1, 0.35))
    _apply_button_style(
        button,
        _panel_style(4, Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0),
        _panel_style(4, Color(1, 1, 1, 0.08), Color(0, 0, 0, 0), 0),
        _panel_style(4, Color(1, 1, 1, 0.14), Color(0, 0, 0, 0), 0)
    )
    button.pressed.connect(func():
        if not children.is_empty():
            game_menu_stack.append(entry)
            _rebuild_game_menu_dialog()
            return
        var path := String(entry.get("path", ""))
        if path.is_empty():
            return
        var result: int = int(player.activate_menu_item(path))
        if result != ENGINE_RESULT_OK:
            render_errors += 1
            _append_log("Menu action failed: %s %s" % [player.get_last_result(), player.get_last_error()])
        game_menu_dialog.visible = false
    )
    return button

func _toggle_virtual_cursor() -> void:
    if not _is_touch_platform():
        return
    virtual_cursor_enabled = not virtual_cursor_enabled
    virtual_cursor_active = false
    virtual_cursor_dragged = false
    if virtual_cursor_enabled and not virtual_cursor_initialized:
        _place_virtual_cursor_at_center()
    _layout_virtual_cursor()
    _rebuild_runtime_overlay()

func _toggle_soft_keyboard() -> void:
    if not _is_touch_platform() or soft_keyboard_input == null:
        return
    if soft_keyboard_visible:
        soft_keyboard_visible = false
        soft_keyboard_input.release_focus()
        soft_keyboard_input.visible = false
    else:
        soft_keyboard_visible = true
        soft_keyboard_shadow_text = SOFT_KEYBOARD_MARKER
        soft_keyboard_input.visible = true
        soft_keyboard_input.text = SOFT_KEYBOARD_MARKER
        soft_keyboard_input.caret_column = soft_keyboard_input.text.length()
        soft_keyboard_input.grab_focus()
    _rebuild_runtime_overlay()

func _toggle_debug_panel() -> void:
    debug_panel_visible = not debug_panel_visible
    if debug_panel != null:
        debug_panel.visible = debug_panel_visible
        if debug_panel_visible:
            _update_debug_panel()
            debug_panel.move_to_front()
    _rebuild_runtime_overlay()

func _toggle_game_pause() -> void:
    if player == null:
        return
    var result: int = int(player.resume() if game_paused else player.pause())
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        _append_log("Pause toggle failed: %s %s" % [player.get_last_result(), player.get_last_error()])
        return
    game_paused = not game_paused
    restart_notice.text = "Paused" if game_paused else ""
    _hide_runtime_overlay()
    _rebuild_runtime_overlay()

func _exit_current_game() -> void:
    _hide_runtime_overlay()
    if game_menu_dialog != null:
        game_menu_dialog.visible = false
    if debug_panel != null:
        debug_panel.visible = false
    debug_panel_visible = false
    game_paused = false
    virtual_cursor_enabled = false
    virtual_cursor_initialized = false
    soft_keyboard_visible = false
    if soft_keyboard_input != null:
        soft_keyboard_input.release_focus()
        soft_keyboard_input.visible = false
    if virtual_cursor != null:
        virtual_cursor.visible = false
    _stop_runtime_player()
    _show_home()

func _stop_runtime_player() -> void:
    game_running = false
    cached_startup_state = STARTUP_IDLE
    current_surface_size = Vector2i.ZERO
    last_texture_size = Vector2i.ZERO
    _finalize_active_game_session()
    viewport.texture = null
    viewport.visible = false
    game_view.visible = false
    loading_panel.visible = false
    perf.visible = false
    restart_notice.visible = false
    if player != null:
        player.release_frame_texture()
        player.destroy_engine()
        player.queue_free()
        player = null
    if _create_runtime_player():
        call_deferred("_initialize_recreated_runtime_player")

func _initialize_recreated_runtime_player() -> void:
    if player == null:
        return
    var user_dir := OS.get_user_data_dir()
    var cache_dir := user_dir.path_join("cache")
    DirAccess.make_dir_recursive_absolute(cache_dir)
    if not player.initialize_engine(user_dir, cache_dir):
        render_errors += 1
        _append_log("Engine reinit failed: %s %s" % [player.get_last_result(), player.get_last_error()])
        return
    _apply_backend(false)
    _apply_engine_options()
    _apply_shell_runtime_settings()

func _layout_virtual_cursor() -> void:
    if virtual_cursor == null:
        return
    virtual_cursor.visible = virtual_cursor_enabled and virtual_cursor_initialized
    if not virtual_cursor.visible:
        return
    virtual_cursor.position = virtual_cursor_position - VIRTUAL_CURSOR_HOTSPOT
    virtual_cursor.move_to_front()

func _place_virtual_cursor_at_center() -> void:
    var rect := viewport.get_global_rect()
    virtual_cursor_position = rect.position + rect.size * 0.5
    virtual_cursor_initialized = true

func _update_debug_panel() -> void:
    if debug_text == null:
        return
    var startup_state := cached_startup_state
    var renderer := selected_backend
    var texture_backend := "none"
    var last_result := ""
    var last_error := ""
    if player != null:
        last_result = String(player.get_last_result())
        last_error = String(player.get_last_error())
        if game_running and startup_state == STARTUP_SUCCEEDED:
            renderer = String(player.get_renderer_info())
        texture_backend = String(player.get_frame_texture_backend()) if game_running else "none"
    debug_text.text = "FPS: %d\nState: %d%s\nRenderer: %s\nTexture: %s %dx%d\nErrors: %d\nGame: %s\nLast: %s %s" % [
        Engine.get_frames_per_second(),
        startup_state,
        " paused" if game_paused else "",
        _renderer_summary(renderer),
        texture_backend,
        last_texture_size.x,
        last_texture_size.y,
        render_errors,
        active_game_path,
        last_result,
        last_error,
    ]

func _on_soft_keyboard_text_changed(text: String) -> void:
    if soft_keyboard_input == null:
        return
    if text == soft_keyboard_shadow_text:
        return
    if text.length() < SOFT_KEYBOARD_MARKER.length():
        _send_key_tap(KEY_BACKSPACE, 0)
    else:
        var typed := text.substr(SOFT_KEYBOARD_MARKER.length())
        for index in range(typed.length()):
            var codepoint := typed.unicode_at(index)
            if codepoint > 0:
                _send_key_tap(codepoint, codepoint)
    soft_keyboard_shadow_text = SOFT_KEYBOARD_MARKER
    soft_keyboard_input.set_deferred("text", SOFT_KEYBOARD_MARKER)
    soft_keyboard_input.set_deferred("caret_column", SOFT_KEYBOARD_MARKER.length())

func _send_key_tap(key_code: int, unicode_codepoint: int) -> void:
    if player == null or not game_running:
        return
    player.send_key_event(true, key_code, 0, unicode_codepoint)
    player.send_key_event(false, key_code, 0, unicode_codepoint)

func _runtime_ui_contains_point(point: Vector2) -> bool:
    var controls: Array[Control] = [runtime_menu_button, runtime_overlay, game_menu_dialog, debug_panel]
    for control in controls:
        if control == null or not control.visible:
            continue
        if control.get_global_rect().has_point(point):
            return true
    return false

func _runtime_pointer_event_on_ui(event: InputEvent) -> bool:
    if event is InputEventMouseButton:
        return _runtime_ui_contains_point((event as InputEventMouseButton).position)
    if event is InputEventMouseMotion:
        return _runtime_ui_contains_point((event as InputEventMouseMotion).position)
    if event is InputEventScreenTouch:
        return _runtime_ui_contains_point((event as InputEventScreenTouch).position)
    if event is InputEventScreenDrag:
        return _runtime_ui_contains_point((event as InputEventScreenDrag).position)
    return false

func _build_home_view() -> void:
    home_view = Control.new()
    home_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    shell_root.add_child(home_view)

    home_title = Label.new()
    home_title.text = "AetherKiri"
    home_title.position = Vector2(38, 96)
    home_title.add_theme_font_size_override("font_size", 28)
    home_title.add_theme_color_override("font_color", COLOR_TEXT)
    home_view.add_child(home_title)

    home_settings_button = _icon_button("☰")
    home_settings_button.anchor_left = 1.0
    home_settings_button.anchor_right = 1.0
    home_settings_button.position = Vector2(-86, 92)
    home_settings_button.pressed.connect(_show_settings)
    home_view.add_child(home_settings_button)

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
    var safe := _safe_content_rect(0.0)
    var page_margin := 20 if _is_phone_layout() else 32
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", int(safe.position.x) + page_margin)
    margin.add_theme_constant_override("margin_top", int(safe.position.y) + (14 if _is_phone_layout() else 24))
    margin.add_theme_constant_override("margin_right", int(_safe_insets().z) + page_margin)
    margin.add_theme_constant_override("margin_bottom", int(_safe_insets().w) + 40)
    settings_view.add_child(margin)

    var page := VBoxContainer.new()
    page.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    page.add_theme_constant_override("separation", 26)
    margin.add_child(page)

    var top := HBoxContainer.new()
    top.custom_minimum_size = Vector2(0, 92 if _is_phone_layout() else 120)
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
    save_button.custom_minimum_size = Vector2(118 if _is_phone_layout() else 150, 64 if _is_phone_layout() else 72)
    save_button.pressed.connect(_save_shell_settings)
    top.add_child(save_button)

    page.add_child(_section_title("▰  渲染"))
    var render_card := _settings_card()
    page.add_child(render_card)
    render_card.add_child(_settings_block("渲染管线", "未运行游戏时立即生效；运行中切换需重启当前游戏", _backend_segment()))
    render_card.add_child(_settings_block("缩放算法", "拉伸低分辨率游戏画面时使用；超分会重建边缘并做轻度锐化", _upscale_select()))
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
    dev_card.add_child(_settings_toggle_row("日志级别弹窗", "将 warning/error/fatal 等日志行额外显示为系统提示；默认关闭", log_alerts, "log_alerts"))
    dev_card.add_child(_settings_toggle_row("错误弹窗附带日志", "真正异常弹窗中追加最近 20 行引擎日志；默认关闭", error_dialog_logs, "error_dialog_logs"))

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

    loading_margin = MarginContainer.new()
    loading_margin.add_theme_constant_override("margin_left", 34)
    loading_margin.add_theme_constant_override("margin_top", 30)
    loading_margin.add_theme_constant_override("margin_right", 34)
    loading_margin.add_theme_constant_override("margin_bottom", 30)
    loading_panel.add_child(loading_margin)

    var box := VBoxContainer.new()
    box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    box.size_flags_vertical = Control.SIZE_EXPAND_FILL
    box.add_theme_constant_override("separation", 16)
    loading_margin.add_child(box)

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

func _upscale_select() -> OptionButton:
    var select := OptionButton.new()
    select.custom_minimum_size = Vector2(360, 58)
    var options := [
        {"label": "Catmull-Rom 超分", "value": "sharp"},
        {"label": "Nearest", "value": "nearest"},
        {"label": "Linear", "value": "linear"},
    ]
    var selected_index := 0
    for i in range(options.size()):
        select.add_item(String(options[i]["label"]))
        select.set_item_metadata(i, String(options[i]["value"]))
        if String(options[i]["value"]) == upscale_algorithm:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_upscale_algorithm(String(select.get_item_metadata(index)))
    )
    return select

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
    elif key == "log_alerts":
        log_alerts = value
    elif key == "error_dialog_logs":
        error_dialog_logs = value
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

func _select_upscale_algorithm(value: String) -> void:
    if not value in ["sharp", "nearest", "linear"]:
        return
    upscale_algorithm = value
    _apply_upscale_algorithm()
    _mark_settings_dirty()

func _empty_help_text() -> String:
    if OS.get_name() == "iOS":
        return "使用「文件」App 将游戏文件夹复制到：\n我的 iPhone / iPad > AetherKiri > Games\n然后点击「刷新」"
    if OS.get_name() == "Web":
        return "点击「导入」选择本地游戏目录或 XP3 文件"
    return "点击「导入」选择游戏目录或 XP3 文件"

func _show_home() -> void:
    if dirty_settings:
        _save_shell_settings()
    _set_game_background(false)
    _apply_shell_runtime_settings()
    home_view.visible = true
    settings_view.visible = false
    detail_view.visible = false
    modal_layer.visible = false
    _refresh_games()

func _show_settings() -> void:
    _set_game_background(false)
    _apply_shell_runtime_settings()
    _rebuild_settings_view()
    home_view.visible = false
    settings_view.visible = true
    detail_view.visible = false
    modal_layer.visible = false

func _show_detail(game: Dictionary) -> void:
    _set_game_background(false)
    _apply_shell_runtime_settings()
    selected_game = game
    home_view.visible = false
    settings_view.visible = false
    detail_view.visible = true
    modal_layer.visible = false
    for child in detail_scroll.get_children():
        child.queue_free()

    var safe := _safe_content_rect(0.0)
    var is_phone := _is_phone_layout()
    var margin := 20.0 if is_phone else 32.0
    var content_width := safe.size.x
    var detail_width := maxf(320.0, content_width - margin * 2.0)
    var content_height := 760.0 if is_phone else 920.0

    var content := Control.new()
    content.custom_minimum_size = Vector2(content_width, content_height + safe.position.y + _safe_insets().w)
    content.mouse_filter = Control.MOUSE_FILTER_PASS
    detail_scroll.add_child(content)

    var back := _icon_button("‹")
    back.position = Vector2(safe.position.x + margin - 8.0, safe.position.y + (14.0 if is_phone else 42.0))
    back.pressed.connect(_show_home)
    content.add_child(back)

    var cover := PanelContainer.new()
    var cover_size := Vector2(minf(260.0, detail_width * 0.66), 190.0 if not is_phone else 150.0)
    cover.position = Vector2(safe.position.x + margin + (detail_width - cover_size.x) * 0.5, safe.position.y + (92.0 if is_phone else 100.0))
    cover.size = cover_size
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
    title.position = Vector2(safe.position.x + margin, cover.position.y + cover_size.y + 18.0)
    title.size = Vector2(detail_width, 54)
    title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    title.add_theme_font_size_override("font_size", 28 if is_phone else 34)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    content.add_child(title)

    var info := VBoxContainer.new()
    info.position = Vector2(safe.position.x + margin, title.position.y + 62.0)
    info.size = Vector2(detail_width, 170)
    info.add_theme_constant_override("separation", 12)
    content.add_child(info)
    info.add_child(_detail_line("□", String(game.get("path", ""))))
    info.add_child(_detail_line("◷", "上次游玩：%s" % (_game_subtitle(game).split(" · ")[0])))
    info.add_child(_detail_line("◴", "已玩 %s" % _format_play_duration(int(game.get("playDurationSeconds", 0)))))
    info.add_child(_detail_line("▤", String(game.get("type", "Directory"))))

    var start := _pill_button("▶  启动游戏")
    start.position = Vector2(safe.position.x + margin, info.position.y + 130.0)
    start.size = Vector2(detail_width, 68 if is_phone else 72)
    start.pressed.connect(_start_selected_game)
    content.add_child(start)

    var tools := VBoxContainer.new()
    tools.position = Vector2(safe.position.x + margin, start.position.y + start.size.y + 28.0)
    tools.size = Vector2(detail_width, 260)
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
    _show_system_alert(message, "AetherKiri")

func _show_system_alert(message: String, title: String = "AetherKiri") -> void:
    if message.strip_edges().is_empty():
        return
    OS.alert(message, title)

func _show_system_alert_once(key: String, message: String, title: String = "AetherKiri") -> void:
    if shown_system_alerts.has(key):
        return
    shown_system_alerts[key] = true
    _show_system_alert(message, title)

func _android_request_storage_permissions() -> void:
    if OS.get_name() != "Android" or android_storage_permission_requested:
        return
    android_storage_permission_requested = true
    OS.request_permissions()

func _on_request_permissions_result(permission: String, granted: bool) -> void:
    if OS.get_name() != "Android":
        return
    _append_log("Android permission result: %s=%s" % [permission, str(granted)])
    if granted:
        _refresh_games()

func _android_granted_permissions() -> PackedStringArray:
    if OS.get_name() != "Android":
        return PackedStringArray()
    return OS.get_granted_permissions()

func _android_storage_permission_granted() -> bool:
    if OS.get_name() != "Android":
        return true
    var granted := _android_granted_permissions()
    return granted.has(ANDROID_READ_EXTERNAL_STORAGE) or granted.has(ANDROID_MANAGE_EXTERNAL_STORAGE)

func _android_game_storage_root() -> String:
    return ProjectSettings.globalize_path("user://Games")

func _android_path_is_app_storage(path: String) -> bool:
    var normalized := path.simplify_path()
    var user_dir := OS.get_user_data_dir().simplify_path()
    var games_dir := _android_game_storage_root().simplify_path()
    return normalized.begins_with(user_dir) or normalized.begins_with(games_dir)

func _can_read_game_path(path: String) -> bool:
    if path.is_empty():
        return false
    if path.to_lower().ends_with(".xp3"):
        var file := FileAccess.open(path, FileAccess.READ)
        if file == null:
            return false
        file.close()
        return true
    var dir := DirAccess.open(path)
    if dir == null:
        return false
    dir.list_dir_begin()
    dir.list_dir_end()
    return true

func _ensure_android_game_path_access(path: String) -> bool:
    if OS.get_name() != "Android" or path.is_empty() or _android_path_is_app_storage(path):
        return true
    if _can_read_game_path(path):
        return true
    _android_request_storage_permissions()
    var games_dir := _android_game_storage_root()
    DirAccess.make_dir_recursive_absolute(games_dir)
    var permission_note := "请授予存储读取权限后重试。"
    if android_storage_permission_requested and not _android_storage_permission_granted():
        permission_note = "如果系统没有弹出权限，请到系统设置中授予 AetherKiri 文件/存储访问权限。"
    _show_message(
        "无法读取游戏文件，Android 可能限制了外部存储访问。\n\n%s\n\n也可以将游戏目录或 XP3 复制到：\n%s\n然后回到本应用刷新。" % [
            permission_note,
            games_dir,
        ]
    )
    return false

func _maybe_show_log_alert(line: String) -> void:
    if not log_alerts:
        return
    var message := line.strip_edges()
    if message.is_empty():
        return
    var lower := message.to_lower()
    var is_warning := lower.contains("warning") or lower.contains("(warning)") or lower.contains("警告")
    var is_error := lower.contains("error") or lower.contains("exception") or lower.contains("fatal") or lower.contains("failed") or lower.contains("错误") or lower.contains("失败")
    if not is_warning and not is_error:
        return
    var title := "AetherKiri 错误" if is_error else "AetherKiri 警告"
    _show_system_alert_once("log:%s" % message, message, title)

func _create_file_dialog(title: String, file_mode: int, filters: PackedStringArray = PackedStringArray()) -> FileDialog:
    var dialog := FileDialog.new()
    dialog.file_mode = file_mode
    dialog.access = FileDialog.ACCESS_FILESYSTEM
    dialog.use_native_dialog = true
    dialog.title = title
    for filter in filters:
        dialog.add_filter(filter)
    return dialog

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
    var dialog := _create_file_dialog(
        "选择封面图片",
        FileDialog.FILE_MODE_OPEN_FILE,
        PackedStringArray(["*.png,*.jpg,*.jpeg,*.webp;Image;image/png,image/jpeg,image/webp"])
    )
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
    if OS.get_name() == "Android":
        _android_request_storage_permissions()
    if OS.get_name() == "Web":
        _show_web_import_picker()
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

func _web_eval_string(source: String) -> String:
    if OS.get_name() != "Web":
        return ""
    var value = JavaScriptBridge.eval(source, true)
    if value == null:
        return ""
    return String(value)

func _web_sync_get_json(path: String):
    var source := "(function(){var xhr=new XMLHttpRequest();xhr.open('GET',%s,false);xhr.send(null);if(xhr.status>=200&&xhr.status<300)return xhr.responseText;return JSON.stringify({error:'HTTP '+xhr.status});})()" % JSON.stringify(path)
    var text := _web_eval_string(source)
    if text.is_empty():
        return null
    return JSON.parse_string(text)

func _web_game_from_mount_info(info: Dictionary) -> Dictionary:
    return {
        "name": String(info.get("name", "本地游戏")),
        "path": String(info.get("gamePath", info.get("path", ""))),
        "type": String(info.get("type", "Directory")),
        "lastPlayed": 0,
        "playDurationSeconds": 0,
        "coverPath": "",
        "developer": "",
        "title": String(info.get("title", "")),
        "webMountBackend": String(info.get("webMountBackend", "http")),
        "webMountBaseUrl": String(info.get("baseUrl", "")),
        "webMountGameId": String(info.get("webMountGameId", "")),
        "webMountPoint": String(info.get("mountPoint", info.get("webMountPoint", ""))),
    }

func _mount_web_game(game: Dictionary) -> bool:
    if OS.get_name() != "Web":
        return true
    var backend := String(game.get("webMountBackend", "http"))
    var base_url := String(game.get("webMountBaseUrl", ""))
    var game_id := String(game.get("webMountGameId", ""))
    var mount_point := String(game.get("webMountPoint", ""))
    if mount_point.is_empty() or (backend == "http" and base_url.is_empty()) or (backend == "blob" and game_id.is_empty()):
        return true
    var mounted_source := "(function(){return typeof AetherKiriIsHttpGameMounted==='function'&&AetherKiriIsHttpGameMounted(%s)?'1':'0';})()" % JSON.stringify(mount_point)
    if _web_eval_string(mounted_source) == "1":
        return true
    var mount_source := ""
    if backend == "blob":
        mount_source = "(function(){if(typeof AetherKiriMountLocalBlobGame!=='function')return JSON.stringify({ok:false,error:'Browser local mount API is not ready'});return AetherKiriMountLocalBlobGame(%s,%s);})()" % [
            JSON.stringify(mount_point),
            JSON.stringify(game_id),
        ]
    else:
        var manifest = _web_sync_get_json(base_url + "/manifest")
        if not manifest is Dictionary or not manifest.has("files"):
            _show_message("无法读取 Web 游戏挂载清单")
            return false
        var manifest_text := JSON.stringify(manifest)
        mount_source = "(function(){if(typeof AetherKiriMountHttpGame!=='function')return JSON.stringify({ok:false,error:'Web mount API is not ready'});return AetherKiriMountHttpGame(%s,%s,%s);})()" % [
            JSON.stringify(mount_point),
            JSON.stringify(base_url),
            JSON.stringify(manifest_text),
        ]
    var result_text := _web_eval_string(mount_source)
    var result = JSON.parse_string(result_text)
    if result is Dictionary and bool(result.get("ok", false)):
        return true
    var error := "未知错误"
    if result is Dictionary:
        error = String(result.get("error", error))
    _show_message("Web 本地挂载失败：%s" % error)
    return false

func _web_local_picker_support() -> Dictionary:
    var source := "(function(){if(typeof AetherKiriLocalPickerSupport!=='function')return JSON.stringify({directory:false,archive:false});return AetherKiriLocalPickerSupport();})()"
    var text := _web_eval_string(source)
    var parsed = JSON.parse_string(text)
    return parsed if parsed is Dictionary else {}

func _web_local_game_restore_state() -> Dictionary:
    var source := "(function(){if(typeof AetherKiriLocalGameRestoreState!=='function')return JSON.stringify({done:true});return AetherKiriLocalGameRestoreState();})()"
    var text := _web_eval_string(source)
    var parsed = JSON.parse_string(text)
    return parsed if parsed is Dictionary else {"done": true}

func _web_dev_mounts() -> Array:
    var response = _web_sync_get_json("/__aetherkiri/games")
    if not response is Dictionary or not response.has("games"):
        return []
    var games = response.get("games", [])
    return games if games is Array else []

func _web_dev_config() -> Dictionary:
    var response = _web_sync_get_json("/__aetherkiri/config")
    return response if response is Dictionary else {}

func _select_web_auto_start_mount(config: Dictionary, games: Array) -> Dictionary:
    var desired_name := String(config.get("autoStartName", "")).strip_edges()
    if not desired_name.is_empty():
        for item in games:
            if not item is Dictionary:
                continue
            var name := String(item.get("name", ""))
            var id := String(item.get("id", ""))
            var path := String(item.get("gamePath", item.get("path", "")))
            if name == desired_name or id == desired_name or path == desired_name:
                var named_match: Dictionary = item
                return named_match
        return {}

    var index := int(config.get("autoStartIndex", 0))
    if index < 0:
        index = 0
    if index < games.size() and games[index] is Dictionary:
        var indexed_match: Dictionary = games[index]
        return indexed_match
    for item in games:
        if item is Dictionary:
            var fallback_match: Dictionary = item
            return fallback_match
    return {}

func _save_game_dictionary_silent(game: Dictionary) -> bool:
    var path := String(game.get("path", ""))
    if path.is_empty() or not _path_exists(path):
        return false
    var games := _load_game_list()
    var next: Array[Dictionary] = []
    var replaced := false
    for existing in games:
        if String(existing.get("path", "")) != path:
            next.append(existing)
            continue
        var merged := _merge_game_dictionary(existing, game)
        next.append(merged)
        replaced = true
    if not replaced:
        next.append(game)
    _save_game_list(_dedupe_games(next))
    ProjectSettings.set_setting(GAME_PATH_KEY, path)
    ProjectSettings.save()
    game_path.text = path
    return true

func _auto_start_web_dev_game() -> void:
    if OS.get_name() != "Web" or web_auto_start_attempted:
        return
    web_auto_start_attempted = true
    var config := _web_dev_config()
    if not bool(config.get("autoStartGame", false)):
        return
    var dev_games := _web_dev_mounts()
    if dev_games.is_empty():
        _append_log("Web dev auto-start requested, but no AETHERKIRI_GAME_ROOT(S) mount is configured.")
        print("AetherKiri Web dev auto-start requested, but no AETHERKIRI_GAME_ROOT(S) mount is configured.")
        return
    var mount_info := _select_web_auto_start_mount(config, dev_games)
    if mount_info.is_empty():
        _append_log("Web dev auto-start did not find a matching game mount.")
        print("AetherKiri Web dev auto-start did not find a matching game mount.")
        return
    var game := _web_game_from_mount_info(mount_info)
    _append_log("Web dev auto-start mounting: %s" % _game_display_title(game))
    if not _mount_web_game(game):
        return
    selected_game = game
    if not _save_game_dictionary_silent(game):
        _append_log("Web dev auto-start could not persist the selected game.")
    _refresh_games()
    call_deferred("_start_selected_game")

func _pick_web_local_game(kind: String) -> void:
    var start_source := "(function(){if(typeof AetherKiriPickLocalGame!=='function')return JSON.stringify({ok:false,error:'Browser local picker is not ready'});return AetherKiriPickLocalGame(%s);})()" % JSON.stringify(kind)
    var start_result = JSON.parse_string(_web_eval_string(start_source))
    if not start_result is Dictionary or not bool(start_result.get("ok", false)):
        var start_error := "当前浏览器不支持本地文件选择"
        if start_result is Dictionary:
            start_error = String(start_result.get("error", start_error))
        _show_message(start_error)
        return

    var ticket := String(start_result.get("ticket", ""))
    if ticket.is_empty():
        _show_message("浏览器没有返回导入任务")
        return

    var deadline_msec := Time.get_ticks_msec() + 5 * 60 * 1000
    while Time.get_ticks_msec() < deadline_msec:
        await get_tree().create_timer(0.25).timeout
        var poll_source := "(function(){if(typeof AetherKiriTakeLocalGamePickResult!=='function')return JSON.stringify({status:'error',error:'Browser local picker is not ready'});return AetherKiriTakeLocalGamePickResult(%s);})()" % JSON.stringify(ticket)
        var poll_result = JSON.parse_string(_web_eval_string(poll_source))
        if not poll_result is Dictionary:
            continue
        var status := String(poll_result.get("status", ""))
        if status == "pending":
            continue
        if status == "cancelled":
            return
        if status == "error" or status == "missing":
            _show_message("本地游戏导入失败：%s" % String(poll_result.get("error", "未知错误")))
            return
        if status == "ok":
            var game_data = poll_result.get("game", {})
            if not game_data is Dictionary:
                _show_message("浏览器返回的游戏信息无效")
                return
            var game := _web_game_from_mount_info(game_data)
            if not _mount_web_game(game):
                return
            _add_game_dictionary(game)
            return
    _show_message("本地游戏导入超时")

func _show_web_import_picker() -> void:
    var support := _web_local_picker_support()
    var dev_games := _web_dev_mounts()
    if not bool(support.get("directory", false)) and not bool(support.get("archive", false)) and dev_games.is_empty():
        _show_message("当前浏览器不支持直接选择本地游戏文件。请使用支持 File System Access 或目录上传的浏览器。")
        return

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
    dialog.position = Vector2(-340, -220)
    dialog.size = Vector2(680, 440)
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

    if bool(support.get("directory", false)):
        var dir_button := _pill_button("选择本地游戏目录")
        dir_button.pressed.connect(func():
            modal_layer.visible = false
            _pick_web_local_game("directory")
        )
        box.add_child(dir_button)

    if bool(support.get("archive", false)):
        var archive_button := _pill_button("选择 XP3 文件")
        archive_button.pressed.connect(func():
            modal_layer.visible = false
            _pick_web_local_game("archive")
        )
        box.add_child(archive_button)

    for item in dev_games:
        if not item is Dictionary:
            continue
        var game := _web_game_from_mount_info(item)
        var captured_game := game.duplicate(true)
        var button := _pill_button("开发挂载  %s" % String(game.get("name", "")))
        button.pressed.connect(func():
            modal_layer.visible = false
            if not _mount_web_game(captured_game):
                return
            _add_game_dictionary(captured_game)
        )
        box.add_child(button)
    var cancel := Button.new()
    cancel.text = "取消"
    cancel.flat = true
    cancel.pressed.connect(func(): modal_layer.visible = false)
    box.add_child(cancel)

func _open_import_dialog(xp3: bool) -> void:
    var filters := PackedStringArray(["*.xp3,*.XP3;KiriKiri XP3 archive"]) if xp3 else PackedStringArray()
    var dialog := _create_file_dialog(
        "选择 XP3 文件" if xp3 else "选择游戏目录",
        FileDialog.FILE_MODE_OPEN_FILE if xp3 else FileDialog.FILE_MODE_OPEN_DIR,
        filters
    )
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
    if OS.get_name() == "iOS" or OS.get_name() == "Android":
        known_games = _scan_mobile_games_dir(known_games)
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
        if item is Dictionary and item.has("path") and _path_exists(String(item.get("path", ""))) and _web_game_entry_available(item):
            games.append(item)
    return games

func _save_game_list(games: Array[Dictionary]) -> void:
    var file := FileAccess.open(GAME_LIST_FILE, FileAccess.WRITE)
    if file != null:
        file.store_string(JSON.stringify(games))

func _scan_mobile_games_dir(existing: Array[Dictionary]) -> Array[Dictionary]:
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
    return _add_game_dictionary(_game_info_from_path(path))

func _add_game_dictionary(game: Dictionary) -> bool:
    var path := String(game.get("path", ""))
    if not _path_exists(path):
        _show_message("游戏路径不存在")
        return false
    var games := _load_game_list()
    var next: Array[Dictionary] = []
    var final_game := game
    var replaced := false
    for existing in games:
        if String(existing.get("path", "")) == path:
            if OS.get_name() != "Web":
                _show_message("游戏已存在：%s" % _game_display_title(existing))
                return false
            final_game = _merge_game_dictionary(existing, game)
            next.append(final_game)
            replaced = true
        else:
            next.append(existing)
    if not replaced:
        next.append(final_game)
    _save_game_list(_dedupe_games(next))
    ProjectSettings.set_setting(GAME_PATH_KEY, path)
    ProjectSettings.save()
    game_path.text = path
    _refresh_games()
    if not replaced:
        _offer_scrape_after_add(final_game)
    return true

func _merge_game_dictionary(existing: Dictionary, game: Dictionary) -> Dictionary:
    var merged := existing.duplicate(true)
    for key in game.keys():
        var value = game[key]
        if (key == "lastPlayed" or key == "playDurationSeconds") and int(value) == 0:
            continue
        if (key == "coverPath" or key == "developer" or key == "title") and String(value).is_empty():
            continue
        merged[key] = value
    return merged

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
    if OS.get_name() == "Web" and path.begins_with("/webgames/"):
        return true
    return DirAccess.dir_exists_absolute(path) or FileAccess.file_exists(path)

func _web_game_entry_available(game: Dictionary) -> bool:
    if OS.get_name() != "Web":
        return true
    var backend := String(game.get("webMountBackend", ""))
    if backend.is_empty() and not String(game.get("webMountBaseUrl", "")).is_empty():
        backend = "http"
    if backend.is_empty() and not String(game.get("webMountGameId", "")).is_empty():
        backend = "blob"
    if backend.is_empty() and String(game.get("path", "")).begins_with("/webgames/"):
        return false
    if backend == "http":
        var base_url := String(game.get("webMountBaseUrl", ""))
        if base_url.begins_with("/__aetherkiri/game/"):
            var manifest = _web_sync_get_json(base_url + "/manifest")
            return manifest is Dictionary and manifest.has("files")
        return true
    if backend != "blob":
        return true
    var game_id := String(game.get("webMountGameId", ""))
    if game_id.is_empty():
        return false
    var source := "(function(){if(typeof AetherKiriLocalGameAvailable==='function')return AetherKiriLocalGameAvailable(%s)?'1':'0';var g=typeof window!=='undefined'?window:globalThis;var s=g.AetherKiriLocalGameStore;return s&&s.games&&s.games[%s]?'1':'0';})()" % [
        JSON.stringify(game_id),
        JSON.stringify(game_id),
    ]
    return _web_eval_string(source) == "1"

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
    if not _mount_web_game(selected_game):
        return
    if not _ensure_android_game_path_access(path):
        return
    var played_game := _mark_game_played(path)
    if not played_game.is_empty():
        selected_game = played_game
    active_game_path = path
    active_game_started_msec = Time.get_ticks_msec()
    game_path.text = path
    _set_game_background(true)
    _apply_shell_runtime_settings()
    shell_root.visible = false
    viewport.visible = true
    viewport.move_to_front()
    game_view.visible = true
    game_view.move_to_front()
    runtime_menu_button.visible = true
    runtime_overlay_visible = false
    game_paused = false
    if runtime_overlay != null:
        runtime_overlay.visible = false
    if game_menu_dialog != null:
        game_menu_dialog.visible = false
    if debug_panel != null:
        debug_panel.visible = debug_panel_visible
    loading_panel.visible = true
    loading_panel.move_to_front()
    runtime_menu_button.move_to_front()
    perf.visible = show_perf_monitor
    restart_notice.visible = true
    _on_open_game()

func _finalize_active_game_session() -> void:
    if active_game_path.is_empty() or active_game_started_msec <= 0:
        active_game_path = ""
        active_game_started_msec = 0
        _apply_shell_runtime_settings()
        return
    var elapsed := int((Time.get_ticks_msec() - active_game_started_msec) / 1000)
    _add_play_duration(active_game_path, elapsed)
    active_game_path = ""
    active_game_started_msec = 0
    _apply_shell_runtime_settings()

func _ready() -> void:
    _apply_ui_font()
    DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_TRANSPARENT, false)
    _apply_initial_window_size()
    _apply_global_dpi_scale()
    get_viewport().transparent_bg = false
    RenderingServer.set_default_clear_color(COLOR_BG)
    var perf_interval_env := OS.get_environment("AETHERKIRI_PERF_LOG_INTERVAL")
    if not perf_interval_env.is_empty():
        perf_log_interval = maxf(0.05, perf_interval_env.to_float())
    frame_spike_ms = maxf(0.0, OS.get_environment("AETHERKIRI_FRAME_SPIKE_MS").to_float())
    verbose_render_log = OS.get_environment("AETHERKIRI_VERBOSE_RENDER_LOG") == "1"
    render_surface_max_size = _env_vector2i("AETHERKIRI_SURFACE_MAX_SIZE", RENDER_SURFACE_MAX_SIZE)
    frame_probe_enabled = _runtime_flag("AETHERKIRI_FRAME_PROBE")
    frame_probe_interval = maxf(0.05, _runtime_float("AETHERKIRI_FRAME_PROBE_INTERVAL", 1.0))

    var live_fps_log_path := OS.get_environment("AETHERKIRI_LIVE_FPS_LOG")
    if not live_fps_log_path.is_empty():
        perf_log_file = FileAccess.open(live_fps_log_path, FileAccess.WRITE)
        if perf_log_file != null:
            perf_log_file.store_line("live fps log started")
            perf_log_file.flush()

    selected_backend = _runtime_string(
        "AETHERKIRI_BACKEND",
        ProjectSettings.get_setting(SETTINGS_KEY, "Godot Native")
    )
    _load_shell_settings()
    if not selected_backend in BACKENDS:
        selected_backend = "Godot Native"

    _build_ui()
    _android_request_storage_permissions()

    if not _create_runtime_player():
        return

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
    viewport.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
    get_viewport().canvas_item_default_texture_filter = Viewport.DEFAULT_CANVAS_ITEM_TEXTURE_FILTER_NEAREST
    _apply_upscale_algorithm()

    _append_log("AetherKiri shell ready. Initializing engine...")
    call_deferred("_finish_ready_after_first_frame")

func _create_runtime_player() -> bool:
    if not ClassDB.class_exists("AetherKiriPlayer"):
        var message := "AetherKiri runtime extension class is unavailable."
        push_error(message)
        _append_log(message)
        _show_system_alert("运行时扩展加载失败：AetherKiriPlayer 不可用", "AetherKiri 错误")
        return false
    var instance: Object = ClassDB.instantiate("AetherKiriPlayer")
    if instance == null or not (instance is Node):
        var create_message := "AetherKiri runtime extension could not create AetherKiriPlayer."
        push_error(create_message)
        _append_log(create_message)
        _show_system_alert("运行时扩展加载失败：无法创建 AetherKiriPlayer", "AetherKiri 错误")
        return false
    player = instance
    add_child(instance as Node)
    return true

func _finish_ready_after_first_frame() -> void:
    await get_tree().process_frame

    var user_dir := OS.get_user_data_dir()
    var cache_dir := user_dir.path_join("cache")
    DirAccess.make_dir_recursive_absolute(cache_dir)
    if not player.initialize_engine(user_dir, cache_dir):
        render_errors += 1
        var init_error_message := "Engine init failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ]
        _append_log(init_error_message)
    else:
        _append_log("AetherKiri engine initialized.")

    _apply_backend(false)
    _apply_engine_options()
    _apply_shell_runtime_settings()
    _append_log("Debug CPU is a fallback backend and is not part of performance acceptance.")
    _write_probe_marker("ready")
    _refresh_games()
    call_deferred("_refresh_games_after_web_local_restore")
    call_deferred("_auto_start_web_dev_game")

    capture_after_open_path = _runtime_string("AETHERKIRI_CAPTURE_AFTER_OPEN")
    capture_after_open_delay_sec = maxf(
        0.0,
        _runtime_float("AETHERKIRI_CAPTURE_DELAY_SEC", 0.0)
    )
    auto_probe_clicks = _parse_click_points(_runtime_string("AETHERKIRI_AUTO_PROBE_CLICKS"))
    if _runtime_flag("AETHERKIRI_AUTO_OPEN"):
        call_deferred("_on_open_game")
    if not OS.get_environment("AETHERKIRI_CAPTURE_UI").is_empty():
        call_deferred("_capture_ui_after_ready")

func _refresh_games_after_web_local_restore() -> void:
    if OS.get_name() != "Web":
        return
    var deadline_msec := Time.get_ticks_msec() + 30 * 1000
    while Time.get_ticks_msec() < deadline_msec:
        var state := _web_local_game_restore_state()
        if bool(state.get("done", true)):
            _refresh_games()
            return
        await get_tree().create_timer(0.25).timeout
    _refresh_games()

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

func _apply_initial_window_size() -> void:
    if OS.get_name() == "iOS" or OS.get_name() == "Android":
        return
    var screen_size := DisplayServer.screen_get_size(DisplayServer.window_get_current_screen())
    if screen_size.x <= 0 or screen_size.y <= 0:
        return
    var requested_size := _env_vector2i("AETHERKIRI_WINDOW_SIZE", INITIAL_WINDOW_SIZE)
    var max_window := Vector2(
        float(screen_size.x) * 0.88,
        float(screen_size.y) * 0.82
    )
    var scale := minf(
        max_window.x / float(requested_size.x),
        max_window.y / float(requested_size.y)
    )
    scale = minf(scale, 1.0)
    var target_size := Vector2i(
        int(round(float(requested_size.x) * scale)),
        int(round(float(requested_size.y) * scale))
    )
    DisplayServer.window_set_size(target_size)
    DisplayServer.window_set_position((screen_size - target_size) / 2)

func _apply_global_dpi_scale() -> void:
    var scale_text := OS.get_environment("AETHERKIRI_UI_DPI_SCALE").strip_edges()
    var scale := DEFAULT_UI_DPI_SCALE
    if not scale_text.is_empty():
        scale = scale_text.to_float()
    scale = clampf(scale, 0.75, 2.0)
    var window := get_window()
    window.content_scale_factor = scale

func _process(delta: float) -> void:
    _fit_full_rects()
    var startup_state := cached_startup_state
    if game_running:
        _sync_player_surface_size(false)
        log_drain_accum += delta
        if log_drain_accum >= LOG_DRAIN_INTERVAL:
            log_drain_accum = 0.0
            _drain_logs()

        startup_poll_accum += delta
        if cached_startup_state == STARTUP_SUCCEEDED or startup_poll_accum >= STARTUP_POLL_INTERVAL:
            startup_poll_accum = 0.0
            cached_startup_state = player.get_startup_state()
            startup_state = cached_startup_state
        if startup_state == STARTUP_SUCCEEDED:
            restart_notice.text = "Paused" if game_paused else ""
            loading_panel.visible = false
            if not game_paused:
                var tick_start := Time.get_ticks_usec()
                var tick_result: int = int(player.tick(delta))
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
                    _finalize_active_game_session()
                else:
                    var update_start := Time.get_ticks_usec()
                    _update_frame()
                    var update_ms := float(Time.get_ticks_usec() - update_start) / 1000.0
                    _log_live_perf(delta, tick_ms, update_ms)
                    _log_frame_spike(delta, tick_ms, update_ms)
                    _log_frame_probe(delta)
        elif startup_state == STARTUP_FAILED:
            restart_notice.text = "Game startup failed."
            loading_panel.visible = false
            _set_game_background(false)
            shell_root.visible = true
            viewport.visible = false
            game_view.visible = false
            game_running = false
            _finalize_active_game_session()
            render_errors += 1
            var startup_error := "Startup failed: %s" % player.get_last_error()
            _append_log(startup_error)

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
        var renderer: String = selected_backend
        if game_running and startup_state == STARTUP_SUCCEEDED:
            renderer = String(player.get_renderer_info())
        var renderer_summary := _renderer_summary(renderer)
        if verbose_render_log and game_running and not renderer.is_empty() and renderer_summary != last_renderer_info_logged:
            last_renderer_info_logged = renderer_summary
            _append_log("Renderer info: %s" % renderer)
        var fallback := _renderer_fallback(renderer)
        var texture_backend: String = String(player.get_frame_texture_backend()) if game_running else "none"
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
        if debug_panel_visible:
            _update_debug_panel()
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

func _log_frame_probe(delta: float) -> void:
    if not frame_probe_enabled:
        return
    frame_probe_accum += delta
    if frame_probe_accum < frame_probe_interval:
        return
    frame_probe_accum = 0.0
    var frame: Dictionary = player.read_frame_rgba()
    var line := "frame_probe texture=%s size=%dx%d serial=%d stats=%s renderer=\"%s\" errors=%d" % [
        player.get_frame_texture_backend(),
        int(frame.get("width", 0)),
        int(frame.get("height", 0)),
        int(frame.get("frame_serial", 0)),
        JSON.stringify(_frame_stats(frame)),
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
    var result: int = int(player.set_render_backend(selected_backend))
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        var backend_error_message := "Renderer selection failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ]
        _append_log(backend_error_message)
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
    _sync_player_surface_size(true)
    cached_startup_state = STARTUP_RUNNING
    startup_poll_accum = STARTUP_POLL_INTERVAL

    var async_open := OS.get_environment("AETHERKIRI_SYNC_OPEN") != "1"
    var result: int = int(player.open_game(path, async_open))
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        cached_startup_state = STARTUP_FAILED
        _write_probe_marker("open_game_failed result=%s error=%s" % [
            player.get_last_result(),
            player.get_last_error(),
        ])
        var launch_error_message := "Game launch failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ]
        _append_log(launch_error_message)
        loading_panel.visible = false
        _set_game_background(false)
        shell_root.visible = true
        viewport.visible = false
        game_view.visible = false
        game_running = false
        _finalize_active_game_session()
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

func _desired_render_surface_size() -> Vector2i:
    var window_size := DisplayServer.window_get_size()
    if window_size.x < 1 or window_size.y < 1:
        return RENDER_SURFACE_SIZE
    var pixel_scale := _surface_pixel_scale()
    var target_pixel_size := Vector2(
        float(window_size.x) * pixel_scale,
        float(window_size.y) * pixel_scale
    )
    var scale := minf(
        target_pixel_size.x / float(RENDER_SURFACE_SIZE.x),
        target_pixel_size.y / float(RENDER_SURFACE_SIZE.y)
    )
    if scale <= 0.0:
        return RENDER_SURFACE_SIZE
    scale = minf(
        scale,
        minf(
            float(render_surface_max_size.x) / float(RENDER_SURFACE_SIZE.x),
            float(render_surface_max_size.y) / float(RENDER_SURFACE_SIZE.y)
        )
    )
    return Vector2i(
        maxi(1, int(round(float(RENDER_SURFACE_SIZE.x) * scale))),
        maxi(1, int(round(float(RENDER_SURFACE_SIZE.y) * scale)))
    )

func _surface_pixel_scale() -> float:
    var env_scale := OS.get_environment("AETHERKIRI_SURFACE_PIXEL_SCALE").strip_edges()
    if not env_scale.is_empty():
        return clampf(env_scale.to_float(), 0.5, 4.0)
    if OS.get_name() == "macOS":
        var screen := DisplayServer.window_get_current_screen()
        return clampf(DisplayServer.screen_get_scale(screen), 1.0, 4.0)
    return 1.0

func _env_vector2i(key: String, fallback: Vector2i) -> Vector2i:
    var value := OS.get_environment(key).strip_edges().to_lower()
    if value.is_empty():
        return fallback
    value = value.replace("x", ",")
    var parts := value.split(",", false)
    if parts.size() != 2:
        return fallback
    var width := int(parts[0])
    var height := int(parts[1])
    if width <= 0 or height <= 0:
        return fallback
    return Vector2i(width, height)

func _sync_player_surface_size(force: bool) -> void:
    if player == null:
        return
    var target_size := _desired_render_surface_size()
    if not force and target_size == current_surface_size:
        return
    var result: int = int(player.set_surface_size(target_size.x, target_size.y))
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        var surface_error_message := "Surface resize failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ]
        _append_log(surface_error_message)
        return
    if current_surface_size != target_size:
        last_texture_size = Vector2i.ZERO
        var window_size := DisplayServer.window_get_size()
        var screen := DisplayServer.window_get_current_screen()
        var line := "surface_resize window=%dx%d screen_scale=%.2f target=%dx%d max=%dx%d" % [
            window_size.x,
            window_size.y,
            DisplayServer.screen_get_scale(screen),
            target_size.x,
            target_size.y,
            render_surface_max_size.x,
            render_surface_max_size.y,
        ]
        print(line)
        if perf_log_file != null:
            perf_log_file.store_line(line)
            perf_log_file.flush()
    current_surface_size = target_size

func _drain_logs() -> void:
    var logs: String = String(player.drain_startup_logs())
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
        _layout_game_viewport(get_viewport_rect().size)
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
    await _auto_probe_wait_frames(_runtime_int("AETHERKIRI_AUTO_PROBE_WARMUP_FRAMES", 180))
    await _save_auto_probe_step(0, "startup")
    var step := 1
    for pos in auto_probe_clicks:
        _send_probe_click(pos)
        await _auto_probe_wait_frames(_runtime_int("AETHERKIRI_AUTO_PROBE_AFTER_CLICK_FRAMES", 180))
        await _save_auto_probe_step(step, "click_%d_%d" % [int(pos.x), int(pos.y)])
        step += 1
    auto_probe_done = true
    auto_probe_running = false
    _write_probe_marker("auto_probe_done steps=%d renderer=%s" % [
        step,
        player.get_renderer_info(),
    ])
    if _runtime_flag("AETHERKIRI_QUIT_AFTER_AUTO_PROBE"):
        get_tree().quit(0)

func _auto_probe_wait_frames(frames: int) -> void:
    for i in range(max(1, frames)):
        await get_tree().process_frame

func _save_auto_probe_step(index: int, label: String) -> void:
    await get_tree().process_frame
    await get_tree().process_frame
    var frame: Dictionary = player.read_frame_rgba()
    var frame_stats := _frame_stats(frame)
    var image := get_viewport().get_texture().get_image()
    var screenshot_stats := _image_stats(image)
    var path := _default_output_path("aetherkiri-auto-step-%02d-%s.png" % [index, label])
    image.save_png(path)
    var line := "auto_step index=%d label=%s output=%s texture=%s frame=%dx%d serial=%d frame_stats=%s screenshot_stats=%s renderer=\"%s\"" % [
        index,
        label,
        path,
        player.get_frame_texture_backend(),
        int(frame.get("width", 0)),
        int(frame.get("height", 0)),
        int(frame.get("frame_serial", 0)),
        JSON.stringify(frame_stats),
        JSON.stringify(screenshot_stats),
        player.get_renderer_info(),
    ]
    _write_probe_marker(line)
    print(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

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
        float(_runtime_int("AETHERKIRI_AUTO_PROBE_COORD_W", 1600)),
        float(_runtime_int("AETHERKIRI_AUTO_PROBE_COORD_H", 900))
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

func _runtime_string(name: String, fallback: String = "") -> String:
    var value := OS.get_environment(name)
    if not value.is_empty():
        return value
    if OS.get_name() != "Web":
        return fallback
    var aliases: Array[String] = [name, name.to_lower()]
    if name.begins_with("AETHERKIRI_"):
        aliases.append(name.substr("AETHERKIRI_".length()).to_lower())
    var source := "(function(names){var p=new URLSearchParams(window.location.search);for(var i=0;i<names.length;i++){if(p.has(names[i]))return p.get(names[i])||'';}return '';})(" + JSON.stringify(aliases) + ")"
    value = _web_eval_string(source)
    return fallback if value.is_empty() else value

func _runtime_flag(name: String, fallback: bool = false) -> bool:
    var value := _runtime_string(name)
    if value.is_empty():
        return fallback
    value = value.strip_edges().to_lower()
    return value == "1" or value == "true" or value == "yes" or value == "on"

func _runtime_float(name: String, fallback: float) -> float:
    var value := _runtime_string(name)
    if value.is_empty():
        return fallback
    return value.to_float()

func _runtime_int(name: String, fallback: int) -> int:
    var value := _runtime_string(name)
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
        if _runtime_pointer_event_on_ui(event):
            get_viewport().set_input_as_handled()
            return
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
        if _is_touch_platform() and mouse_button.button_index != MOUSE_BUTTON_WHEEL_UP and mouse_button.button_index != MOUSE_BUTTON_WHEEL_DOWN:
            return Time.get_ticks_msec() < suppress_mouse_until_msec
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
        if _is_touch_platform():
            return Time.get_ticks_msec() < suppress_mouse_until_msec
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
        suppress_mouse_until_msec = Time.get_ticks_msec() + TOUCH_MOUSE_SUPPRESS_MS
        if _handle_virtual_cursor_touch(touch):
            return true
        var mapped := _map_viewport_point(touch.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return false
        var event_type := POINTER_DOWN if touch.pressed else POINTER_UP
        if event_type == POINTER_DOWN:
            player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
        player.send_pointer_event(event_type, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
        return true
    elif event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        suppress_mouse_until_msec = Time.get_ticks_msec() + TOUCH_MOUSE_SUPPRESS_MS
        if _handle_virtual_cursor_drag(drag):
            return true
        var mapped := _map_viewport_point(drag.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return false
        var rel := _map_viewport_delta(drag.relative)
        player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, rel.x, rel.y, 0)
        return true
    return false

func _handle_virtual_cursor_touch(touch: InputEventScreenTouch) -> bool:
    if not _is_touch_platform() or not virtual_cursor_enabled:
        return false
    if touch.pressed:
        if not virtual_cursor_initialized:
            virtual_cursor_position = _clamp_virtual_cursor_position(touch.position)
            virtual_cursor_initialized = true
        virtual_cursor_active = true
        virtual_cursor_dragged = false
        _send_virtual_cursor_move(Vector2.ZERO)
        _layout_virtual_cursor()
        return true

    if not virtual_cursor_active:
        return true
    virtual_cursor_active = false
    if not virtual_cursor_dragged:
        _send_virtual_cursor_click(0)
    virtual_cursor_dragged = false
    _layout_virtual_cursor()
    return true

func _handle_virtual_cursor_drag(drag: InputEventScreenDrag) -> bool:
    if not _is_touch_platform() or not virtual_cursor_enabled:
        return false
    if not virtual_cursor_active:
        return true
    var previous := virtual_cursor_position
    virtual_cursor_position = _clamp_virtual_cursor_position(virtual_cursor_position + drag.relative)
    var applied_delta := virtual_cursor_position - previous
    if applied_delta.length_squared() > 1.0:
        virtual_cursor_dragged = true
    _send_virtual_cursor_move(applied_delta)
    _layout_virtual_cursor()
    return true

func _clamp_virtual_cursor_position(position_to_clamp: Vector2) -> Vector2:
    var rect := viewport.get_global_rect()
    if rect.size.x <= 0.0 or rect.size.y <= 0.0:
        return position_to_clamp
    return Vector2(
        clampf(position_to_clamp.x, rect.position.x, rect.position.x + rect.size.x),
        clampf(position_to_clamp.y, rect.position.y, rect.position.y + rect.size.y)
    )

func _send_virtual_cursor_move(global_delta: Vector2) -> void:
    var mapped := _map_viewport_point(virtual_cursor_position)
    if mapped.x < 0.0 or mapped.y < 0.0:
        return
    var rel := _map_viewport_delta(global_delta)
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, rel.x, rel.y, 0)

func _send_virtual_cursor_click(button: int) -> void:
    var mapped := _map_viewport_point(virtual_cursor_position)
    if mapped.x < 0.0 or mapped.y < 0.0:
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, button)
    _pump_pointer_event_tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, 0, mapped.x, mapped.y, 0.0, 0.0, button)
    _pump_pointer_event_tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_UP, 0, mapped.x, mapped.y, 0.0, 0.0, button)

func _is_touch_platform() -> bool:
    var platform := OS.get_name()
    return platform == "iOS" or platform == "Android"

func _map_viewport_point(pos: Vector2) -> Vector2:
    if viewport.texture == null:
        return pos
    var local_pos := pos - viewport.get_global_rect().position
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
    var inside: Vector2 = local_pos - offset
    if inside.x < 0.0 or inside.y < 0.0 or inside.x > drawn_size.x or inside.y > drawn_size.y:
        return Vector2(-1.0, -1.0)
    return inside / scale

func _pump_pointer_event_tick(delta: float) -> void:
    if not game_running:
        return
    if player.get_startup_state() != STARTUP_SUCCEEDED:
        return
    var result: int = int(player.tick(delta))
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
    _maybe_show_log_alert(line)
    log_lines.append(line)
    while log_lines.size() > MAX_LOG_LINES:
        log_lines.remove_at(0)
    log_view.text = "\n".join(log_lines)
    call_deferred("_scroll_log_to_bottom")

func _scroll_log_to_bottom() -> void:
    if log_view == null:
        return
    log_view.scroll_vertical = max(0, log_view.get_line_count())
