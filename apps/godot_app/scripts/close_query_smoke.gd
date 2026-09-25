extends SceneTree

# close_query_smoke.gd — verifies issue #221 exit fidelity:
#   Phase A (synthetic game): Window.close() must deliver the synchronous
#   onCloseQuery event; a veto keeps the session alive, a later allow ends it
#   with "runtime requested termination".
#   Phase B (real KAG3 scripts): kag.shutdown() must run the game's
#   saveSystemVariables() (datasc.ksd / datasu.ksd written at exit) and then
#   terminate cleanly.
#
# Env:
#   AETHERKIRI_CLOSEQUERY_GAME      synthetic game directory
#   AETHERKIRI_KAG3_EXIT_GAME       patched KAG3 demo directory

const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3
const KEY_DOWN := 1
const POINTER_MOVE := 2
const POINTER_DOWN := 1
const POINTER_UP := 3

var retained_player = null
var phase_log: Array[String] = []


func _initialize() -> void:
    var synthetic_game := OS.get_environment("AETHERKIRI_CLOSEQUERY_GAME").strip_edges()
    var kag3_game := OS.get_environment("AETHERKIRI_KAG3_EXIT_GAME").strip_edges()
    if synthetic_game.is_empty() or kag3_game.is_empty():
        printerr("AETHERKIRI_CLOSEQUERY_GAME / AETHERKIRI_KAG3_EXIT_GAME not set")
        quit(2)
        return

    var ok := true
    ok = await _run_synthetic_veto_allow(synthetic_game)
    if not ok:
        quit(1)
        return
    ok = await _run_kag3_shutdown(kag3_game)
    if not ok:
        quit(1)
        return
    for line in phase_log:
        print(line)
    print("CLOSE_QUERY_SMOKE_OK")
    quit(0)


func _make_player() -> Object:
    if retained_player != null:
        var reused = retained_player
        retained_player = null
        return reused
    var player = ClassDB.instantiate("AetherRuntimePlayer")
    root.add_child(player as Node)
    return player


func _retire_player(player: Object, keep: bool) -> void:
    if keep:
        retained_player = player
    else:
        (player as Node).queue_free()
    await process_frame
    await process_frame


func _open_and_start(player: Object, game_path: String, phase: String) -> bool:
    var session_dir := OS.get_user_data_dir().path_join(phase)
    DirAccess.make_dir_recursive_absolute(session_dir)
    if not player.initialize_engine(session_dir, session_dir.path_join("cache")):
        printerr("%s initialize failed: %s" % [phase, player.get_last_error()])
        return false
    player.set_engine_option("plugin_load_mode", "krkrsdl3")
    player.set_engine_option(
        "default_font",
        ProjectSettings.globalize_path("res://assets/fonts/aetherkiri-runtime-cjk.otf"))
    player.set_engine_option("font_dir", ProjectSettings.globalize_path("res://assets/fonts"))
    player.set_render_backend("GodotNative")
    player.set_surface_size(1280, 720)
    var result: int = player.open_game(game_path, true)
    if result != 0:
        printerr("%s open failed: %s" % [phase, player.get_last_error()])
        player.destroy_engine()
        return false
    for _frame in range(900):
        player.tick(1.0 / 60.0)
        var state: int = player.get_startup_state()
        if state == STARTUP_SUCCEEDED:
            return true
        if state == STARTUP_FAILED:
            printerr("%s startup failed: %s" % [phase, player.get_last_error()])
            player.destroy_engine()
            return false
        await process_frame
    printerr("%s startup timed out" % phase)
    player.destroy_engine()
    return false


func _run_synthetic_veto_allow(game_path: String) -> bool:
    if not ClassDB.class_exists("AetherRuntimePlayer"):
        printerr("phase A player class unavailable")
        return false
    var player = _make_player()
    if not await _open_and_start(player, game_path, "close-query-smoke-a"):
        _retire_player(player, false)
        return false
    phase_log.append("phase A: startup succeeded after vetoed close()")

    # Phase 1 — the startup script already called win.close() and the game
    # vetoed it. Every tick must stay healthy: the old stub would have
    # terminated the session during startup instead.
    for _frame in range(60):
        var result: int = player.tick(1.0 / 60.0)
        if result != 0:
            printerr("phase A veto failed: %s" % player.get_last_error())
            player.destroy_engine()
            _retire_player(player, false)
            return false
        await process_frame
    phase_log.append("phase A: session survived 60 ticks after close() veto")

    # Phase 2 — key down flips the decision and closes again; the runtime
    # must request termination now.
    var key_result: int = player.send_key_event(true, 65, 0, 0)
    if key_result != 0:
        printerr("phase A key event failed: %s" % player.get_last_error())
        player.destroy_engine()
        _retire_player(player, false)
        return false
    var terminated := false
    for _frame in range(120):
        var result: int = player.tick(1.0 / 60.0)
        if result != 0:
            var message := String(player.get_last_error())
            if message.contains("runtime requested termination") or message.contains("runtime has been terminated"):
                terminated = true
                break
            printerr("phase A allow failed: %s" % message)
            player.destroy_engine()
            _retire_player(player, false)
            return false
        await process_frame
    if not terminated:
        printerr("phase A allow timed out")
        player.destroy_engine()
        _retire_player(player, false)
        return false
    phase_log.append("phase A: allowed close ended the session as requested")

    player.destroy_engine()
    _retire_player(player, true)
    return true


func _run_kag3_shutdown(game_path: String) -> bool:
    # KAG writes system/user data to System.dataPath, which the engine
    # defaults to "<game root>/savedata" for directory games.
    var savedata_dir := game_path.path_join("savedata")
    var system_save := savedata_dir.path_join("datasc.ksd")
    var user_save := savedata_dir.path_join("datasu.ksd")
    DirAccess.remove_absolute(system_save)
    DirAccess.remove_absolute(user_save)

    var player = _make_player()
    if not await _open_and_start(player, game_path, "close-query-smoke-b"):
        _retire_player(player, false)
        return false

    var terminated := false
    for _frame in range(600):
        var result: int = player.tick(1.0 / 60.0)
        if result != 0:
            var message := String(player.get_last_error())
            if message.contains("runtime requested termination") or message.contains("runtime has been terminated"):
                terminated = true
                break
            printerr("phase B exit failed: %s" % message)
            player.destroy_engine()
            _retire_player(player, false)
            return false
        await process_frame
    if not terminated:
        printerr("phase B exit timed out")
        player.destroy_engine()
        _retire_player(player, false)
        return false
    phase_log.append("phase B: kag.shutdown() terminated the session")

    if not FileAccess.file_exists(system_save):
        printerr("phase B: system data not written at exit: %s" % system_save)
        player.destroy_engine()
        _retire_player(player, false)
        return false
    phase_log.append("phase B: system data written at exit (datasc.ksd)")
    if FileAccess.file_exists(user_save):
        phase_log.append("phase B: user data written at exit (datasu.ksd)")

    player.destroy_engine()
    _retire_player(player, false)
    return true
