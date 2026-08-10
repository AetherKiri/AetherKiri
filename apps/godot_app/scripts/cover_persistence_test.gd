extends SceneTree

const MAIN_SCRIPT = preload("res://scripts/main.gd")

func _init() -> void:
    call_deferred("_run")

func _run() -> void:
    var app = MAIN_SCRIPT.new()
    var test_name := "aether-cover-test-%d" % Time.get_ticks_usec()
    var test_root := ProjectSettings.globalize_path("user://").path_join(test_name)
    if DirAccess.make_dir_recursive_absolute(test_root) != OK:
        _fail(app, test_root, "could not create test root")
        return

    var localized_names := [
        "cover", "封面", "表紙", "カバー", "표지",
        "background", "背景", "壁紙", "배경",
    ]
    for index in range(localized_names.size()):
        var candidate_root := test_root.path_join("localized-%d" % index)
        DirAccess.make_dir_recursive_absolute(candidate_root)
        var image_path := candidate_root.path_join("%s.png" % localized_names[index])
        if not _write_image(image_path):
            _fail(app, test_root, "could not write %s" % image_path)
            return
        if String(app._discover_default_cover_path(candidate_root)) != image_path:
            _fail(app, test_root, "localized default was not discovered: %s" % localized_names[index])
            return

    var current_custom_path := test_root.path_join("custom.png")
    if not _write_image(current_custom_path):
        _fail(app, test_root, "could not write migrated custom cover")
        return
    var old_container_path := "/private/var/mobile/Containers/Data/Application/OLD/Documents/%s/custom.png" % test_name
    var migrated_game := {
        "name": "Migrated",
        "path": test_root,
        "coverPath": old_container_path,
    }
    if String(app._resolve_cover_path(migrated_game)) != current_custom_path:
        _fail(app, test_root, "old iOS container path was not migrated")
        return

    var game_root := test_root.path_join("Game")
    DirAccess.make_dir_recursive_absolute(game_root)
    var default_background := game_root.path_join("背景.png")
    if not _write_image(default_background):
        _fail(app, test_root, "could not write default background")
        return
    var games: Array[Dictionary] = [{
        "name": "Game",
        "path": game_root,
        "coverPath": "/private/var/mobile/Containers/Data/Application/OLD/Documents/Covers/missing.png",
        "_autoCoverScanned": true,
    }]
    if not bool(app._backfill_default_game_covers(games)):
        _fail(app, test_root, "stale scanned cover was not repaired")
        return
    if String(games[0].get("coverPath", "")) != "game://背景.png":
        _fail(app, test_root, "default background was not stored relative to the game")
        return
    if String(app._resolve_cover_path(games[0])) != default_background:
        _fail(app, test_root, "game-relative cover did not resolve")
        return
    if app._load_cover_texture(games[0]) == null:
        _fail(app, test_root, "resolved default background did not load")
        return
    games[0]["title"] = "A customized display title"
    var indexed_games: Dictionary = app._games_by_library_entry(games, test_root)
    if not indexed_games.has("Game") \
            or String(indexed_games["Game"].get("coverPath", "")) != "game://背景.png":
        _fail(app, test_root, "iOS rescan did not preserve metadata by directory entry")
        return

    _remove_tree(test_root)
    app.cover_texture_cache.clear()
    app.free()
    print("cover_persistence_test: PASS")
    quit(0)

func _write_image(path: String) -> bool:
    var image := Image.create(4, 4, false, Image.FORMAT_RGBA8)
    image.fill(Color(0.25, 0.5, 0.75, 1.0))
    return image.save_png(path) == OK

func _remove_tree(path: String) -> void:
    var dir := DirAccess.open(path)
    if dir == null:
        return
    for file_name in dir.get_files():
        DirAccess.remove_absolute(path.path_join(file_name))
    for directory_name in dir.get_directories():
        _remove_tree(path.path_join(directory_name))
    DirAccess.remove_absolute(path)

func _fail(app, test_root: String, message: String) -> void:
    _remove_tree(test_root)
    app.cover_texture_cache.clear()
    app.free()
    push_error("cover_persistence_test: %s" % message)
    quit(1)
