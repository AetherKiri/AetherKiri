extends Control

const BACKENDS := ["Godot Native", "GPU Bridge", "Debug CPU"]
const SETTINGS_KEY := "aether_kiri/render_backend"
const GAME_PATH_KEY := "aether_kiri/game_path"

const ENGINE_RESULT_OK := 0
const STARTUP_IDLE := 0
const STARTUP_RUNNING := 1
const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3

const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3
const POINTER_SCROLL := 4

@onready var backend: OptionButton = $Root/Toolbar/Backend
@onready var game_path: LineEdit = $Root/Toolbar/GamePath
@onready var restart_notice: Label = $Root/Toolbar/RestartNotice
@onready var viewport: TextureRect = $Root/ViewportPanel
@onready var perf: Label = $Root/Perf
@onready var log_view: TextEdit = $Root/Log

var player: AetherKiriPlayer
var selected_backend := "Godot Native"
var game_running := false
var render_errors := 0
var last_renderer_info_logged := ""
var last_texture_size := Vector2i.ZERO
var capture_after_open_path := ""
var capture_after_open_done := false
var log_drain_accum := 0.0
var perf_accum := 0.0
var perf_log_accum := 0.0
var state_log_accum := 0.0
var perf_log_file: FileAccess
var log_lines: PackedStringArray = []
const LOG_DRAIN_INTERVAL := 0.25
const PERF_UPDATE_INTERVAL := 0.25
const PERF_LOG_INTERVAL := 2.0
const MAX_LOG_LINES := 240
const RENDER_SURFACE_SIZE := Vector2i(1280, 720)

func _ready() -> void:
    perf_log_file = FileAccess.open("/tmp/aetherkiri-live-fps.log", FileAccess.WRITE)
    if perf_log_file != null:
        perf_log_file.store_line("live fps log started")
        perf_log_file.flush()

    player = AetherKiriPlayer.new()
    add_child(player)

    for item in BACKENDS:
        backend.add_item(item)

    selected_backend = OS.get_environment("AETHERKIRI_BACKEND")
    if selected_backend.is_empty():
        selected_backend = ProjectSettings.get_setting(SETTINGS_KEY, "Godot Native")
    if not selected_backend in BACKENDS:
        selected_backend = "Godot Native"
    var index := BACKENDS.find(selected_backend)
    backend.select(max(index, 0))

    var configured_game_path := OS.get_environment("AETHERKIRI_GAME_PATH")
    if configured_game_path.is_empty():
        configured_game_path = ProjectSettings.get_setting(
            GAME_PATH_KEY, "/Users/liuyu/gal/奶牛5 KR3.7S"
        )
    game_path.text = configured_game_path

    backend.item_selected.connect(_on_backend_selected)
    $Root/Toolbar/OpenButton.pressed.connect(_on_open_game)
    viewport.gui_input.connect(_on_viewport_input)
    viewport.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    viewport.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED

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
    _append_log("Debug CPU is a fallback backend and is not part of performance acceptance.")

    capture_after_open_path = OS.get_environment("AETHERKIRI_CAPTURE_AFTER_OPEN")
    if OS.get_environment("AETHERKIRI_AUTO_OPEN") == "1":
        call_deferred("_on_open_game")

func _process(delta: float) -> void:
    var startup_state := STARTUP_IDLE
    if game_running:
        log_drain_accum += delta
        if log_drain_accum >= LOG_DRAIN_INTERVAL:
            log_drain_accum = 0.0
            _drain_logs()

        startup_state = player.get_startup_state()
        if startup_state == STARTUP_SUCCEEDED:
            restart_notice.text = ""
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
        elif startup_state == STARTUP_FAILED:
            restart_notice.text = "Game startup failed."
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
        if game_running and not renderer.is_empty() and renderer != last_renderer_info_logged:
            last_renderer_info_logged = renderer
            _append_log("Renderer info: %s" % renderer)
        var fallback := _renderer_fallback(renderer)
        var texture_backend := player.get_frame_texture_backend() if game_running else "none"
        perf.text = "Backend: %s | FPS: %d | Frame: %.2f ms | Texture: %s | Size: %dx%d | Fallback: %s | Errors: %d" % [
            renderer if not renderer.is_empty() else selected_backend,
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
    if perf_log_accum < PERF_LOG_INTERVAL:
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

func _notification(what: int) -> void:
    if player == null:
        return
    if what == NOTIFICATION_WM_CLOSE_REQUEST:
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

func _on_open_game() -> void:
    var path := game_path.text.strip_edges()
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
        _append_log("Game launch failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ])
        return

    game_running = true
    last_texture_size = Vector2i.ZERO
    capture_after_open_done = false
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
        if not capture_after_open_path.is_empty() and not capture_after_open_done:
            var frame := player.read_frame_rgba()
            var frame_stats := _frame_stats(frame)
            if int(frame_stats.get("visible", 0)) > 0:
                capture_after_open_done = true
                call_deferred("_capture_main_view", frame_stats)

func _capture_main_view(frame_stats: Dictionary) -> void:
    await get_tree().process_frame
    await get_tree().process_frame
    var image := get_viewport().get_texture().get_image()
    var screenshot_stats := _image_stats(image)
    var output_path := capture_after_open_path
    if output_path.is_empty():
        output_path = OS.get_user_data_dir().path_join("main_render_probe.png")
    image.save_png(output_path)
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

func _on_viewport_input(event: InputEvent) -> void:
    if not game_running:
        return

    if event is InputEventMouseButton:
        var mouse_button := event as InputEventMouseButton
        var mapped := _map_viewport_point(mouse_button.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return
        var event_type := POINTER_DOWN if mouse_button.pressed else POINTER_UP
        if mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP or mouse_button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
            event_type = POINTER_SCROLL
        var button := _map_mouse_button(mouse_button.button_index)
        player.send_pointer_event(
            event_type,
            0,
            mapped.x,
            mapped.y,
            0.0,
            -1.0 if mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0,
            button
        )
    elif event is InputEventMouseMotion:
        var motion := event as InputEventMouseMotion
        var mapped := _map_viewport_point(motion.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return
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
    log_lines.append(line)
    while log_lines.size() > MAX_LOG_LINES:
        log_lines.remove_at(0)
    log_view.text = "\n".join(log_lines)
    log_view.scroll_vertical = log_view.get_line_count()
