class_name CoverIndex
extends RefCounted

const FILE_PATH := "user://aetherkiri_covers.json"

static func load_index() -> Dictionary:
    var file := FileAccess.open(FILE_PATH, FileAccess.READ)
    if file == null:
        return {}
    var parsed = JSON.parse_string(file.get_as_text())
    return parsed if parsed is Dictionary else {}

static func save_index(index: Dictionary) -> void:
    var file := FileAccess.open(FILE_PATH, FileAccess.WRITE)
    if file != null:
        file.store_string(JSON.stringify(index, "  "))

static func key_for(game: Dictionary) -> String:
    var title := String(game.get("name", game.get("title", ""))).strip_edges()
    return title if not title.is_empty() else String(game.get("path", "")).get_file()
