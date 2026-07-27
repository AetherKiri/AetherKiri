class_name VideoSubtitles
extends RefCounted

static var _html_tag_regex: RegEx
static var _ass_tag_regex: RegEx

static func parse_file(path: String) -> Array[Dictionary]:
    var file := FileAccess.open(path, FileAccess.READ)
    if file == null:
        return []
    var text := file.get_as_text().replace("\r\n", "\n").replace("\r", "\n")
    var extension := path.get_extension().to_lower()
    if extension in ["ass", "ssa"]:
        return _parse_ass(text)
    return _parse_srt_or_vtt(text)

static func text_at(cues: Array[Dictionary], seconds: float, hint_index: int = 0) -> Dictionary:
    if cues.is_empty():
        return {"index": 0, "text": ""}
    var index := clampi(hint_index, 0, cues.size() - 1)
    while index > 0 and seconds < float(cues[index].get("start", 0.0)):
        index -= 1
    while index + 1 < cues.size() and seconds >= float(cues[index].get("end", 0.0)):
        index += 1
    var cue: Dictionary = cues[index]
    var visible := seconds >= float(cue.get("start", 0.0)) and seconds < float(cue.get("end", 0.0))
    return {"index": index, "text": String(cue.get("text", "")) if visible else ""}

static func _parse_srt_or_vtt(text: String) -> Array[Dictionary]:
    var cues: Array[Dictionary] = []
    var lines := text.split("\n")
    var index := 0
    while index < lines.size():
        var line := String(lines[index]).strip_edges()
        if line.is_empty() or line == "WEBVTT" or line.begins_with("NOTE"):
            index += 1
            continue
        if not line.contains("-->"):
            index += 1
            if index >= lines.size():
                break
            line = String(lines[index]).strip_edges()
        if not line.contains("-->"):
            continue
        var timing := line.split("-->", false, 1)
        if timing.size() != 2:
            index += 1
            continue
        var start := _parse_time(String(timing[0]).strip_edges())
        var end_token := String(timing[1]).strip_edges().split(" ", false, 1)[0]
        var end := _parse_time(end_token)
        index += 1
        var body: Array[String] = []
        while index < lines.size() and not String(lines[index]).strip_edges().is_empty():
            body.append(String(lines[index]).strip_edges())
            index += 1
        var cue_text := _clean_text("\n".join(body))
        if end > start and not cue_text.is_empty():
            cues.append({"start": start, "end": end, "text": cue_text})
    return cues

static func _parse_ass(text: String) -> Array[Dictionary]:
    var cues: Array[Dictionary] = []
    for raw_line in text.split("\n"):
        var line := String(raw_line).strip_edges()
        if not line.begins_with("Dialogue:"):
            continue
        var fields := line.trim_prefix("Dialogue:").split(",", true, 9)
        if fields.size() < 10:
            continue
        var start := _parse_time(String(fields[1]).strip_edges())
        var end := _parse_time(String(fields[2]).strip_edges())
        var cue_text := _clean_ass_text(String(fields[9]))
        if end > start and not cue_text.is_empty():
            cues.append({"start": start, "end": end, "text": cue_text})
    cues.sort_custom(func(a: Dictionary, b: Dictionary):
        return float(a.get("start", 0.0)) < float(b.get("start", 0.0))
    )
    return cues

static func _parse_time(value: String) -> float:
    var normalized := value.replace(",", ".").strip_edges()
    var parts := normalized.split(":")
    if parts.size() == 3:
        return float(parts[0]) * 3600.0 + float(parts[1]) * 60.0 + float(parts[2])
    if parts.size() == 2:
        return float(parts[0]) * 60.0 + float(parts[1])
    return normalized.to_float()

static func _clean_text(value: String) -> String:
    if _html_tag_regex == null:
        _html_tag_regex = RegEx.new()
        _html_tag_regex.compile("<[^>]+>")
    return _html_tag_regex.sub(value, "", true).replace("&nbsp;", " ").strip_edges()

static func _clean_ass_text(value: String) -> String:
    if _ass_tag_regex == null:
        _ass_tag_regex = RegEx.new()
        _ass_tag_regex.compile("\\{[^}]*\\}")
    return _ass_tag_regex.sub(value, "", true).replace("\\N", "\n").replace("\\n", "\n").strip_edges()
