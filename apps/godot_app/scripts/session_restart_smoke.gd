extends SceneTree

const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3

func _initialize() -> void:
    var game_path := OS.get_environment("AETHERKIRI_SMOKE_GAME").strip_edges()
    if game_path.is_empty():
        printerr("AETHERKIRI_SMOKE_GAME is not set")
        quit(2)
        return

    var second_game_path := OS.get_environment("AETHERKIRI_SMOKE_GAME_2").strip_edges()
    if second_game_path.is_empty():
        second_game_path = game_path

    var first_plugin_mode := OS.get_environment("AETHERKIRI_SMOKE_PLUGIN_MODE").strip_edges()
    if not first_plugin_mode in ["krkrsdl3", "aether_all"]:
        first_plugin_mode = "krkrsdl3"

    var second_plugin_mode := OS.get_environment("AETHERKIRI_SMOKE_PLUGIN_MODE_2").strip_edges()
    if not second_plugin_mode in ["krkrsdl3", "aether_all"]:
        second_plugin_mode = first_plugin_mode

    if not await _run_session(game_path, first_plugin_mode, 1):
        quit(1)
        return
    if not await _run_session(second_game_path, second_plugin_mode, 2):
        quit(1)
        return

    print("SESSION_RESTART_SMOKE_OK")
    quit(0)

func _run_session(game_path: String, plugin_mode: String, index: int) -> bool:
    var player = ClassDB.instantiate("AetherKiriPlayer")
    root.add_child(player as Node)
    player.platform_request.connect(
        func(operation: String, _argument: String) -> void:
            if operation == "dialog":
                player.submit_platform_response("dialog", "result=1&text=")
    )
    var session_dir := OS.get_user_data_dir().path_join("session-restart-%d" % index)
    DirAccess.make_dir_recursive_absolute(session_dir)
    if not player.initialize_engine(session_dir, session_dir.path_join("cache")):
        printerr("session %d initialize failed: %s" % [index, player.get_last_error()])
        return false

    player.set_engine_option("plugin_load_mode", plugin_mode)
    player.set_render_backend("GodotNative")
    player.set_surface_size(1280, 720)
    var result: int = player.open_game(game_path, true)
    if result != 0:
        printerr("session %d open failed: %s" % [index, player.get_last_error()])
        player.destroy_engine()
        return false

    var started := false
    for _frame in range(900):
        # engine_tick intentionally reports INVALID_STATE during async startup,
        # but the Godot wrapper still drains platform requests afterwards. This
        # keeps startup scripts with informational dialogs from deadlocking the
        # smoke test.
        player.tick(1.0 / 60.0)
        var state: int = player.get_startup_state()
        if state == STARTUP_SUCCEEDED:
            started = true
            break
        if state == STARTUP_FAILED:
            printerr("session %d startup failed: %s" % [index, player.get_last_error()])
            player.destroy_engine()
            return false
        await process_frame

    if not started:
        printerr("session %d startup timed out" % index)
        player.destroy_engine()
        return false

    for _frame in range(3):
        result = player.tick(1.0 / 60.0)
        if result != 0:
            printerr("session %d tick failed: %s" % [index, player.get_last_error()])
            player.destroy_engine()
            return false
        await process_frame

    player.destroy_engine()
    (player as Node).queue_free()
    await process_frame
    await process_frame
    return true
