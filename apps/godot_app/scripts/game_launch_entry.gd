extends RefCounted

const FIELD := "launchFile"
const SUPPORTED_EXTENSIONS := ["exe", "xp3"]
const DIRECTORY_RUNTIME_KINDS := ["artemis", "minori", "onscripter", "siglus"]


static func configured_relative_path(game: Dictionary) -> String:
    var relative_path := _normalize_path(String(game.get(FIELD, "")).strip_edges())
    if relative_path.is_empty() or relative_path.is_absolute_path():
        return ""
    relative_path = relative_path.simplify_path()
    if relative_path == "." or relative_path == ".." or relative_path.begins_with("../"):
        return ""
    if not is_supported_file(relative_path):
        return ""
    return relative_path


static func resolve(game: Dictionary) -> String:
    var game_path := _normalize_path(String(game.get("path", "")).strip_edges())
    var relative_path := configured_relative_path(game)
    if game_path.is_empty() or relative_path.is_empty():
        return game_path
    return game_path.path_join(relative_path).simplify_path()


static func resolve_for_runtime(game: Dictionary, runtime_kind: String) -> String:
    if runtime_uses_directory(runtime_kind):
        return _normalize_path(String(game.get("path", "")).strip_edges()).simplify_path()
    return resolve(game)


static func runtime_uses_directory(runtime_kind: String) -> bool:
    return DIRECTORY_RUNTIME_KINDS.has(runtime_kind.to_lower())


static func is_supported_file(path: String) -> bool:
    return SUPPORTED_EXTENSIONS.has(path.get_extension().to_lower())


static func relative_path_for_selection(game_path: String, selected_path: String) -> String:
    var root := _normalize_path(game_path.strip_edges()).simplify_path().trim_suffix("/")
    var selected := _normalize_path(selected_path.strip_edges()).simplify_path()
    if root.is_empty() or selected.is_empty() or not is_supported_file(selected):
        return ""
    var root_prefix := root + "/"
    if not selected.begins_with(root_prefix):
        return ""
    var relative_path := selected.substr(root_prefix.length())
    if relative_path.is_empty() or relative_path == ".." or relative_path.begins_with("../"):
        return ""
    return relative_path


static func _normalize_path(path: String) -> String:
    return path.replace("\\", "/")
