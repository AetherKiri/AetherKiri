class_name GameMetadata
extends RefCounted

const RUNTIME_KIRIKIRI := "kirikiri"
const RUNTIME_ONSCRIPTER := "onscripter"
const RUNTIME_ARTEMIS := "artemis"
const RUNTIME_SIGLUS := "siglus"

static func inspect(path: String) -> Dictionary:
    var root := path
    if FileAccess.file_exists(root):
        root = root.get_base_dir()
    var result := {
        "engine": RUNTIME_KIRIKIRI,
        "title": "",
        "titleCandidates": PackedStringArray(),
        "launchFile": "",
        "signals": PackedStringArray(),
    }
    if root.is_empty() or not DirAccess.dir_exists_absolute(root):
        return result

    var candidates: PackedStringArray = []
    var launch := _first_file(root, [".exe", ".xp3"])
    var files := _file_names(root)
    if files.has("nscript.dat") or files.has("0.txt") or _has_prefix(files, "onscript.nt"):
        result.engine = RUNTIME_ONSCRIPTER
        result.signals.append("onscript-marker")
    elif files.has("system.ini") and _contains_text(root.path_join("system.ini"), "Artemis"):
        result.engine = RUNTIME_ARTEMIS
        result.signals.append("artemis-system-ini")
        var ini := _read_text(root.path_join("system.ini"))
        var save_path := _value_after(ini, "SAVEPATH")
        if not save_path.is_empty():
            candidates.append(save_path.replace("\\\\", "/").get_file())
    elif files.has("gameexe.dat"):
        result.engine = RUNTIME_SIGLUS
        result.signals.append("siglus-gameexe")
    else:
        result.signals.append("kirikiri-xp3-or-default")

    if not launch.is_empty():
        result.launchFile = launch
    for marker in ["README.md", "title.ini", "title.ks", "startup.tjs", "config.tjs"]:
        var marker_path := root.path_join(marker)
        if FileAccess.file_exists(marker_path):
            var text := _read_text(marker_path)
            candidates.append_array(_extract_title_candidates(text))
            result.signals.append("content:" + marker)
    candidates.append_array(_title_from_path(root))
    candidates = _unique_nonempty(candidates)
    result.titleCandidates = candidates
    if not candidates.is_empty():
        result.title = candidates[0]
    return result

static func _file_names(root: String) -> PackedStringArray:
    var result: PackedStringArray = []
    var dir := DirAccess.open(root)
    if dir == null:
        return result
    dir.list_dir_begin()
    var entry := dir.get_next()
    while not entry.is_empty():
        if not dir.current_is_dir():
            result.append(entry.to_lower())
        entry = dir.get_next()
    dir.list_dir_end()
    return result

static func _first_file(root: String, extensions: Array) -> String:
    var dir := DirAccess.open(root)
    if dir == null:
        return ""
    var fallback := ""
    dir.list_dir_begin()
    var entry := dir.get_next()
    while not entry.is_empty():
        if not dir.current_is_dir():
            var lower := entry.to_lower()
            if extensions.has("." + lower.get_extension()):
                if lower.ends_with(".exe"):
                    return entry
                if fallback.is_empty():
                    fallback = entry
        entry = dir.get_next()
    dir.list_dir_end()
    return fallback

static func _has_prefix(values: PackedStringArray, prefix: String) -> bool:
    for value in values:
        if value.begins_with(prefix):
            return true
    return false

static func _read_text(path: String) -> String:
    var file := FileAccess.open(path, FileAccess.READ)
    if file == null or file.get_length() > 1024 * 1024:
        return ""
    return file.get_as_text()

static func _contains_text(path: String, needle: String) -> bool:
    return _read_text(path).findn(needle) >= 0

static func _value_after(text: String, key: String) -> String:
    for line in text.split("\n"):
        var trimmed := line.strip_edges()
        if trimmed.begins_with(key) and trimmed.find("=") >= 0:
            return trimmed.substr(trimmed.find("=") + 1).strip_edges()
    return ""

static func _extract_title_candidates(text: String) -> PackedStringArray:
    var result: PackedStringArray = []
    for line in text.split("\n"):
        var value := line.strip_edges()
        if value.begins_with("#") or value.begins_with(";") or value.length() < 3:
            continue
        for key in ["title", "name", "game", "product"]:
            if value.to_lower().begins_with(key + "="):
                var candidate := value.substr(value.find("=") + 1).strip_edges()
                if candidate.length() >= 3 and candidate.length() <= 160:
                    result.append(candidate)
    return result

static func _title_from_path(root: String) -> PackedStringArray:
    var result: PackedStringArray = []
    var name := root.get_file().replace("_aetherkiri", "").strip_edges()
    if not name.is_empty():
        result.append(name)
    return result

static func _unique_nonempty(values: PackedStringArray) -> PackedStringArray:
    var result: PackedStringArray = []
    var seen := {}
    for value in values:
        var normalized := value.strip_edges()
        if normalized.is_empty() or seen.has(normalized):
            continue
        seen[normalized] = true
        result.append(normalized)
    return result
