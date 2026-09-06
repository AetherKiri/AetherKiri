extends SceneTree

const ProbeConfig = preload("res://scripts/probe_config.gd")
const GameInputMapping = preload("res://scripts/game_input_mapping.gd")
const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3
const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3
const POINTER_SCROLL := 4
const POINTER_MOD_CANCEL := 1 << 30
const TOUCH_POINTER_ID_OFFSET := 100000
const TOUCH_SECONDARY_POINTER_ID := 0

var player
var rect: TextureRect
var test_config := {}
var output_dir := "/tmp"

func _initialize() -> void:
    test_config = ProbeConfig.load()
    var configured_output_dir := OS.get_environment("AETHERKIRI_PROBE_OUTPUT_DIR").strip_edges()
    if not configured_output_dir.is_empty():
        output_dir = configured_output_dir
        DirAccess.make_dir_recursive_absolute(output_dir)
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

    player = ClassDB.instantiate("AetherRuntimePlayer")
    root.add_child(player as Node)

    var user_dir := OS.get_environment("AETHERKIRI_PROBE_USER_DIR").strip_edges()
    if user_dir.is_empty():
        user_dir = OS.get_user_data_dir()
    else:
        DirAccess.make_dir_recursive_absolute(user_dir)
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
    if test_config.has("engine_options") and test_config["engine_options"] is Dictionary:
        var engine_options: Dictionary = test_config["engine_options"]
        for key in engine_options.keys():
            player.set_engine_option(String(key), String(engine_options[key]))
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
    var path := output_dir.path_join("aetherkiri-step-%02d-%s.png" % [index, label])
    image.save_png(path)
    var runtime_debug := ""
    if bool(test_config.get("runtime_debug", true)):
        runtime_debug = String(player.get_plugin_debug_info())
    print("step %02d label=%s texture_backend=%s renderer=\"%s\" screenshot=%s stats=%s" % [
        index,
        label,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
        path,
        JSON.stringify(_image_stats(image)),
    ])
    if not runtime_debug.is_empty():
        print("step %02d runtime_debug=%s" % [index, runtime_debug])
    if bool(test_config.get("runtime_logs", false)):
        var runtime_logs := String(player.drain_startup_logs()).strip_edges()
        if not runtime_logs.is_empty():
            print("step %02d runtime_logs=%s" % [index, runtime_logs])

func _capture_frame_image() -> Image:
    var prefer_engine_frame := OS.get_environment("AETHERKIRI_PROBE_PREFER_ENGINE_FRAME") == "1"
    # A GPU-direct frame is newer than the renderer's CPU compatibility
    # snapshot. Read the exact TextureRect source first so screenshots catch
    # transient crop/placement defects that are visible on screen.
    if not prefer_engine_frame and rect.texture != null:
        var direct_image := rect.texture.get_image()
        if direct_image != null and direct_image.get_width() > 0 and direct_image.get_height() > 0:
            if int(_image_stats(direct_image).get("visible", 0)) > 0:
                return direct_image

    # In headless mode the root viewport can be an opaque white dummy target.
    # Read the engine's final RGBA frame first so visual probes inspect the
    # actual KiriKiri composition rather than that dummy viewport.
    var frame: Dictionary = player.read_frame_rgba()
    var data: PackedByteArray = frame.get("rgba", PackedByteArray())
    var width := int(frame.get("width", 0))
    var height := int(frame.get("height", 0))
    if width > 0 and height > 0 and data.size() >= width * height * 4:
        var frame_image := Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, data)
        if int(_image_stats(frame_image).get("visible", 0)) > 0:
            return frame_image

    var texture := root.get_viewport().get_texture()
    if texture != null:
        var viewport_image := texture.get_image()
        if viewport_image != null and viewport_image.get_width() > 0 and viewport_image.get_height() > 0:
            if int(_image_stats(viewport_image).get("visible", 0)) > 0:
                return viewport_image

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
        if OS.get_environment("AETHERKIRI_PROBE_ACTION_TRACE") != "":
            print("step_probe_action begin label=%s kind=%s" % [label, kind])
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
        elif kind == "touch_click":
            var pos := ProbeConfig.click_position(action)
            await _send_window_touch_click(
                pos,
                int(action.get("touch_index", 0)),
                max(0, int(action.get("release_delay_ms", 0)))
            )
            if label.is_empty() or label == "touch_click":
                label = "touch_click_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "two_finger_tap":
            var pos := ProbeConfig.click_position(action)
            if not await _send_window_two_finger_tap(
                pos,
                max(0, int(action.get("first_finger_lead_ms", 0))),
                bool(action.get("forward_first_finger", false))
            ):
                continue
            if label.is_empty() or label == "two_finger_tap":
                label = "two_finger_tap_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "move":
            var pos := ProbeConfig.click_position(action)
            _send_window_move(pos)
            if label.is_empty() or label == "move":
                label = "move_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "drag":
            var from := _action_point(action, "from", ProbeConfig.click_position(action))
            var to := _action_point(action, "to", from)
            if not await _send_window_drag(
                from,
                to,
                max(1, int(action.get("steps", 12))),
                max(0, int(action.get("per_step_frames", 1))),
                int(action.get("pointer_id", 0))
            ):
                continue
            if label.is_empty() or label == "drag":
                label = "drag_%d_%d_to_%d_%d" % [int(from.x), int(from.y), int(to.x), int(to.y)]
        elif kind == "key":
            var key_code := int(action.get("key_code", 13))
            player.send_key_event(true, key_code, int(action.get("modifiers", 0)), int(action.get("unicode", 0)))
            player.tick(1.0 / 60.0)
            player.send_key_event(false, key_code, int(action.get("modifiers", 0)), 0)
            if label.is_empty() or label == "key":
                label = "key_%d" % key_code
        elif kind == "key_hold":
            var key_code := int(action.get("key_code", 17))
            var modifiers := int(action.get("modifiers", 0))
            var frames: int = max(1, int(action.get("frames", 3600)))
            var sample_every: int = max(1, int(action.get("sample_every_frames", 60)))
            var duration_ms: int = max(0, int(action.get("duration_ms", 0)))
            var sample_interval_ms: int = max(1, int(action.get("sample_interval_ms", 1000)))
            var hold_fps_limit: int = max(0, int(action.get("fps_limit", 0)))
            if hold_fps_limit > 0:
                player.set_engine_option("fps_limit", str(hold_fps_limit))
            player.send_key_event(true, key_code, modifiers, int(action.get("unicode", 0)))
            player.tick(1.0 / 60.0)
            _print_memory_sample(label, 0)
            var advanced := 0
            if duration_ms > 0:
                var started_ms := Time.get_ticks_msec()
                var next_sample_ms := sample_interval_ms
                while Time.get_ticks_msec() - started_ms < duration_ms:
                    await _advance(1)
                    advanced += 1
                    var elapsed_ms := Time.get_ticks_msec() - started_ms
                    if elapsed_ms >= next_sample_ms:
                        _print_memory_sample(label, advanced)
                        next_sample_ms += sample_interval_ms
            else:
                while advanced < frames:
                    var batch: int = mini(sample_every, frames - advanced)
                    await _advance(batch)
                    advanced += batch
                    _print_memory_sample(label, advanced)
            player.send_key_event(false, key_code, modifiers, 0)
            player.tick(1.0 / 60.0)
            if hold_fps_limit > 0:
                player.set_engine_option("fps_limit", "0")
            if label.is_empty() or label == "key_hold":
                label = "key_hold_%d_%d" % [key_code, advanced]
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
        elif kind == "scroll" or kind == "repeat_scroll":
            var pos := ProbeConfig.click_position(action)
            var count: int = max(1, int(action.get("count", 1)))
            var delta_y := float(action.get("delta_y", -1.0))
            var per_scroll_frames: int = max(0, int(action.get("per_scroll_frames", 1)))
            for i in range(count):
                _send_window_scroll(pos, delta_y)
                if per_scroll_frames > 0:
                    await _advance(per_scroll_frames)
            if label.is_empty() or label == "scroll" or label == "repeat_scroll":
                label = "scroll_%d_%d_%d" % [count, int(pos.x), int(pos.y)]
        elif kind == "click_stream":
            step = await _run_click_stream(step, label, action)
            continue
        elif kind == "wait_ms":
            var duration_ms: int = max(0, int(action.get("duration_ms", action.get("milliseconds", 0))))
            await _advance_for_ms(duration_ms)
            if label.is_empty() or label == "wait_ms":
                label = "wait_%dms" % duration_ms
        elif kind == "wait" or kind == "capture":
            pass
        else:
            print("skip unknown action: %s" % kind)
            continue

        var default_after_frames := 0 if kind == "wait_ms" else ProbeConfig.int_value(
            test_config,
            "after_click_frames",
            _env_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 180)
        )
        await _advance(int(action.get("after_frames", default_after_frames)))
        if bool(action.get("capture", true)):
            await _save_step(step, label)
            step += 1
    return step

func _print_memory_sample(label: String, frame: int) -> void:
    var memory: Dictionary = {}
    if player.has_method("get_memory_stats"):
        memory = (player.get_memory_stats() as Dictionary).duplicate(true)
    var runtime_counts: Dictionary = {}
    var runtime_debug := String(player.get_plugin_debug_info())
    if not runtime_debug.is_empty():
        var parsed = JSON.parse_string(runtime_debug)
        if parsed is Dictionary:
            var debug: Dictionary = parsed
            for key in ["emotePlayers", "emoteLayers", "emotePlayerTemplates", "runtimeJournalEntries", "layers", "cachedSurfaceBytes"]:
                if debug.has(key):
                    runtime_counts[key] = debug[key]
    var gpu_counts: Dictionary = {}
    var renderer_info := String(player.get_renderer_info())
    for field in renderer_info.split(" ", false):
        var separator := field.find("=")
        if separator <= 0:
            continue
        var key := field.left(separator)
        if key in [
            "bridge_textures",
            "bridge_texture_live_mb",
            "bridge_texture_created",
            "bridge_texture_released",
            "emote_template_hits",
            "emote_template_misses",
        ]:
            gpu_counts[key] = field.substr(separator + 1)
    print("memory_sample label=%s frame=%d memory=%s runtime=%s gpu=%s" % [
        label,
        frame,
        JSON.stringify(memory),
        JSON.stringify(runtime_counts),
        JSON.stringify(gpu_counts),
    ])

func _run_click_stream(step: int, label: String, action: Dictionary) -> int:
    var pos := ProbeConfig.click_position(action)
    var mapped := _map_window_point(pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip click_stream outside texture window=%s mapped=%s" % [pos, mapped])
        return step

    var frames: int = max(1, int(action.get("frames", 180)))
    var clicks_per_frame: int = max(0, int(action.get("clicks_per_frame", 1)))
    var click_every_frames: int = max(1, int(action.get("click_every_frames", 1)))
    var max_clicks: int = max(0, int(action.get("max_clicks", 0)))
    var capture_every: int = max(0, int(action.get("capture_every", 0)))
    # PNG encoding is intentionally deferred when requested. E-mote consumes
    # wall-clock intervals from TJS, so encoding a 1080p PNG inside the loop
    # turns the next progress() call into a 30+ frame hitch and can skip an
    # entire blink or inject an artificial physics impulse.
    var deferred_capture_every: int = max(0, int(action.get("deferred_capture_every", 0)))
    var deferred_capture_crop := Rect2i()
    if action.has("deferred_capture_crop") and action["deferred_capture_crop"] is Array:
        var crop_values: Array = action["deferred_capture_crop"]
        if crop_values.size() >= 4:
            deferred_capture_crop = Rect2i(
                int(crop_values[0]), int(crop_values[1]),
                int(crop_values[2]), int(crop_values[3])
            )
    var deferred_captures: Array[Dictionary] = []
    var spike_ms: float = max(0.0, float(action.get("spike_ms", 20.0)))
    var pointer_id: int = int(action.get("pointer_id", 100000))
    var tick_total := 0.0
    var update_total := 0.0
    var input_total := 0.0
    var frame_total := 0.0
    var present_total := 0.0
    var tick_max := 0.0
    var update_max := 0.0
    var input_max := 0.0
    var frame_max := 0.0
    var present_max := 0.0
    var spikes := 0
    var present_spikes := 0
    var input_events := 0
    var clicks_sent := 0
    var measured_frames := 0
    var stream_start_ticks := Time.get_ticks_usec()

    if label.is_empty() or label == "click_stream":
        label = "click_stream_%d_%d_%d" % [frames, int(pos.x), int(pos.y)]

    player.send_pointer_event(POINTER_MOVE, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
    input_events += 1
    for frame_index in range(frames):
        var frame_start := Time.get_ticks_usec()
        var input_start := frame_start
        var click_batch := 0
        if clicks_per_frame > 0 and (frame_index % click_every_frames) == 0:
            click_batch = clicks_per_frame
            if max_clicks > 0:
                click_batch = mini(click_batch, max_clicks - clicks_sent)
        for i in range(max(0, click_batch)):
            player.send_pointer_event(POINTER_DOWN, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
            player.send_pointer_event(POINTER_UP, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
            input_events += 2
            clicks_sent += 1

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
            var capture_path := output_dir.path_join("aetherkiri-step-%02d-%s_f%03d.png" % [
                step,
                label,
                frame_index,
            ])
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
        if deferred_capture_every > 0 and (frame_index % deferred_capture_every) == 0:
            var deferred_image := _capture_frame_image()
            if deferred_capture_crop.size.x > 0 and deferred_capture_crop.size.y > 0:
                var bounded_crop := deferred_capture_crop.intersection(
                    Rect2i(Vector2i.ZERO, deferred_image.get_size())
                )
                if bounded_crop.size.x > 0 and bounded_crop.size.y > 0:
                    deferred_image = deferred_image.get_region(bounded_crop)
            deferred_captures.append({"frame": frame_index, "image": deferred_image})
        await process_frame
        var present_ms := float(Time.get_ticks_usec() - frame_start) / 1000.0
        present_total += present_ms
        present_max = maxf(present_max, present_ms)
        if spike_ms > 0.0 and present_ms >= spike_ms:
            present_spikes += 1

    var stream_end_ticks := Time.get_ticks_usec()

    for deferred_capture in deferred_captures:
        var deferred_frame := int(deferred_capture["frame"])
        var deferred_image: Image = deferred_capture["image"]
        var deferred_path := output_dir.path_join("aetherkiri-step-%02d-%s_deferred_f%03d.png" % [
            step,
            label,
            deferred_frame,
        ])
        deferred_image.save_png(deferred_path)
        print("step %02d label=%s deferred_frame=%d screenshot=%s stats=%s" % [
            step,
            label,
            deferred_frame,
            deferred_path,
            JSON.stringify(_image_stats(deferred_image)),
        ])
        step += 1

    var divisor := float(max(1, measured_frames))
    var elapsed_sec: float = maxf(0.0001, float(stream_end_ticks - stream_start_ticks) / 1000000.0)
    var fps: float = float(measured_frames) / elapsed_sec
    print("click_stream label=%s frames=%d measured_frames=%d clicks_per_frame=%d click_every_frames=%d max_clicks=%d clicks_sent=%d input_events=%d fps=%.2f avg_present_ms=%.2f max_present_ms=%.2f present_spikes=%d avg_input_ms=%.2f max_input_ms=%.2f avg_tick_ms=%.2f max_tick_ms=%.2f avg_update_ms=%.2f max_update_ms=%.2f avg_frame_ms=%.2f max_frame_ms=%.2f spikes=%d spike_ms=%.2f texture_backend=%s renderer=\"%s\"" % [
        label,
        frames,
        measured_frames,
        clicks_per_frame,
        click_every_frames,
        max_clicks,
        clicks_sent,
        input_events,
        fps,
        present_total / divisor,
        present_max,
        present_spikes,
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

func _send_window_touch_click(
    window_pos: Vector2,
    touch_index: int = 0,
    release_delay_ms: int = 0
) -> void:
    var mapped := _map_window_point(window_pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip touch click outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    var pointer_id := TOUCH_POINTER_ID_OFFSET + touch_index
    player.send_pointer_event(POINTER_MOVE, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.send_pointer_event(POINTER_DOWN, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
    if release_delay_ms > 0:
        await _advance_for_ms(release_delay_ms)
    player.send_pointer_event(POINTER_UP, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)

func _send_window_two_finger_tap(
    window_pos: Vector2,
    first_finger_lead_ms: int = 0,
    forward_first_finger: bool = false
) -> bool:
    var mapped := _map_window_point(window_pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip two-finger tap outside texture window=%s mapped=%s" % [window_pos, mapped])
        return false
    # iOS keeps the first touch in the gesture arbiter until the second touch
    # arrives, so a qualifying two-finger gesture forwards only the synthetic
    # secondary click. The opt-in primary path is useful for desktop probes.
    var first_mapped := mapped - Vector2(12.0, 0.0)
    if forward_first_finger:
        player.send_pointer_event(POINTER_MOVE, TOUCH_POINTER_ID_OFFSET, first_mapped.x, first_mapped.y, 0.0, 0.0, 0)
        player.send_pointer_event(POINTER_DOWN, TOUCH_POINTER_ID_OFFSET, first_mapped.x, first_mapped.y, 0.0, 0.0, 0)
    if first_finger_lead_ms > 0:
        await _advance_for_ms(first_finger_lead_ms)
    player.send_pointer_event(POINTER_MOVE, TOUCH_SECONDARY_POINTER_ID, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.send_pointer_event(POINTER_DOWN, TOUCH_SECONDARY_POINTER_ID, mapped.x, mapped.y, 0.0, 0.0, 1)
    player.send_pointer_event(POINTER_UP, TOUCH_SECONDARY_POINTER_ID, mapped.x, mapped.y, 0.0, 0.0, 1)
    if forward_first_finger:
        player.send_pointer_event(
            POINTER_UP,
            TOUCH_POINTER_ID_OFFSET,
            first_mapped.x,
            first_mapped.y,
            0.0,
            0.0,
            0,
            POINTER_MOD_CANCEL
        )
    return true

func _advance_for_ms(duration_ms: int) -> void:
    if duration_ms <= 0:
        return

    # Some games schedule title/audio state from the wall clock rather than
    # accumulated tick deltas. Keep pumping at roughly 60 Hz without allowing
    # a fast headless process to simulate thousands of frames during the wait.
    var deadline_usec := Time.get_ticks_usec() + duration_ms * 1000
    var previous_tick_usec := Time.get_ticks_usec()
    var next_tick_usec := previous_tick_usec
    while Time.get_ticks_usec() < deadline_usec:
        var now_usec := Time.get_ticks_usec()
        if now_usec >= next_tick_usec:
            var delta := clampf(float(now_usec - previous_tick_usec) / 1000000.0, 1.0 / 240.0, 1.0 / 15.0)
            if player.tick(delta) == 0:
                var texture: Texture2D = player.update_frame_texture()
                if texture != null:
                    rect.texture = texture
                    rect.queue_redraw()
            previous_tick_usec = now_usec
            next_tick_usec = now_usec + 16667
        await process_frame

func _send_window_move(window_pos: Vector2) -> void:
    var mapped := _map_window_point(window_pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip move outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)

func _action_point(action: Dictionary, key: String, fallback: Vector2) -> Vector2:
    var value: Variant = action.get(key, null)
    if value is Array and value.size() >= 2:
        return Vector2(float(value[0]), float(value[1]))
    if value is Dictionary:
        return Vector2(float(value.get("x", fallback.x)), float(value.get("y", fallback.y)))
    return fallback

func _send_window_drag(from: Vector2, to: Vector2, steps: int, per_step_frames: int, pointer_id: int = 0) -> bool:
    var mapped_from := _map_window_point(from)
    var mapped_to := _map_window_point(to)
    if mapped_from.x < 0.0 or mapped_from.y < 0.0 or mapped_to.x < 0.0 or mapped_to.y < 0.0:
        print("skip drag outside texture window from=%s to=%s mapped_from=%s mapped_to=%s" % [
            from,
            to,
            mapped_from,
            mapped_to,
        ])
        return false

    player.send_pointer_event(POINTER_MOVE, pointer_id, mapped_from.x, mapped_from.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, pointer_id, mapped_from.x, mapped_from.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)

    var previous := mapped_from
    for index in range(1, steps + 1):
        var current := mapped_from.lerp(mapped_to, float(index) / float(steps))
        var delta := current - previous
        player.send_pointer_event(
            POINTER_MOVE,
            pointer_id,
            current.x,
            current.y,
            delta.x,
            delta.y,
            0,
            0
        )
        player.tick(1.0 / 60.0)
        previous = current
        if per_step_frames > 0:
            await _advance(per_step_frames)

    player.send_pointer_event(POINTER_UP, pointer_id, mapped_to.x, mapped_to.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    return true

func _send_window_scroll(window_pos: Vector2, delta_y: float) -> void:
    var mapped := _map_window_point(window_pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip scroll outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_SCROLL, 0, mapped.x, mapped.y, 0.0, delta_y, 0)

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
    return GameInputMapping.map_point(
        pos,
        Rect2(Vector2.ZERO, panel_size),
        tex_size
    )

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
