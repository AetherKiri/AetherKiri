extends SceneTree

const ProbeConfig = preload("res://scripts/probe_config.gd")
const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3
const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3

var player
var rect: TextureRect
var test_config := {}

func _initialize() -> void:
    test_config = ProbeConfig.load()
    root.size = ProbeConfig.window_size(test_config, Vector2i(
        _env_int("AETHERKIRI_PROBE_WINDOW_W", 1600),
        _env_int("AETHERKIRI_PROBE_WINDOW_H", 900)
    ))

    rect = TextureRect.new()
    rect.set_anchors_preset(Control.PRESET_FULL_RECT)
    rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    rect.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
    root.add_child(rect)

    player = ClassDB.instantiate("AetherKiriPlayer")
    root.add_child(player as Node)

    var user_dir := OS.get_user_data_dir()
    var cache_dir := user_dir.path_join("cache")
    DirAccess.make_dir_recursive_absolute(cache_dir)
    if not player.initialize_engine(user_dir, cache_dir):
        printerr("initialize_engine failed: %s" % player.get_last_error())
        quit(1)
        return

    var backend: String = ProbeConfig.backend(test_config, "AETHERKIRI_PROBE_BACKEND")
    player.set_render_backend(backend)
    var fps_limit := ProbeConfig.int_value(test_config, "fps_limit", _env_int("AETHERKIRI_PROBE_FPS_LIMIT", 0))
    player.set_engine_option("fps_limit", str(maxi(0, fps_limit)))
    if ProbeConfig.bool_value(test_config, "plugin_trace", false):
        player.set_engine_option("plugin_trace", "1")
    if ProbeConfig.bool_value(test_config, "export_scripts", false):
        player.set_engine_option("export_scripts", "1")
    var plugin_load_mode := ProbeConfig.string_value(test_config, "plugin_load_mode", "")
    if plugin_load_mode in ["krkrsdl3", "aether_all"]:
        player.set_engine_option("plugin_load_mode", plugin_load_mode)
    var surface_size: Vector2i = ProbeConfig.surface_size(test_config)
    player.set_surface_size(surface_size.x, surface_size.y)

    var game_path: String = ProbeConfig.require_game_path(test_config)
    if game_path.is_empty():
        quit(2)
        return
    var result: int = player.open_game(game_path, true)
    if result != 0:
        printerr("open_game failed: %s" % player.get_last_error())
        quit(1)
        return

    if not await _wait_startup():
        quit(1)
        return

    await _advance(ProbeConfig.int_value(test_config, "warmup_frames", _env_int("AETHERKIRI_PROBE_WARMUP_FRAMES", 180)))
    await _save_step(0, "startup")

    var step := 1
    if test_config.has("actions") and test_config["actions"] is Array:
        step = await _run_actions(step)
    else:
        step = await _run_legacy_steps(step)

    var measured_frames: int = ProbeConfig.int_value(test_config, "measure_frames", _env_int("AETHERKIRI_PROBE_MEASURE_FRAMES", 120))
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
    for i in range(ProbeConfig.int_value(test_config, "startup_timeout_frames", 900)):
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
    var image := _capture_frame_image()
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

func _capture_frame_image() -> Image:
    var texture := root.get_viewport().get_texture()
    if texture != null:
        var viewport_image := texture.get_image()
        if viewport_image != null and viewport_image.get_width() > 0 and viewport_image.get_height() > 0:
            if int(_image_stats(viewport_image).get("visible", 0)) > 0:
                return viewport_image

    if rect.texture != null:
        var rect_image := rect.texture.get_image()
        if rect_image != null and rect_image.get_width() > 0 and rect_image.get_height() > 0:
            if int(_image_stats(rect_image).get("visible", 0)) > 0:
                return rect_image

    var frame: Dictionary = player.read_frame_rgba()
    var data: PackedByteArray = frame.get("rgba", PackedByteArray())
    var width := int(frame.get("width", 0))
    var height := int(frame.get("height", 0))
    if width > 0 and height > 0 and data.size() >= width * height * 4:
        var frame_image := Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, data)
        if int(_image_stats(frame_image).get("visible", 0)) > 0:
            return frame_image

    return Image.create(1, 1, false, Image.FORMAT_RGBA8)

func _run_legacy_steps(step: int) -> int:
    for click in ProbeConfig.clicks(test_config):
        var pos := ProbeConfig.click_position(click)
        _send_window_click(pos)
        await _advance(int(click.get("after_frames", ProbeConfig.int_value(test_config, "after_click_frames", _env_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 180)))))
        await _save_step(step, "click_%d_%d" % [int(pos.x), int(pos.y)])
        step += 1

    for key_event in test_config.get("keys", []):
        var key_code := int(key_event.get("key_code", 13))
        player.send_key_event(true, key_code, int(key_event.get("modifiers", 0)), int(key_event.get("unicode", 0)))
        player.tick(1.0 / 60.0)
        player.send_key_event(false, key_code, int(key_event.get("modifiers", 0)), 0)
        await _advance(int(key_event.get("after_frames", ProbeConfig.int_value(test_config, "after_click_frames", _env_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 180)))))
        await _save_step(step, "key_%d" % key_code)
        step += 1

    for click in test_config.get("clicks_after_keys", []):
        var pos := ProbeConfig.click_position(click)
        _send_window_click(pos)
        await _advance(int(click.get("after_frames", ProbeConfig.int_value(test_config, "after_click_frames", _env_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 180)))))
        await _save_step(step, "click_%d_%d" % [int(pos.x), int(pos.y)])
        step += 1
    return step

func _run_actions(step: int) -> int:
    for raw_action in test_config["actions"]:
        if not raw_action is Dictionary:
            continue
        var action: Dictionary = raw_action
        var kind := String(action.get("type", "click"))
        var label := String(action.get("label", kind))
        if kind == "click":
            var pos := ProbeConfig.click_position(action)
            _send_window_click(pos)
            if label.is_empty() or label == "click":
                label = "click_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "right_click":
            var pos := ProbeConfig.click_position(action)
            _send_window_click(pos, 1)
            if label.is_empty() or label == "right_click":
                label = "right_click_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "move":
            var pos := ProbeConfig.click_position(action)
            _send_window_move(pos)
            if label.is_empty() or label == "move":
                label = "move_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "key":
            var key_code := int(action.get("key_code", 13))
            player.send_key_event(true, key_code, int(action.get("modifiers", 0)), int(action.get("unicode", 0)))
            player.tick(1.0 / 60.0)
            player.send_key_event(false, key_code, int(action.get("modifiers", 0)), 0)
            if label.is_empty() or label == "key":
                label = "key_%d" % key_code
        elif kind == "repeat_click":
            var pos := ProbeConfig.click_position(action)
            var count: int = max(1, int(action.get("count", 1)))
            var per_click_frames: int = max(0, int(action.get("per_click_frames", 0)))
            for i in range(count):
                _send_window_click(pos)
                if per_click_frames > 0:
                    await _advance(per_click_frames)
            if label.is_empty() or label == "repeat_click":
                label = "repeat_click_%d_%d_%d" % [count, int(pos.x), int(pos.y)]
        elif kind == "click_stream":
            step = await _run_click_stream(step, label, action)
            continue
        elif kind == "wait" or kind == "capture":
            pass
        else:
            print("skip unknown action: %s" % kind)
            continue

        await _advance(int(action.get("after_frames", ProbeConfig.int_value(test_config, "after_click_frames", _env_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 180)))))
        if bool(action.get("capture", true)):
            await _save_step(step, label)
            step += 1
    return step

func _run_click_stream(step: int, label: String, action: Dictionary) -> int:
    var pos := ProbeConfig.click_position(action)
    var mapped := _map_window_point(pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip click_stream outside texture window=%s mapped=%s" % [pos, mapped])
        return step

    var frames: int = max(1, int(action.get("frames", 180)))
    var clicks_per_frame: int = max(0, int(action.get("clicks_per_frame", 1)))
    var capture_every: int = max(0, int(action.get("capture_every", 0)))
    var spike_ms: float = max(0.0, float(action.get("spike_ms", 20.0)))
    var pointer_id: int = int(action.get("pointer_id", 100000))
    var tick_total := 0.0
    var update_total := 0.0
    var input_total := 0.0
    var frame_total := 0.0
    var tick_max := 0.0
    var update_max := 0.0
    var input_max := 0.0
    var frame_max := 0.0
    var spikes := 0
    var input_events := 0
    var measured_frames := 0

    if label.is_empty() or label == "click_stream":
        label = "click_stream_%d_%d_%d" % [frames, int(pos.x), int(pos.y)]

    player.send_pointer_event(POINTER_MOVE, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
    input_events += 1
    for frame_index in range(frames):
        var frame_start := Time.get_ticks_usec()
        var input_start := frame_start
        for i in range(clicks_per_frame):
            player.send_pointer_event(POINTER_DOWN, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
            player.send_pointer_event(POINTER_UP, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
            input_events += 2

        var after_input := Time.get_ticks_usec()
        var tick_start := after_input
        var tick_result: int = int(player.tick(1.0 / 60.0))
        var after_tick := Time.get_ticks_usec()
        if tick_result != 0:
            printerr("click_stream tick failed: %s" % player.get_last_error())
            break
        var texture: Texture2D = player.update_frame_texture()
        if texture != null:
            rect.texture = texture
            rect.queue_redraw()
        var frame_end := Time.get_ticks_usec()

        var input_ms := float(after_input - input_start) / 1000.0
        var tick_ms := float(after_tick - tick_start) / 1000.0
        var update_ms := float(frame_end - after_tick) / 1000.0
        var frame_ms := float(frame_end - frame_start) / 1000.0
        input_total += input_ms
        tick_total += tick_ms
        update_total += update_ms
        frame_total += frame_ms
        measured_frames += 1
        input_max = maxf(input_max, input_ms)
        tick_max = maxf(tick_max, tick_ms)
        update_max = maxf(update_max, update_ms)
        frame_max = maxf(frame_max, frame_ms)
        if spike_ms > 0.0 and frame_ms >= spike_ms:
            spikes += 1
            print("click_stream_spike label=%s frame=%d input_ms=%.2f tick_ms=%.2f update_ms=%.2f frame_ms=%.2f texture_backend=%s renderer=\"%s\"" % [
                label,
                frame_index,
                input_ms,
                tick_ms,
                update_ms,
                frame_ms,
                player.get_frame_texture_backend(),
                player.get_renderer_info(),
            ])

        if capture_every > 0 and (frame_index % capture_every) == 0:
            var image := _capture_frame_image()
            var capture_path := "/tmp/aetherkiri-step-%02d-%s_f%03d.png" % [
                step,
                label,
                frame_index,
            ]
            image.save_png(capture_path)
            print("step %02d label=%s frame=%d texture_backend=%s renderer=\"%s\" screenshot=%s stats=%s" % [
                step,
                label,
                frame_index,
                player.get_frame_texture_backend(),
                player.get_renderer_info(),
                capture_path,
                JSON.stringify(_image_stats(image)),
            ])
            step += 1
        await process_frame

    var divisor := float(max(1, measured_frames))
    print("click_stream label=%s frames=%d measured_frames=%d clicks_per_frame=%d input_events=%d avg_input_ms=%.2f max_input_ms=%.2f avg_tick_ms=%.2f max_tick_ms=%.2f avg_update_ms=%.2f max_update_ms=%.2f avg_frame_ms=%.2f max_frame_ms=%.2f spikes=%d spike_ms=%.2f texture_backend=%s renderer=\"%s\"" % [
        label,
        frames,
        measured_frames,
        clicks_per_frame,
        input_events,
        input_total / divisor,
        input_max,
        tick_total / divisor,
        tick_max,
        update_total / divisor,
        update_max,
        frame_total / divisor,
        frame_max,
        spikes,
        spike_ms,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
    ])

    if bool(action.get("capture_final", true)):
        await _save_step(step, "%s_final" % label)
        step += 1
    return step

func _send_window_click(window_pos: Vector2, button: int = 0) -> void:
    var mapped := _map_window_point(window_pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip click outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, 0, mapped.x, mapped.y, 0.0, 0.0, button)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_UP, 0, mapped.x, mapped.y, 0.0, 0.0, button)

func _send_window_move(window_pos: Vector2) -> void:
    var mapped := _map_window_point(window_pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip move outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)

func _map_window_point(pos: Vector2) -> Vector2:
    if rect.texture == null:
        return pos
    var tex_size := Vector2(max(1.0, float(rect.texture.get_width())),
                            max(1.0, float(rect.texture.get_height())))
    var coord := ProbeConfig.coord_size(test_config, Vector2i(
        _env_int("AETHERKIRI_PROBE_COORD_W", 1600),
        _env_int("AETHERKIRI_PROBE_COORD_H", 900)
    ))
    var panel_size := Vector2(coord)
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
