extends SceneTree

const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3
const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3

var player: AetherKiriPlayer
var rect: TextureRect

func _initialize() -> void:
    root.size = Vector2i(_env_int("AETHERKIRI_PROBE_WINDOW_W", 1600),
                         _env_int("AETHERKIRI_PROBE_WINDOW_H", 900))

    rect = TextureRect.new()
    rect.set_anchors_preset(Control.PRESET_FULL_RECT)
    rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    root.add_child(rect)

    player = AetherKiriPlayer.new()
    root.add_child(player)

    var user_dir := OS.get_user_data_dir()
    var cache_dir := user_dir.path_join("cache")
    DirAccess.make_dir_recursive_absolute(cache_dir)
    if not player.initialize_engine(user_dir, cache_dir):
        printerr("initialize_engine failed: %s" % player.get_last_error())
        quit(1)
        return

    var backend := OS.get_environment("AETHERKIRI_PROBE_BACKEND")
    if backend.is_empty():
        backend = "Godot Native"
    player.set_render_backend(backend)
    player.set_surface_size(1280, 720)

    var game_path := OS.get_environment("AETHERKIRI_SMOKE_GAME")
    if game_path.is_empty():
        game_path = "/Users/liuyu/gal/奶牛5 KR3.7S"
    var result: int = player.open_game(game_path, true)
    if result != 0:
        printerr("open_game failed: %s" % player.get_last_error())
        quit(1)
        return

    if not await _wait_startup():
        quit(1)
        return

    await _advance(_env_int("AETHERKIRI_PROBE_WARMUP_FRAMES", 180))
    await _save_step(0, "startup")

    var step := 1
    for pos in _parse_clicks(OS.get_environment("AETHERKIRI_PROBE_CLICKS")):
        _send_window_click(pos)
        await _advance(_env_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 180))
        await _save_step(step, "click_%d_%d" % [int(pos.x), int(pos.y)])
        step += 1

    var measured_frames: int = _env_int("AETHERKIRI_PROBE_MEASURE_FRAMES", 120)
    var start_ticks: int = Time.get_ticks_usec()
    await _advance(measured_frames)
    var fps: float = float(measured_frames) / max(0.0001, float(Time.get_ticks_usec() - start_ticks) / 1000000.0)
    print("step probe fps=%.2f texture_backend=%s renderer=\"%s\" steps=%d output=/tmp/aetherkiri-step-*.png" % [
        fps,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
        step,
    ])

    if OS.get_environment("AETHERKIRI_PROBE_SKIP_DESTROY") != "1":
        await _destroy_player()
    quit(0)

func _destroy_player() -> void:
    rect.texture = null
    await process_frame
    player.release_frame_texture()
    player.destroy_engine()

func _wait_startup() -> bool:
    for i in range(900):
        var state: int = player.get_startup_state()
        if state == STARTUP_SUCCEEDED:
            return true
        if state == STARTUP_FAILED:
            printerr("startup failed: %s" % player.get_last_error())
            return false
        await process_frame
    printerr("startup timed out")
    return false

func _advance(frames: int) -> void:
    for i in range(frames):
        if player.tick(1.0 / 60.0) == 0:
            var texture: Texture2D = player.update_frame_texture()
            if texture != null:
                rect.texture = texture
                rect.queue_redraw()
        await process_frame

func _save_step(index: int, label: String) -> void:
    await process_frame
    await process_frame
    var image := root.get_viewport().get_texture().get_image()
    var path := "/tmp/aetherkiri-step-%02d-%s.png" % [index, label]
    image.save_png(path)
    print("step %02d label=%s texture_backend=%s renderer=\"%s\" screenshot=%s stats=%s" % [
        index,
        label,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
        path,
        JSON.stringify(_image_stats(image)),
    ])

func _send_window_click(window_pos: Vector2) -> void:
    var mapped := _map_window_point(window_pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip click outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_UP, 0, mapped.x, mapped.y, 0.0, 0.0, 0)

func _map_window_point(pos: Vector2) -> Vector2:
    if rect.texture == null:
        return pos
    var tex_size := Vector2(max(1.0, float(rect.texture.get_width())),
                            max(1.0, float(rect.texture.get_height())))
    var panel_size := Vector2(_env_int("AETHERKIRI_PROBE_COORD_W", 1600),
                              _env_int("AETHERKIRI_PROBE_COORD_H", 900))
    var scale: float = min(panel_size.x / tex_size.x, panel_size.y / tex_size.y)
    if scale <= 0.0:
        return Vector2(-1.0, -1.0)
    var drawn_size := tex_size * scale
    var offset := (panel_size - drawn_size) * 0.5
    var inside := pos - offset
    if inside.x < 0.0 or inside.y < 0.0 or inside.x > drawn_size.x or inside.y > drawn_size.y:
        return Vector2(-1.0, -1.0)
    return inside / scale

func _parse_clicks(spec: String) -> Array[Vector2]:
    var clicks: Array[Vector2] = []
    if spec.is_empty():
        return clicks
    for item in spec.split(";"):
        var parts := item.split(",")
        if parts.size() == 2:
            clicks.push_back(Vector2(float(parts[0]), float(parts[1])))
    return clicks

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

func _env_int(name: String, fallback: int) -> int:
    var value := OS.get_environment(name)
    if value.is_empty():
        return fallback
    return int(value)
