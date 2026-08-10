extends SceneTree

const ProbeConfig = preload("res://scripts/probe_config.gd")
const ONSCRIPTER_SCRIPT_MARKERS := [
    "0.txt",
    "00.txt",
    "nscr_sec.dat",
    "nscript.___",
    "nscript.dat",
    "onscript.nt2",
    "onscript.nt3",
]
const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3

func _game_root(path: String) -> String:
    return path.get_base_dir() if FileAccess.file_exists(path) else path

func _is_onscripter_game(path: String) -> bool:
    var root_path := _game_root(path)
    for marker in ONSCRIPTER_SCRIPT_MARKERS:
        if FileAccess.file_exists(root_path.path_join(marker)):
            return true
    return false

func _smoke_media_bridge(player, media_path: String) -> bool:
    if media_path.is_empty():
        return true
    if not bool(player.media_open(media_path)):
        printerr("media_open failed: %s" % player.get_last_error())
        return false
    if int(player.media_play()) != 0:
        printerr("media_play failed: %s" % player.get_last_error())
        player.media_close()
        return false

    var state := {}
    for i in range(600):
        state = player.media_get_state()
        if bool(state.get("frame_ready", false)):
            break
        if int(state.get("status", 0)) == 4:
            break
        await process_frame
    if not bool(state.get("frame_ready", false)):
        printerr("media frame timed out: %s" % str(state))
        player.media_close()
        return false

    var texture: Texture2D = player.media_update_texture()
    if texture == null or texture.get_width() <= 0 or texture.get_height() <= 0:
        printerr("media texture update failed: %s" % str(state))
        player.media_close()
        return false
    if int(player.media_pause()) != 0:
        printerr("media_pause failed")
        player.media_close()
        return false
    if bool(state.get("seekable", false)) and int(player.media_seek(0.0)) != 0:
        printerr("media_seek failed")
        player.media_close()
        return false
    if int(player.media_set_rate(1.0)) != 0 or int(player.media_play()) != 0:
        printerr("media resume/rate failed")
        player.media_close()
        return false
    print("media smoke ok size=%dx%d serial=%d audio=%s video=%s" % [
        texture.get_width(),
        texture.get_height(),
        int(state.get("frame_serial", 0)),
        str(state.get("has_audio", false)),
        str(state.get("has_video", false)),
    ])
    player.media_close()
    return true

func _initialize() -> void:
    var config := ProbeConfig.load()
    var game_path: String = ProbeConfig.require_game_path(config)
    if game_path.is_empty():
        printerr("AETHERKIRI_SMOKE_GAME is not set")
        quit(2)
        return

    var is_onscripter := _is_onscripter_game(game_path)
    var player_class_name := "AetherOnscripterPlayer" if is_onscripter else "AetherKiriPlayer"
    if not ClassDB.class_exists(player_class_name):
        printerr("%s is unavailable" % player_class_name)
        quit(1)
        return
    if is_onscripter:
        game_path = _game_root(game_path)
    var player = ClassDB.instantiate(player_class_name)
    root.add_child(player as Node)

    var user_dir := OS.get_user_data_dir()
    var cache_dir := user_dir.path_join("cache")
    if not player.initialize_engine(user_dir, cache_dir):
        printerr("initialize_engine failed: %s" % player.get_last_error())
        quit(1)
        return

    var media_path := OS.get_environment("AETHERKIRI_SMOKE_MEDIA").strip_edges()
    if not await _smoke_media_bridge(player, media_path):
        player.destroy_engine()
        quit(1)
        return

    var backend: String = ProbeConfig.backend(config)
    player.set_render_backend(backend)
    if is_onscripter:
        var runtime_font := ProjectSettings.globalize_path(
            "res://assets/fonts/aetherkiri-runtime-cjk.otf"
        )
        player.set_engine_option("default_font", runtime_font)
        var onscripter_encoding := OS.get_environment("AETHERKIRI_ONS_ENCODING").strip_edges()
        if not onscripter_encoding.is_empty():
            player.set_engine_option("onscripter_encoding", onscripter_encoding)
    if OS.get_environment("AETHERKIRI_EXPORT_SCRIPTS") == "1" or ProbeConfig.bool_value(config, "export_scripts", false):
        player.set_engine_option("export_scripts", "1")
    var surface_size: Vector2i = ProbeConfig.surface_size(config)
    player.set_surface_size(surface_size.x, surface_size.y)
    var frame_enhancement_enabled: bool = (
        ProbeConfig.bool_value(config, "frame_enhancement_enabled", false)
        and player.has_method("set_frame_enhancement_enabled")
    )
    if frame_enhancement_enabled:
        player.set_frame_native_output_enabled(false)
        player.set_frame_enhancement_mode(ProbeConfig.string_value(
            config,
            "frame_enhancement_mode",
            "chain_soft"
        ))
        if (
            config.has("frame_enhancement_custom_chain")
            and config["frame_enhancement_custom_chain"] is Array
        ):
            player.set_frame_enhancement_custom_chain(PackedStringArray(
                config["frame_enhancement_custom_chain"]
            ))
        player.set_frame_enhancement_target_size(
            surface_size.x,
            surface_size.y
        )
        player.set_frame_enhancement_enabled(true)

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

    for i in range(ProbeConfig.int_value(config, "warmup_frames", 0)):
        if player.tick(1.0 / 60.0) != 0:
            printerr("warmup tick failed: %s" % player.get_last_error())
            player.destroy_engine()
            quit(1)
            return
        await process_frame

    for click in ProbeConfig.clicks(config):
        var position := ProbeConfig.click_position(click)
        player.send_pointer_event(
            POINTER_MOVE, 0, position.x, position.y, 0.0, 0.0, 0
        )
        player.tick(1.0 / 60.0)
        await process_frame
        await process_frame
        player.send_pointer_event(
            POINTER_DOWN, 0, position.x, position.y, 0.0, 0.0, 0
        )
        player.tick(1.0 / 60.0)
        await process_frame
        player.send_pointer_event(
            POINTER_UP, 0, position.x, position.y, 0.0, 0.0, 0
        )
        for i in range(int(click.get("after_frames", 180))):
            if player.tick(1.0 / 60.0) != 0:
                printerr("tick after click failed: %s" % player.get_last_error())
                player.destroy_engine()
                quit(1)
                return
            await process_frame

    for key_event in config.get("keys", []):
        var key_code := int(key_event.get("key_code", 13))
        var modifiers := int(key_event.get("modifiers", 0))
        player.send_key_event(
            true,
            key_code,
            modifiers,
            int(key_event.get("unicode", 0))
        )
        player.tick(1.0 / 60.0)
        player.send_key_event(false, key_code, modifiers, 0)
        for i in range(int(key_event.get("after_frames", 180))):
            if player.tick(1.0 / 60.0) != 0:
                printerr("tick after key failed: %s" % player.get_last_error())
                player.destroy_engine()
                quit(1)
                return
            await process_frame

    if OS.get_environment("AETHERKIRI_SMOKE_EXPECT_SCRIPT_MEDIA") == "1":
        var script_media_state := {}
        for i in range(600):
            player.tick(1.0 / 60.0)
            script_media_state = player.media_get_state()
            if bool(script_media_state.get("frame_ready", false)):
                break
            await process_frame
        if not bool(script_media_state.get("frame_ready", false)):
            printerr("ONS movie command did not produce a frame: %s" % [
                str(script_media_state),
            ])
            player.destroy_engine()
            quit(1)
            return
        await create_timer(0.5).timeout
        player.tick(1.0 / 60.0)
        player.send_key_event(true, 0x1b, 0, 0)
        player.send_key_event(false, 0x1b, 0, 0)
        await create_timer(0.3).timeout
        player.tick(1.0 / 60.0)
        player.send_key_event(true, 0x1b, 0, 0)
        player.send_key_event(false, 0x1b, 0, 0)
        await create_timer(0.4).timeout
        player.tick(1.0 / 60.0)
        var command_logs := String(player.drain_startup_logs())
        if not command_logs.contains("[ONScripter Yuri media] movie stop"):
            printerr("ONS movie stop command was not observed: %s" % command_logs)
            player.destroy_engine()
            quit(1)
            return
        if command_logs.count("[ONScripter Yuri media] playing:") < 4:
            printerr("ONS movie/mpegplay/avi commands were not all observed: %s" % command_logs)
            player.destroy_engine()
            quit(1)
            return
        print("ONS movie command smoke ok size=%dx%d status=%d serial=%d" % [
            int(script_media_state.get("width", 0)),
            int(script_media_state.get("height", 0)),
            int(script_media_state.get("status", 0)),
            int(script_media_state.get("frame_serial", 0)),
        ])

    if ProbeConfig.bool_value(config, "smoke_startup_only", false):
        print("smoke ok startup_only backend=%s renderer=\"%s\"" % [
            backend,
            player.get_renderer_info(),
        ])
        player.destroy_engine()
        quit(0)
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

    if OS.get_environment("AETHERKIRI_SMOKE_EXPECT_SCRIPT_MEDIA") == "1":
        var outside_offset := (10 * width + 10) * 4
        var inside_offset := ((height / 2) * width + (width / 2)) * 4
        var outside_distance_from_white := 0
        var inside_outside_difference := 0
        for channel in range(3):
            outside_distance_from_white += abs(
                int(frame["rgba"][outside_offset + channel]) - 255
            )
            inside_outside_difference += abs(
                int(frame["rgba"][inside_offset + channel]) -
                int(frame["rgba"][outside_offset + channel])
            )
        if outside_distance_from_white > 12 or inside_outside_difference < 24:
            printerr("ONS positioned movie was not composited correctly: outside=%d difference=%d" % [
                outside_distance_from_white,
                inside_outside_difference,
            ])
            player.destroy_engine()
            quit(1)
            return

    var texture: Texture2D = player.update_frame_texture()
    var expected_texture_size := (
        surface_size if frame_enhancement_enabled else Vector2i(width, height)
    )
    if (
        texture == null
        or texture.get_width() != expected_texture_size.x
        or texture.get_height() != expected_texture_size.y
    ):
        printerr("texture update failed backend=%s frame=%dx%d renderer=%s" % [
            backend,
            width,
            height,
            player.get_renderer_info(),
        ])
        player.destroy_engine()
        quit(1)
        return

    var screenshot_path := OS.get_environment(
        "AETHERKIRI_SMOKE_SCREENSHOT"
    ).strip_edges()
    if not screenshot_path.is_empty():
        var screenshot := texture.get_image() if frame_enhancement_enabled else Image.create_from_data(
            width,
            height,
            false,
            Image.FORMAT_RGBA8,
            frame["rgba"]
        )
        var screenshot_error := screenshot.save_png(screenshot_path)
        if screenshot_error != OK:
            printerr("failed to save smoke screenshot: %s" % screenshot_path)
            player.destroy_engine()
            quit(1)
            return
        print("smoke screenshot=%s" % screenshot_path)

    if frame_enhancement_enabled:
        var effect_status: Dictionary = player.get_frame_enhancement_status()
        if not bool(effect_status.get("active", false)):
            printerr("frame enhancement did not activate: %s" % [
                JSON.stringify(effect_status),
            ])
            player.destroy_engine()
            quit(1)
            return
        print("smoke frame enhancement=%s" % JSON.stringify(effect_status))

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
