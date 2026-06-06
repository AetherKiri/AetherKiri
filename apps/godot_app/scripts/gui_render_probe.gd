extends SceneTree

const ProbeConfig = preload("res://scripts/probe_config.gd")
const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3

func _initialize() -> void:
    var config := ProbeConfig.load()
    root.size = ProbeConfig.window_size(config, Vector2i(1280, 720))

    var game_path: String = ProbeConfig.require_game_path(config)
    if game_path.is_empty():
        quit(2)
        return

    var rect := TextureRect.new()
    rect.name = "ProbeTexture"
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

    var backend: String = ProbeConfig.backend(config)
    player.set_render_backend(backend)
    var surface_size: Vector2i = ProbeConfig.surface_size(config)
    player.set_surface_size(surface_size.x, surface_size.y)

    var result: int = player.open_game(game_path, true)
    if result != 0:
        printerr("open_game failed: %s" % player.get_last_error())
        player.destroy_engine()
        quit(1)
        return

    var started := false
    for i in range(ProbeConfig.int_value(config, "startup_timeout_frames", 600)):
        var state: int = player.get_startup_state()
        if state == STARTUP_SUCCEEDED:
            started = true
            break
        if state == STARTUP_FAILED:
            printerr("startup failed: %s" % player.get_last_error())
            player.destroy_engine()
            quit(1)
            return
        await process_frame

    if not started:
        printerr("startup timed out")
        player.destroy_engine()
        quit(1)
        return

    var min_frames := ProbeConfig.int_value(config, "min_visible_frames", 0)
    var max_frames := ProbeConfig.int_value(config, "gui_probe_frames", 180)
    var texture: Texture2D
    var frame := {}
    for i in range(max_frames):
        result = player.tick(1.0 / 60.0)
        if result != 0:
            printerr("tick failed: %s" % player.get_last_error())
            player.destroy_engine()
            quit(1)
            return
        texture = player.update_frame_texture()
        if texture != null:
            rect.texture = texture
        frame = player.read_frame_rgba()
        if i >= min_frames and _frame_has_visible_pixels(frame):
            break
        await process_frame

    await process_frame
    await process_frame

    var stats := _frame_stats(frame)
    var screenshot := root.get_viewport().get_texture().get_image()
    var screenshot_stats := _image_stats(screenshot)
    var output_path := OS.get_user_data_dir().path_join("gui_render_probe.png")
    screenshot.save_png(output_path)

    print("gui probe renderer=\"%s\" texture_backend=%s frame=%dx%d serial=%d stats=%s screenshot=%s screenshot_stats=%s" % [
        player.get_renderer_info(),
        player.get_frame_texture_backend(),
        int(frame.get("width", 0)),
        int(frame.get("height", 0)),
        int(frame.get("frame_serial", 0)),
        JSON.stringify(stats),
        output_path,
        JSON.stringify(screenshot_stats),
    ])

    var screenshot_visible := int(screenshot_stats.get("visible", 0))
    player.destroy_engine()
    if screenshot_visible <= 0:
        quit(2)
        return
    quit(0)

func _frame_has_visible_pixels(frame: Dictionary) -> bool:
    return int(_frame_stats(frame).get("visible", 0)) > 0

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
