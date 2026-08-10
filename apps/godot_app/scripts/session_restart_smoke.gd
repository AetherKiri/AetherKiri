extends SceneTree

const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3
const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3
const ONSCRIPTER_SCRIPT_MARKERS := [
    "0.txt",
    "00.txt",
    "nscr_sec.dat",
    "nscript.___",
    "nscript.dat",
    "onscript.nt2",
    "onscript.nt3",
]

var retained_player
var retained_player_class := ""

func _game_root(path: String) -> String:
    return path.get_base_dir() if FileAccess.file_exists(path) else path

func _is_onscripter_game(path: String) -> bool:
    var root_path := _game_root(path)
    for marker in ONSCRIPTER_SCRIPT_MARKERS:
        if FileAccess.file_exists(root_path.path_join(marker)):
            return true
    return false

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
    var is_onscripter := _is_onscripter_game(game_path)
    var player_class_name := (
        "AetherOnscripterPlayer" if is_onscripter else "AetherKiriPlayer"
    )
    if not ClassDB.class_exists(player_class_name):
        printerr("session %d player class is unavailable: %s" % [
            index,
            player_class_name,
        ])
        return false
    if is_onscripter:
        game_path = _game_root(game_path)
    var player
    if retained_player != null and retained_player_class == player_class_name:
        player = retained_player
        retained_player = null
        retained_player_class = ""
    else:
        if retained_player != null:
            (retained_player as Node).queue_free()
            retained_player = null
            retained_player_class = ""
            await process_frame
        player = ClassDB.instantiate(player_class_name)
        root.add_child(player as Node)
    if player.has_signal("platform_request"):
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
    if is_onscripter:
        player.set_engine_option(
            "default_font",
            ProjectSettings.globalize_path(
                "res://assets/fonts/aetherkiri-runtime-cjk.otf"
            )
        )
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

    var warmup_frames := 60 if is_onscripter else 3
    for _frame in range(warmup_frames):
        result = player.tick(1.0 / 60.0)
        if result != 0:
            printerr("session %d tick failed: %s" % [index, player.get_last_error()])
            player.destroy_engine()
            return false
        await process_frame

    var natural_exit_x := OS.get_environment(
        "AETHERKIRI_SMOKE_NATURAL_EXIT_X"
    ).strip_edges()
    var natural_exit_y := OS.get_environment(
        "AETHERKIRI_SMOKE_NATURAL_EXIT_Y"
    ).strip_edges()
    if (
        is_onscripter
        and index == 1
        and not natural_exit_x.is_empty()
        and not natural_exit_y.is_empty()
    ):
        # Exercise the game's natural `end` command instead of only
        # host-forced shutdown. Coordinates are supplied by the authorized
        # local reproduction and are never committed as game-specific data.
        var exit_position := Vector2(
            natural_exit_x.to_float(),
            natural_exit_y.to_float()
        )
        player.send_pointer_event(
            POINTER_MOVE, 0, exit_position.x, exit_position.y,
            0.0, 0.0, 0
        )
        player.tick(1.0 / 60.0)
        player.send_pointer_event(
            POINTER_DOWN, 0, exit_position.x, exit_position.y,
            0.0, 0.0, 0
        )
        player.tick(1.0 / 60.0)
        player.send_pointer_event(
            POINTER_UP, 0, exit_position.x, exit_position.y,
            0.0, 0.0, 0
        )
        var naturally_ended := false
        for _frame in range(600):
            result = player.tick(1.0 / 60.0)
            if result != 0:
                if String(player.get_last_error()).contains(
                    "runtime requested termination"
                ):
                    naturally_ended = true
                    break
                printerr("session %d natural exit failed: %s" % [
                    index,
                    player.get_last_error(),
                ])
                player.destroy_engine()
                return false
            await process_frame
        if not naturally_ended:
            printerr("session %d natural exit timed out" % index)
            player.destroy_engine()
            return false

    player.destroy_engine()
    if index == 1:
        retained_player = player
        retained_player_class = player_class_name
    else:
        (player as Node).queue_free()
    await process_frame
    await process_frame
    return true
