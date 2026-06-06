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

    var game_path: String = ProbeConfig.require_game_path(config)
    if game_path.is_empty():
        quit(2)
        return

    var rect := TextureRect.new()
    rect.set_anchors_preset(Control.PRESET_FULL_RECT)
    rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    rect.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
    root.add_child(rect)

    var player := AetherKiriPlayer.new()
    root.add_child(player)

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

    var result: int = player.open_game(game_path, true)
    if result != 0:
        printerr("open_game failed: %s" % player.get_last_error())
        player.destroy_engine()
        quit(1)
        return

    for i in range(ProbeConfig.int_value(config, "startup_timeout_frames", 900)):
        var state: int = player.get_startup_state()
        if state == STARTUP_SUCCEEDED:
            break
        if state == STARTUP_FAILED:
            printerr("startup failed: %s" % player.get_last_error())
            player.destroy_engine()
            quit(1)
            return
        if i == 899:
            printerr("startup timed out")
            player.destroy_engine()
            quit(1)
            return
        await process_frame

    for i in range(ProbeConfig.int_value(config, "warmup_frames", 180)):
        _tick_and_update(player, rect)
        await process_frame

    var before := root.get_viewport().get_texture().get_image()
    before.save_png("/tmp/aetherkiri-before-click.png")

    var measured_frames: int = ProbeConfig.int_value(config, "measure_frames", _env_int("AETHERKIRI_PROBE_MEASURE_FRAMES", 180))
    var start_ticks: int = Time.get_ticks_usec()
    for i in range(measured_frames):
        _tick_and_update(player, rect)
        await process_frame
    var elapsed_sec: float = float(Time.get_ticks_usec() - start_ticks) / 1000000.0
    var fps: float = float(measured_frames) / max(0.0001, elapsed_sec)

    var clicks := ProbeConfig.clicks(config)
    var click_pos: Vector2 = ProbeConfig.click_position(clicks[0]) if not clicks.is_empty() else ProbeConfig.perf_click(config, "click", Vector2(
        _env_float("AETHERKIRI_PROBE_CLICK_X", 450.0),
        _env_float("AETHERKIRI_PROBE_CLICK_Y", 880.0)
    ))
    _send_click(player, click_pos)
    var post_click_frames: int = int(clicks[0].get("after_frames", 180)) if not clicks.is_empty() else ProbeConfig.nested_int(config, "perf_input", "post_click_frames", _env_int("AETHERKIRI_PROBE_POST_CLICK_FRAMES", 180))
    for i in range(post_click_frames):
        _tick_and_update(player, rect)
        await process_frame

    var has_second_click := clicks.size() > 1 or OS.get_environment("AETHERKIRI_PROBE_SECOND_CLICK") == "1"
    if has_second_click:
        var second_click_pos := ProbeConfig.click_position(clicks[1]) if clicks.size() > 1 else ProbeConfig.perf_click(config, "second_click", Vector2(
            _env_float("AETHERKIRI_PROBE_SECOND_CLICK_X", 1350.0),
            _env_float("AETHERKIRI_PROBE_SECOND_CLICK_Y", 240.0)
        ))
        _send_click(player, second_click_pos)
        var second_post_click_frames: int = int(clicks[1].get("after_frames", 600)) if clicks.size() > 1 else ProbeConfig.nested_int(config, "perf_input", "second_post_click_frames", _env_int("AETHERKIRI_PROBE_SECOND_POST_CLICK_FRAMES", 600))
        for i in range(second_post_click_frames):
            _tick_and_update(player, rect)
            await process_frame

    var after := root.get_viewport().get_texture().get_image()
    after.save_png("/tmp/aetherkiri-after-click.png")
    var diff: float = _image_diff_score(before, after)

    print("perf_input probe fps=%.2f texture_backend=%s renderer=\"%s\" click_diff=%.5f before=/tmp/aetherkiri-before-click.png after=/tmp/aetherkiri-after-click.png" % [
        fps,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
        diff,
    ])

    if OS.get_environment("AETHERKIRI_PROBE_SKIP_DESTROY") != "1":
        player.destroy_engine()
    quit(0 if diff > 0.01 else 2)

func _env_int(name: String, fallback: int) -> int:
    var value := OS.get_environment(name)
    if value.is_empty():
        return fallback
    return int(value)

func _env_float(name: String, fallback: float) -> float:
    var value := OS.get_environment(name)
    if value.is_empty():
        return fallback
    return float(value)

func _tick_and_update(player: AetherKiriPlayer, rect: TextureRect) -> void:
    var result: int = player.tick(1.0 / 60.0)
    if result != 0:
        return
    var texture: Texture2D = player.update_frame_texture()
    if texture != null:
        rect.texture = texture

func _send_click(player: AetherKiriPlayer, pos: Vector2) -> void:
    player.send_pointer_event(POINTER_MOVE, 0, pos.x, pos.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, 0, pos.x, pos.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_UP, 0, pos.x, pos.y, 0.0, 0.0, 0)

func _image_diff_score(a: Image, b: Image) -> float:
    var width: int = min(a.get_width(), b.get_width())
    var height: int = min(a.get_height(), b.get_height())
    if width <= 0 or height <= 0:
        return 0.0
    var step_x: int = max(1, width / 160)
    var step_y: int = max(1, height / 90)
    var total: float = 0.0
    var sampled: int = 0
    for y in range(0, height, step_y):
        for x in range(0, width, step_x):
            var ca: Color = a.get_pixel(x, y)
            var cb: Color = b.get_pixel(x, y)
            total += abs(ca.r - cb.r) + abs(ca.g - cb.g) + abs(ca.b - cb.b)
            sampled += 1
    return total / max(1, sampled)
