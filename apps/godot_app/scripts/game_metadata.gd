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
    var files := _file_names(root)
    if files.has("nscript.dat") or files.has("0.txt") or _has_prefix(files, "onscript.nt"):
        result.engine = RUNTIME_ONSCRIPTER
        result.signals.append("onscript-marker")
    elif files.has("system.ini") and _is_artemis_package(
        files,
        _read_text(root.path_join("system.ini"))
    ):
        result.engine = RUNTIME_ARTEMIS
        result.signals.append("artemis-system-ini")
        var ini := _read_text(root.path_join("system.ini"))
        if (
            _has_extension(files, "pfs")
            and _value_after(ini, "BOOT").to_lower().ends_with(".iet")
        ):
            result.signals.append("artemis-pfs-boot")
        var save_path := _value_after(ini, "SAVEPATH")
        if not save_path.is_empty():
            candidates.append(save_path.replace("\\\\", "/").get_file())
    elif files.has("gameexe.dat"):
        result.engine = RUNTIME_SIGLUS
        result.signals.append("siglus-gameexe")
    else:
        result.signals.append("kirikiri-xp3-or-default")

    if result.engine == RUNTIME_KIRIKIRI:
        var launch := _first_file(root, [".exe", ".xp3"])
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
    var executable_fallback := ""
    var archive_fallback := ""
    dir.list_dir_begin()
    var entry := dir.get_next()
    while not entry.is_empty():
        if not dir.current_is_dir():
            var lower := entry.to_lower()
            if extensions.has("." + lower.get_extension()):
                # A KiriKiri Windows executable is not a readable storage
                # file on macOS/iOS. Prefer the game's data archive whenever
                # both a launcher and an XP3 archive are present.
                if lower == "data.xp3":
                    dir.list_dir_end()
                    return entry
                if lower.ends_with(".xp3"):
                    if archive_fallback.is_empty():
                        archive_fallback = entry
                elif executable_fallback.is_empty():
                    executable_fallback = entry
        entry = dir.get_next()
    dir.list_dir_end()
    if not archive_fallback.is_empty():
        return archive_fallback
    return executable_fallback

static func _has_prefix(values: PackedStringArray, prefix: String) -> bool:
    for value in values:
        if value.begins_with(prefix):
            return true
    return false

static func _has_extension(values: PackedStringArray, extension: String) -> bool:
    var normalized := extension.to_lower().trim_prefix(".")
    for value in values:
        if value.get_extension() == normalized:
            return true
    return false

static func _is_artemis_package(files: PackedStringArray, ini: String) -> bool:
    if ini.findn("Artemis") >= 0:
        return true
    var boot := _value_after(ini, "BOOT").replace("\\", "/")
    return _has_extension(files, "pfs") and boot.to_lower().ends_with(".iet")

static func _read_text(path: String) -> String:
    var file := FileAccess.open(path, FileAccess.READ)
    if file == null or file.get_length() > 1024 * 1024:
        return ""
    return file.get_as_text()

static func _value_after(text: String, key: String) -> String:
    for line in text.split("\n"):
        var trimmed := line.strip_edges()
        var equals := trimmed.find("=")
        if equals >= 0 and trimmed.left(equals).strip_edges().to_upper() == key.to_upper():
            return trimmed.substr(equals + 1).strip_edges()
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
