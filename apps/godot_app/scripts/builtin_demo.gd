extends RefCounted

const DEMO_ID := "aetherkiri-kag3-demo"
const DEMO_VERSION := 4
const DEFAULT_SOURCE_PATH := "res://builtin_demos/aetherkiri-kag3/data.xp3"
const DEFAULT_INSTALL_DIR := "user://builtin_games/aetherkiri-demo"
const DEFAULT_INSTALL_PATH := DEFAULT_INSTALL_DIR + "/data.xp3"
const DEFAULT_STATE_PATH := "user://aetherkiri_builtin_demo.cfg"
const DEFAULT_WEB_SAVE_DIR := "/userfs/aetherkiri/savedata/aetherkiri-demo"
const COPY_BUFFER_SIZE := 1024 * 1024

var source_path: String
var install_dir: String
var install_path: String
var state_path: String
var version: int
var additional_cleanup_dirs: PackedStringArray

func _init(
        source: String = DEFAULT_SOURCE_PATH,
        destination_dir: String = DEFAULT_INSTALL_DIR,
        state_file: String = DEFAULT_STATE_PATH,
        demo_version: int = DEMO_VERSION,
        cleanup_dirs: PackedStringArray = PackedStringArray()) -> void:
    source_path = source
    install_dir = destination_dir.trim_suffix("/")
    install_path = install_dir.path_join("data.xp3")
    state_path = state_file
    version = demo_version
    additional_cleanup_dirs = cleanup_dirs.duplicate()
    if OS.get_name() == "Web" and not additional_cleanup_dirs.has(DEFAULT_WEB_SAVE_DIR):
        additional_cleanup_dirs.append(DEFAULT_WEB_SAVE_DIR)

func game_path() -> String:
    return ProjectSettings.globalize_path(install_path)

func is_game(game: Dictionary) -> bool:
    return String(game.get("builtinId", "")) == DEMO_ID or is_path(String(game.get("path", "")))

func is_path(path: String) -> bool:
    if path.is_empty():
        return false
    return path.simplify_path() == game_path().simplify_path()

func is_removed() -> bool:
    var state := _load_state()
    return bool(state.get_value("demo", "removed", false))

func ensure_installed() -> Dictionary:
    var state := _load_state()
    if bool(state.get_value("demo", "removed", false)):
        var cleanup_result := _cleanup_removed_files()
        if cleanup_result != OK:
            push_warning("Could not finish cleaning removed built-in demo: %s" % error_string(cleanup_result))
        return {}
    var source := FileAccess.open(source_path, FileAccess.READ)
    if source == null:
        push_error("Built-in demo resource is unavailable: %s" % source_path)
        return {}
    var source_size := source.get_length()
    source = null
    var installed_version := int(state.get_value("demo", "installed_version", 0))
    var install_current := FileAccess.file_exists(install_path) and \
            FileAccess.get_size(install_path) == source_size and installed_version == version
    if not install_current:
        var copy_result := _copy_file_atomic(source_path, install_path)
        if copy_result != OK:
            push_error("Could not install built-in demo: %s" % error_string(copy_result))
            return {}
    var state_needs_save := not FileAccess.file_exists(state_path) or \
            bool(state.get_value("demo", "removed", false)) or \
            int(state.get_value("demo", "installed_version", 0)) != version or \
            int(state.get_value("demo", "installed_size", -1)) != source_size
    if state_needs_save:
        state.set_value("demo", "removed", false)
        state.set_value("demo", "installed_version", version)
        state.set_value("demo", "installed_size", source_size)
        if state.save(state_path) != OK:
            push_warning("Could not save built-in demo state: %s" % state_path)
    return _game_dictionary()

func reconcile_games(games: Array[Dictionary]) -> Array[Dictionary]:
    var installed := ensure_installed()
    var existing_demo: Dictionary = {}
    var result: Array[Dictionary] = []
    for game in games:
        if is_game(game):
            if existing_demo.is_empty():
                existing_demo = game
        else:
            result.append(game)
    if installed.is_empty():
        return result
    if not existing_demo.is_empty():
        for key in ["lastPlayed", "playDurationSeconds", "coverPath", "title"]:
            if existing_demo.has(key):
                installed[key] = existing_demo[key]
    result.append(installed)
    return result

func remove_install() -> Error:
    var state := _load_state()
    state.set_value("demo", "removed", true)
    state.set_value("demo", "installed_version", version)
    var state_result := state.save(state_path)
    if state_result != OK:
        return state_result
    return _cleanup_removed_files()

func _cleanup_removed_files() -> Error:
    var cleanup_dirs := PackedStringArray([install_dir])
    cleanup_dirs.append_array(additional_cleanup_dirs)
    var first_error: Error = OK
    var visited := {}
    for cleanup_dir in cleanup_dirs:
        var absolute_path := ProjectSettings.globalize_path(cleanup_dir).simplify_path()
        if visited.has(absolute_path):
            continue
        visited[absolute_path] = true
        var cleanup_result := _remove_tree_absolute(absolute_path)
        if cleanup_result != OK and first_error == OK:
            first_error = cleanup_result
    return first_error

func _game_dictionary() -> Dictionary:
    return {
        "name": "AetherKiri Demo",
        "path": game_path(),
        "type": "Archive",
        "lastPlayed": 0,
        "playDurationSeconds": 0,
        "coverPath": "",
        "developer": "AetherKiri",
        "title": "AetherKiri Demo",
        "builtinId": DEMO_ID,
        "builtinVersion": version,
    }

func _load_state() -> ConfigFile:
    var state := ConfigFile.new()
    state.load(state_path)
    return state

func _copy_file_atomic(source_file: String, destination_file: String) -> Error:
    var mkdir_result := DirAccess.make_dir_recursive_absolute(
        ProjectSettings.globalize_path(destination_file.get_base_dir())
    )
    if mkdir_result != OK:
        return mkdir_result
    var source := FileAccess.open(source_file, FileAccess.READ)
    if source == null:
        return FileAccess.get_open_error()
    var temporary_file := destination_file + ".tmp"
    var backup_file := destination_file + ".bak"
    _remove_file_if_present(temporary_file)
    _remove_file_if_present(backup_file)
    var destination := FileAccess.open(temporary_file, FileAccess.WRITE)
    if destination == null:
        return FileAccess.get_open_error()
    var expected_size := source.get_length()
    while source.get_position() < source.get_length():
        var remaining := source.get_length() - source.get_position()
        destination.store_buffer(source.get_buffer(mini(COPY_BUFFER_SIZE, remaining)))
    destination.flush()
    var write_result := destination.get_error()
    destination = null
    source = null
    if write_result != OK or FileAccess.get_size(temporary_file) != expected_size:
        _remove_file_if_present(temporary_file)
        return write_result if write_result != OK else ERR_FILE_CORRUPT
    var destination_absolute := ProjectSettings.globalize_path(destination_file)
    var temporary_absolute := ProjectSettings.globalize_path(temporary_file)
    var backup_absolute := ProjectSettings.globalize_path(backup_file)
    var had_previous := FileAccess.file_exists(destination_file)
    if had_previous:
        var backup_result := DirAccess.rename_absolute(destination_absolute, backup_absolute)
        if backup_result != OK:
            _remove_file_if_present(temporary_file)
            return backup_result
    var install_result := DirAccess.rename_absolute(temporary_absolute, destination_absolute)
    if install_result != OK:
        if had_previous:
            DirAccess.rename_absolute(backup_absolute, destination_absolute)
        _remove_file_if_present(temporary_file)
        return install_result
    _remove_file_if_present(backup_file)
    return OK

func _remove_file_if_present(path: String) -> Error:
    if not FileAccess.file_exists(path):
        return OK
    return DirAccess.remove_absolute(ProjectSettings.globalize_path(path))

func _remove_tree_absolute(path: String) -> Error:
    if path.strip_edges().is_empty() or path == "/":
        return ERR_INVALID_PARAMETER
    if not DirAccess.dir_exists_absolute(path):
        return OK
    var directory := DirAccess.open(path)
    if directory == null:
        return DirAccess.get_open_error()
    var first_error: Error = OK
    directory.list_dir_begin()
    var entry := directory.get_next()
    while not entry.is_empty():
        if entry != "." and entry != "..":
            var child := path.path_join(entry)
            var result := _remove_tree_absolute(child) if directory.current_is_dir() else DirAccess.remove_absolute(child)
            if result != OK and first_error == OK:
                first_error = result
        entry = directory.get_next()
    directory.list_dir_end()
    if first_error != OK:
        return first_error
    return DirAccess.remove_absolute(path)
