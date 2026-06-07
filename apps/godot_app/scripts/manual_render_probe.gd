extends SceneTree

const ProbeConfig = preload("res://scripts/probe_config.gd")

const ENGINE_RESULT_OK := 0
const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3

const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3
const POINTER_SCROLL := 4

const TOUCH_MOUSE_SUPPRESS_MS := 700

var player
var rect: TextureRect
var config := {}
var started := false
var frame_count := 0
var screenshot_index := 0
var suppress_mouse_until_msec := 0

func _initialize() -> void:
    config = ProbeConfig.load()
    root.size = ProbeConfig.window_size(config, Vector2i(
        _env_int("AETHERKIRI_MANUAL_PROBE_WINDOW_W", 1600),
        _env_int("AETHERKIRI_MANUAL_PROBE_WINDOW_H", 900)
    ))
    root.title = "AetherKiri Manual Render Probe"

    rect = TextureRect.new()
    rect.name = "ManualProbeTexture"
    rect.set_anchors_preset(Control.PRESET_FULL_RECT)
    rect.mouse_filter = Control.MOUSE_FILTER_STOP
    rect.focus_mode = Control.FOCUS_ALL
    rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    rect.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
    rect.gui_input.connect(_on_rect_gui_input)
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

    var backend: String = ProbeConfig.backend(config, "AETHERKIRI_PROBE_BACKEND")
    player.set_render_backend(backend)
    var surface_size: Vector2i = ProbeConfig.surface_size(config)
    player.set_surface_size(surface_size.x, surface_size.y)

    var game_path: String = ProbeConfig.require_game_path(config)
    if game_path.is_empty():
        _destroy_player()
        quit(2)
        return

    var result: int = player.open_game(game_path, true)
    if result != ENGINE_RESULT_OK:
        printerr("open_game failed: %s" % player.get_last_error())
        _destroy_player()
        quit(1)
        return

    print("manual probe opening path=%s backend=%s surface=%dx%d" % [
        game_path,
        backend,
        surface_size.x,
        surface_size.y,
    ])

func _process(delta: float) -> bool:
    if player == null:
        return false

    if not started:
        if not _poll_startup():
            return false
        started = true
        print("manual probe ready renderer=\"%s\" texture_backend=%s" % [
            player.get_renderer_info(),
            player.get_frame_texture_backend(),
        ])

    _tick_and_update(delta)
    frame_count += 1

    var auto_capture_frame := _env_int("AETHERKIRI_MANUAL_PROBE_AUTO_CAPTURE_FRAME", 0)
    if auto_capture_frame > 0 and frame_count == auto_capture_frame:
        _save_screenshot("auto")

    var auto_quit_frames := _env_int("AETHERKIRI_MANUAL_PROBE_AUTO_QUIT_FRAMES", 0)
    if auto_quit_frames > 0 and frame_count >= auto_quit_frames:
        _exit_probe(0)
    return false

func _on_rect_gui_input(event: InputEvent) -> void:
    if player == null or not started:
        return

    if _handle_pointer_event(event):
        root.set_input_as_handled()
        return

    if event is InputEventKey:
        var key := event as InputEventKey
        if key.pressed and not key.echo:
            if key.keycode == KEY_F12:
                _save_screenshot("manual")
                root.set_input_as_handled()
                return
            if key.keycode == KEY_ESCAPE:
                _exit_probe(0)
                root.set_input_as_handled()
                return
        player.send_key_event(key.pressed, key.keycode, key.get_modifiers_mask(), key.unicode)
        root.set_input_as_handled()

func _finalize() -> void:
    if OS.get_environment("AETHERKIRI_MANUAL_PROBE_DESTROY_ON_EXIT") == "1":
        _destroy_player()

func _poll_startup() -> bool:
    var state: int = player.get_startup_state()
    if state == STARTUP_SUCCEEDED:
        return true
    if state == STARTUP_FAILED:
        printerr("startup failed: %s" % player.get_last_error())
        _exit_probe(1)
        return false
    return false

func _tick_and_update(delta: float) -> void:
    var tick_delta := delta
    if tick_delta <= 0.0 or tick_delta > 0.1:
        tick_delta = 1.0 / 60.0
    var result: int = player.tick(tick_delta)
    if result != ENGINE_RESULT_OK:
        printerr("tick failed: %s %s" % [player.get_last_result(), player.get_last_error()])
        _exit_probe(1)
        return
    var texture: Texture2D = player.update_frame_texture()
    if texture != null:
        rect.texture = texture
        rect.queue_redraw()

func _handle_pointer_event(event: InputEvent) -> bool:
    if event is InputEventMouseButton:
        var mouse_button := event as InputEventMouseButton
        if _is_touch_platform() and mouse_button.button_index != MOUSE_BUTTON_WHEEL_UP and mouse_button.button_index != MOUSE_BUTTON_WHEEL_DOWN:
            return Time.get_ticks_msec() < suppress_mouse_until_msec
        if mouse_button.pressed:
            rect.grab_focus()
        var mapped := _map_viewport_point(mouse_button.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return false
        if mouse_button.pressed:
            print("manual probe mouse window=%s mapped=%s button=%d" % [
                mouse_button.position,
                mapped,
                mouse_button.button_index,
            ])
        var event_type := POINTER_DOWN if mouse_button.pressed else POINTER_UP
        if mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP or mouse_button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
            event_type = POINTER_SCROLL
        var button := _map_mouse_button(mouse_button.button_index)
        if event_type == POINTER_DOWN:
            player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, button)
            _pump_pointer_event_tick()
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
            _pump_pointer_event_tick()
        return true

    if event is InputEventMouseMotion:
        if _is_touch_platform():
            return Time.get_ticks_msec() < suppress_mouse_until_msec
        var motion := event as InputEventMouseMotion
        var mapped := _map_viewport_point(motion.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return false
        var rel := _map_viewport_delta(motion.relative)
        player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, rel.x, rel.y, 0)
        return true

    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        suppress_mouse_until_msec = Time.get_ticks_msec() + TOUCH_MOUSE_SUPPRESS_MS
        var mapped := _map_viewport_point(touch.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return false
        var event_type := POINTER_DOWN if touch.pressed else POINTER_UP
        if event_type == POINTER_DOWN:
            player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
        player.send_pointer_event(event_type, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
        _pump_pointer_event_tick()
        return true

    if event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        suppress_mouse_until_msec = Time.get_ticks_msec() + TOUCH_MOUSE_SUPPRESS_MS
        var mapped := _map_viewport_point(drag.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            return false
        var rel := _map_viewport_delta(drag.relative)
        player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, rel.x, rel.y, 0)
        return true

    return false

func _map_viewport_point(pos: Vector2) -> Vector2:
    if rect.texture == null:
        return pos
    var local_pos := pos - rect.get_global_rect().position
    var tex_size := Vector2(
        max(1.0, float(rect.texture.get_width())),
        max(1.0, float(rect.texture.get_height()))
    )
    var panel_size := rect.size
    var scale: float = min(panel_size.x / tex_size.x, panel_size.y / tex_size.y)
    if scale <= 0.0:
        return Vector2(-1.0, -1.0)
    var drawn_size := tex_size * scale
    var offset := (panel_size - drawn_size) * 0.5
    var inside := local_pos - offset
    if inside.x < 0.0 or inside.y < 0.0 or inside.x > drawn_size.x or inside.y > drawn_size.y:
        return Vector2(-1.0, -1.0)
    return inside / scale

func _map_viewport_delta(delta: Vector2) -> Vector2:
    if rect.texture == null:
        return delta
    var tex_size := Vector2(
        max(1.0, float(rect.texture.get_width())),
        max(1.0, float(rect.texture.get_height()))
    )
    var panel_size := rect.size
    var scale: float = min(panel_size.x / tex_size.x, panel_size.y / tex_size.y)
    return delta / max(0.0001, scale)

func _pump_pointer_event_tick() -> void:
    if player == null or not started:
        return
    var result: int = player.tick(1.0 / 60.0)
    if result != ENGINE_RESULT_OK:
        printerr("pointer tick failed: %s %s" % [player.get_last_result(), player.get_last_error()])

func _save_screenshot(label: String) -> void:
    await process_frame
    await process_frame
    screenshot_index += 1
    var image := root.get_viewport().get_texture().get_image()
    var path := "/tmp/aetherkiri-manual-%02d-%s.png" % [screenshot_index, label]
    image.save_png(path)
    print("manual probe screenshot=%s stats=%s renderer=\"%s\"" % [
        path,
        JSON.stringify(_image_stats(image)),
        player.get_renderer_info() if player != null else "",
    ])

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

func _map_mouse_button(button_index: MouseButton) -> int:
    if button_index == MOUSE_BUTTON_RIGHT:
        return 1
    if button_index == MOUSE_BUTTON_MIDDLE:
        return 2
    return 0

func _is_touch_platform() -> bool:
    var platform := OS.get_name()
    return platform == "iOS" or platform == "Android"

func _destroy_player() -> void:
    if player == null:
        return
    if rect != null:
        rect.texture = null
    player.release_frame_texture()
    player.destroy_engine()
    player = null

func _exit_probe(code: int) -> void:
    if OS.get_environment("AETHERKIRI_MANUAL_PROBE_DESTROY_ON_EXIT") == "1":
        _destroy_player()
        quit(code)
        return
    if rect != null:
        rect.texture = null
    print("manual probe fast exit code=%d" % code)
    OS.kill(OS.get_process_id())

func _env_int(name: String, fallback: int) -> int:
    var value := OS.get_environment(name)
    if value.is_empty():
        return fallback
    return int(value)
