class_name CoverIndex
extends RefCounted

const FILE_PATH := "user://aetherkiri_covers_v2.json"
const METADATA_VERSION := 2

static func load_index() -> Dictionary:
    var file := FileAccess.open(FILE_PATH, FileAccess.READ)
    if file == null:
        return {}
    var parsed = JSON.parse_string(file.get_as_text())
    if not parsed is Dictionary:
        return {}
    var result := {}
    for key in parsed:
        var value = parsed[key]
        if value is Dictionary:
            result[key] = value
        else:
            result[key] = {"recognized": true, "coverPath": String(value), "metadataVersion": 0}
    return result

static func record(cover_path: String, recognized: bool = true) -> Dictionary:
    return {"recognized": recognized, "coverPath": cover_path, "metadataVersion": METADATA_VERSION}

static func needs_recognition(index: Dictionary, key: String) -> bool:
    var value = index.get(key, null)
    return not value is Dictionary or not bool(value.get("recognized", false)) or int(value.get("metadataVersion", 0)) < METADATA_VERSION

static func save_index(index: Dictionary) -> void:
    var temporary_path := FILE_PATH + ".tmp"
    var file := FileAccess.open(temporary_path, FileAccess.WRITE)
    if file != null:
        file.store_string(JSON.stringify(index, "  "))
        file.close()
        DirAccess.rename_absolute(ProjectSettings.globalize_path(temporary_path), ProjectSettings.globalize_path(FILE_PATH))

static func key_for(game: Dictionary) -> String:
    var title := String(game.get("name", game.get("title", ""))).strip_edges()
    return title if not title.is_empty() else String(game.get("path", "")).get_file()
