extends SceneTree

const BuiltinDemo = preload("res://scripts/builtin_demo.gd")

var test_root := ""

func _init() -> void:
    call_deferred("_run")

func _run() -> void:
    if not _validate_product_asset():
        return
    test_root = "user://builtin-demo-test-%d" % Time.get_ticks_usec()
    var source_path := test_root.path_join("source.xp3")
    var install_dir := test_root.path_join("installed")
    var external_save_dir := test_root.path_join("web-savedata")
    var state_path := test_root.path_join("state.cfg")
    DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(test_root))
    if not _write_bytes(source_path, PackedByteArray([1, 2, 3, 4])):
        _fail("could not create source fixture")
        return

    var demo = BuiltinDemo.new(source_path, install_dir, state_path, 7)
    var games: Array[Dictionary] = []
    games = demo.reconcile_games(games)
    if games.size() != 1 or not demo.is_game(games[0]):
        _fail("first launch did not seed the built-in demo")
        return
    if _read_bytes(demo.game_path()) != PackedByteArray([1, 2, 3, 4]):
        _fail("installed demo does not match the bundled resource")
        return

    games[0]["lastPlayed"] = 42
    games[0]["playDurationSeconds"] = 73
    games[0]["title"] = "Renamed Demo"
    games = demo.reconcile_games(games)
    if int(games[0].get("lastPlayed", 0)) != 42 or \
            int(games[0].get("playDurationSeconds", 0)) != 73 or \
            String(games[0].get("title", "")) != "Renamed Demo":
        _fail("reconciliation discarded player metadata")
        return

    if not _write_bytes(source_path, PackedByteArray([9, 8, 7, 6, 5])):
        _fail("could not update source fixture")
        return
    var upgraded_demo = BuiltinDemo.new(
        source_path,
        install_dir,
        state_path,
        8,
        PackedStringArray([external_save_dir])
    )
    games = upgraded_demo.reconcile_games(games)
    if _read_bytes(upgraded_demo.game_path()) != PackedByteArray([9, 8, 7, 6, 5]):
        _fail("a bundled demo version update did not replace the installed copy")
        return

    var save_dir := install_dir.path_join("savedata")
    DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(save_dir))
    if not _write_bytes(save_dir.path_join("save0.dat"), PackedByteArray([4, 2])):
        _fail("could not create save fixture")
        return
    DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(external_save_dir))
    if not _write_bytes(external_save_dir.path_join("save1.dat"), PackedByteArray([2, 4])):
        _fail("could not create external Web save fixture")
        return
    if upgraded_demo.remove_install() != OK:
        _fail("built-in demo deletion failed")
        return
    if DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(install_dir)) or \
            DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(external_save_dir)):
        _fail("built-in demo files or saves remain after deletion")
        return
    games = upgraded_demo.reconcile_games(games)
    if not games.is_empty() or not upgraded_demo.is_removed():
        _fail("deleted demo was restored during the same session")
        return
    DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(install_dir))
    DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(external_save_dir))
    if not _write_bytes(install_dir.path_join("leftover.tmp"), PackedByteArray([1])) or \
            not _write_bytes(external_save_dir.path_join("leftover.sav"), PackedByteArray([2])):
        _fail("could not create retry cleanup fixtures")
        return
    var restarted_demo = BuiltinDemo.new(
        source_path,
        install_dir,
        state_path,
        9,
        PackedStringArray([external_save_dir])
    )
    if not restarted_demo.reconcile_games(games).is_empty() or FileAccess.file_exists(restarted_demo.game_path()):
        _fail("deleted demo was restored after restart or upgrade")
        return
    if DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(install_dir)) or \
            DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(external_save_dir)):
        _fail("a later reconciliation did not retry removal of leftover files")
        return

    _cleanup()
    print("builtin_demo_test: PASS")
    quit(0)

func _validate_product_asset() -> bool:
    var manifest_file := FileAccess.open(
        "res://builtin_demos/aetherkiri-kag3/manifest.json",
        FileAccess.READ
    )
    if manifest_file == null:
        _fail("built-in demo manifest is missing")
        return false
    var manifest = JSON.parse_string(manifest_file.get_as_text())
    if not manifest is Dictionary:
        _fail("built-in demo manifest is invalid")
        return false
    if String(manifest.get("id", "")) != BuiltinDemo.DEMO_ID or \
            int(manifest.get("version", 0)) != BuiltinDemo.DEMO_VERSION:
        _fail("built-in demo manifest identity or version is out of sync")
        return false
    var source := FileAccess.open(BuiltinDemo.DEFAULT_SOURCE_PATH, FileAccess.READ)
    if source == null:
        _fail("built-in demo product archive is missing")
        return false
    if source.get_length() != int(manifest.get("size", -1)):
        _fail("built-in demo product size does not match its manifest")
        return false
    var context := HashingContext.new()
    context.start(HashingContext.HASH_SHA256)
    while source.get_position() < source.get_length():
        var remaining := source.get_length() - source.get_position()
        context.update(source.get_buffer(mini(BuiltinDemo.COPY_BUFFER_SIZE, remaining)))
    var digest := context.finish().hex_encode()
    if digest != String(manifest.get("sha256", "")):
        _fail("built-in demo product SHA-256 does not match its manifest")
        return false
    return true

func _write_bytes(path: String, bytes: PackedByteArray) -> bool:
    var file := FileAccess.open(path, FileAccess.WRITE)
    if file == null:
        return false
    file.store_buffer(bytes)
    return true

func _read_bytes(path: String) -> PackedByteArray:
    var file := FileAccess.open(path, FileAccess.READ)
    if file == null:
        return PackedByteArray()
    return file.get_buffer(file.get_length())

func _cleanup() -> void:
    if test_root.is_empty():
        return
    var demo = BuiltinDemo.new("", test_root, test_root.path_join("unused.cfg"), 0)
    demo._remove_tree_absolute(ProjectSettings.globalize_path(test_root))

func _fail(message: String) -> void:
    _cleanup()
    push_error("builtin_demo_test: %s" % message)
    quit(1)
