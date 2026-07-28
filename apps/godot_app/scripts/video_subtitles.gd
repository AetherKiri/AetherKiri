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
    while index + 1 < cues.size() and seconds >= float(cues[index + 1].get("start", 0.0)):
        index += 1
    while index > 0 and seconds < float(cues[index].get("start", 0.0)):
        index -= 1
    if seconds < float(cues[index].get("start", 0.0)):
        return {"index": index, "text": ""}
    var first_visible_candidate := index
    while first_visible_candidate > 0 and float(
        cues[first_visible_candidate - 1].get(
            "_max_end",
            cues[first_visible_candidate - 1].get("end", 0.0)
        )
    ) > seconds:
        first_visible_candidate -= 1
    var visible_texts: Array[String] = []
    for cue_index in range(first_visible_candidate, index + 1):
        var cue: Dictionary = cues[cue_index]
        if seconds < float(cue.get("start", 0.0)) or seconds >= float(cue.get("end", 0.0)):
            continue
        var cue_text := String(cue.get("text", ""))
        if not cue_text.is_empty() and not visible_texts.has(cue_text):
            visible_texts.append(cue_text)
    return {"index": index, "text": "\n".join(visible_texts)}

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
            cues.append({
                "start": start,
                "end": end,
                "text": cue_text,
                "_order": cues.size(),
            })
    return _prepare_cues(cues)

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
            cues.append({
                "start": start,
                "end": end,
                "text": cue_text,
                "_order": cues.size(),
            })
    return _prepare_cues(cues)

static func _prepare_cues(cues: Array[Dictionary]) -> Array[Dictionary]:
    cues.sort_custom(func(a: Dictionary, b: Dictionary):
        var a_start := float(a.get("start", 0.0))
        var b_start := float(b.get("start", 0.0))
        if not is_equal_approx(a_start, b_start):
            return a_start < b_start
        return int(a.get("_order", 0)) < int(b.get("_order", 0))
    )
    var merged: Array[Dictionary] = []
    for cue in cues:
        var cue_text := String(cue.get("text", ""))
        if not merged.is_empty():
            var previous: Dictionary = merged[merged.size() - 1]
            if is_equal_approx(
                float(previous.get("start", 0.0)),
                float(cue.get("start", 0.0))
            ) and is_equal_approx(
                float(previous.get("end", 0.0)),
                float(cue.get("end", 0.0))
            ):
                var variants: Array = previous.get("_variants", [])
                if not variants.has(cue_text):
                    variants.append(cue_text)
                    previous["_variants"] = variants
                    previous["text"] = "\n".join(variants)
                continue
        var prepared := cue.duplicate()
        prepared["_variants"] = [cue_text]
        merged.append(prepared)
    var maximum_end := 0.0
    for cue in merged:
        maximum_end = maxf(maximum_end, float(cue.get("end", 0.0)))
        cue["_max_end"] = maximum_end
        cue.erase("_order")
        cue.erase("_variants")
    return merged

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
