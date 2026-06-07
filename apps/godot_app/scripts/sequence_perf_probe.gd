extends SceneTree

const ProbeConfig = preload("res://scripts/probe_config.gd")
const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3
const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3

func _initialize() -> void:
    var config := ProbeConfig.load()
    root.size = ProbeConfig.window_size(config, Vector2i(1280, 720))

    var rect := TextureRect.new()
    rect.set_anchors_preset(Control.PRESET_FULL_RECT)
    rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    rect.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
    root.add_child(rect)

    var player = ClassDB.instantiate("AetherKiriPlayer")
    root.add_child(player as Node)

    var user_dir := OS.get_user_data_dir()
    var cache_dir := user_dir.path_join("cache")
    DirAccess.make_dir_recursive_absolute(cache_dir)
    if not player.initialize_engine(user_dir, cache_dir):
        printerr("initialize_engine failed: %s" % player.get_last_error())
        quit(1)
        return

    player.set_render_backend(ProbeConfig.backend(config, "AETHERKIRI_PROBE_BACKEND"))
    var surface_size: Vector2i = ProbeConfig.surface_size(config)
    player.set_surface_size(surface_size.x, surface_size.y)

    var game_path: String = ProbeConfig.require_game_path(config)
    if game_path.is_empty():
        quit(2)
        return
    var result: int = player.open_game(game_path, true)
    if result != 0:
        printerr("open_game failed: %s" % player.get_last_error())
        quit(1)
        return

    for i in range(ProbeConfig.int_value(config, "startup_timeout_frames", 900)):
        var state: int = player.get_startup_state()
        if state == STARTUP_SUCCEEDED:
            break
        if state == STARTUP_FAILED:
            printerr("startup failed: %s" % player.get_last_error())
            quit(1)
            return
        await process_frame

    for i in range(ProbeConfig.int_value(config, "warmup_frames", _env_int("AETHERKIRI_PROBE_WARMUP_FRAMES", 180))):
        _tick_and_update(player, rect)
        await process_frame

    for click in ProbeConfig.clicks(config):
        var pos := ProbeConfig.click_position(click)
        _send_click(player, pos)
        var after_frames := int(click.get("after_frames", ProbeConfig.int_value(config, "after_click_frames", _env_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 120))))
        for i in range(after_frames):
            _tick_and_update(player, rect)
            await process_frame

    var measured_frames: int = ProbeConfig.int_value(config, "measure_frames", _env_int("AETHERKIRI_PROBE_MEASURE_FRAMES", 180))
    var start_ticks: int = Time.get_ticks_usec()
    for i in range(measured_frames):
        _tick_and_update(player, rect)
        await process_frame
    var elapsed_sec: float = float(Time.get_ticks_usec() - start_ticks) / 1000000.0
    var fps: float = float(measured_frames) / max(0.0001, elapsed_sec)

    root.get_viewport().get_texture().get_image().save_png(
        "/tmp/aetherkiri-sequence-after.png")
    print("sequence probe fps=%.2f texture_backend=%s renderer=\"%s\" screenshot=/tmp/aetherkiri-sequence-after.png" % [
        fps,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
    ])

    if OS.get_environment("AETHERKIRI_PROBE_SKIP_DESTROY") != "1":
        player.destroy_engine()
    quit(0)

func _tick_and_update(player, rect: TextureRect) -> void:
    if player.tick(1.0 / 60.0) != 0:
        return
    var texture: Texture2D = player.update_frame_texture()
    if texture != null:
        rect.texture = texture
        rect.queue_redraw()

func _send_click(player, pos: Vector2) -> void:
    player.send_pointer_event(POINTER_MOVE, 0, pos.x, pos.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, 0, pos.x, pos.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_UP, 0, pos.x, pos.y, 0.0, 0.0, 0)

func _parse_clicks(spec: String) -> Array[Vector2]:
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
