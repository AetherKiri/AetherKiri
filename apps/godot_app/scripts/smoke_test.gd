extends SceneTree

const ProbeConfig = preload("res://scripts/probe_config.gd")

func _initialize() -> void:
    var config := ProbeConfig.load()
    var game_path: String = ProbeConfig.require_game_path(config)
    if game_path.is_empty():
        printerr("AETHERKIRI_SMOKE_GAME is not set")
        quit(2)
        return

    var player = ClassDB.instantiate("AetherKiriPlayer")
    root.add_child(player as Node)

    var user_dir := OS.get_user_data_dir()
    var cache_dir := user_dir.path_join("cache")
    if not player.initialize_engine(user_dir, cache_dir):
        printerr("initialize_engine failed: %s" % player.get_last_error())
        quit(1)
        return

    var backend: String = ProbeConfig.backend(config)
    player.set_render_backend(backend)
    if OS.get_environment("AETHERKIRI_EXPORT_SCRIPTS") == "1" or ProbeConfig.bool_value(config, "export_scripts", false):
        player.set_engine_option("export_scripts", "1")
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
        if state == 2:
            started = true
            break
        if state == 3:
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

    for i in range(ProbeConfig.int_value(config, "smoke_tick_frames", 5)):
        result = player.tick(1.0 / 60.0)
        if result != 0:
            printerr("tick failed: %s" % player.get_last_error())
            player.destroy_engine()
            quit(1)
            return
        await process_frame

    var frame: Dictionary = player.read_frame_rgba()
    var bytes: int = frame.get("rgba", PackedByteArray()).size()
    var width: int = int(frame.get("width", 0))
    var height: int = int(frame.get("height", 0))
    if width <= 0 or height <= 0 or bytes <= 0:
        printerr("empty frame backend=%s frame=%dx%d bytes=%d renderer=%s" % [
            backend,
            width,
            height,
            bytes,
            player.get_renderer_info(),
        ])
        player.destroy_engine()
        quit(1)
        return

    var texture: Texture2D = player.update_frame_texture()
    if texture == null or texture.get_width() != width or texture.get_height() != height:
        printerr("texture update failed backend=%s frame=%dx%d renderer=%s" % [
            backend,
            width,
            height,
            player.get_renderer_info(),
        ])
        player.destroy_engine()
        quit(1)
        return

    print("smoke ok backend=%s renderer=\"%s\" texture_backend=%s frame=%dx%d texture=%dx%d serial=%d bytes=%d" % [
        backend,
        player.get_renderer_info(),
        player.get_frame_texture_backend(),
        width,
        height,
        texture.get_width(),
        texture.get_height(),
        int(frame.get("frame_serial", 0)),
        bytes,
    ])
    player.destroy_engine()
    quit(0)
