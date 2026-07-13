extends Node

signal marker_created(index: int, label: String)

const SCHEMA_VERSION := 1
const RING_WINDOW_USEC := 10_000_000
const POST_MARKER_USEC := 5_000_000
const MAX_RING_EVENTS := 2000
const MAX_MARKERS := 8
const MAX_FILE_BYTES := 4 * 1024 * 1024
const FLUSH_INTERVAL := 1.0
const NATIVE_DRAIN_INTERVAL := 0.5
const REQUEST_FILE := "user://diagnostic-request.json"
const MARKER_BUTTON_TEXT := "标记问题"
const MARKER_FEEDBACK_MSEC := 1800

const CATEGORY := {
    "lifecycle": 1 << 0,
    "input": 1 << 1,
    "render": 1 << 2,
    "storage": 1 << 3,
    "script": 1 << 4,
    "audio": 1 << 5,
    "video": 1 << 6,
    "plugin": 1 << 7,
    "memory": 1 << 8,
    "system": 1 << 9,
}

const PROFILE_MASK := {
    "baseline": (1 << 0) | (1 << 2),
    "input": (1 << 0) | (1 << 1) | (1 << 2),
    "render": (1 << 0) | (1 << 2) | (1 << 8),
    "storage": (1 << 0) | (1 << 2) | (1 << 3) | (1 << 8),
    "script": (1 << 0) | (1 << 2) | (1 << 4),
    "audio": (1 << 0) | (1 << 2) | (1 << 5),
    "video": (1 << 0) | (1 << 6) | (1 << 2),
    "plugin": (1 << 0) | (1 << 2) | (1 << 7),
    "system": (1 << 0) | (1 << 2) | (1 << 9),
    "full": (1 << 10) - 1,
}

var active := false
var profile := "baseline"
var session_id := ""
var session_dir := ""
var player = null
var sequence := 0
var dropped_events := 0
var marker_count := 0
var slow_frame_threshold_ms := 20.0

var _ring: Array[Dictionary] = []
var _pending_lines: PackedStringArray = []
var _event_file: FileAccess
var _flush_accum := 0.0
var _drain_accum := 0.0
var _frame_accum := 0.0
var _frame_samples: PackedFloat64Array = []
var _pending_incidents: Array[Dictionary] = []
var _overlay: CanvasLayer
var _marker_button: Button
var _status_label: Label
var _status_until_msec := 0

static func requested_profile() -> String:
    var requested := OS.get_environment("AETHERKIRI_DIAGNOSTIC_PROFILE").strip_edges().to_lower()
    if requested.is_empty() and FileAccess.file_exists(REQUEST_FILE):
        var file := FileAccess.open(REQUEST_FILE, FileAccess.READ)
        if file != null:
            var parsed = JSON.parse_string(file.get_as_text())
            if parsed is Dictionary:
                requested = String(parsed.get("profile", "")).strip_edges().to_lower()
    if requested.is_empty() and OS.get_name() == "Web":
        var value = JavaScriptBridge.eval(
            "new URLSearchParams(globalThis.location.search).get('diagnostic_profile') || ''",
            true
        )
        requested = String(value).strip_edges().to_lower()
    return requested if PROFILE_MASK.has(requested) else "baseline"

static func requested_session_id() -> String:
    var requested := OS.get_environment("AETHERKIRI_DIAGNOSTIC_SESSION").strip_edges()
    if requested.is_empty() and FileAccess.file_exists(REQUEST_FILE):
        var file := FileAccess.open(REQUEST_FILE, FileAccess.READ)
        if file != null:
            var parsed = JSON.parse_string(file.get_as_text())
            if parsed is Dictionary:
                requested = String(parsed.get("session", "")).strip_edges()
    if requested.is_empty() and OS.get_name() == "Web":
        var value = JavaScriptBridge.eval(
            "new URLSearchParams(globalThis.location.search).get('diagnostic_session') || ''",
            true
        )
        requested = String(value).strip_edges()
    if requested.is_empty():
        requested = "%s-%d" % [Time.get_datetime_string_from_system(false, true), OS.get_process_id()]
    return _safe_name(requested)

static func requested_enabled() -> bool:
    if OS.is_debug_build():
        return true
    var value := OS.get_environment("AETHERKIRI_DIAGNOSTICS").strip_edges().to_lower()
    if value in ["1", "true", "on", "yes"]:
        return true
    if FileAccess.file_exists(REQUEST_FILE):
        return true
    if OS.get_name() == "Web":
        return bool(JavaScriptBridge.eval(
            "new URLSearchParams(globalThis.location.search).has('diagnostic_session')",
            true
        ))
    return false

static func _safe_name(value: String) -> String:
    var result := value
    for character in ["/", "\\", ":", " ", "\t", "\n", "\r"]:
        result = result.replace(character, "-")
    return result.substr(0, 96)

func build_overlay(parent: Node) -> void:
    _overlay = CanvasLayer.new()
    _overlay.name = "DiagnosticMarkerOverlay"
    _overlay.layer = 140
    parent.add_child(_overlay)

    var box := VBoxContainer.new()
    box.set_anchors_preset(Control.PRESET_TOP_RIGHT)
    box.position = Vector2(-190, 18)
    box.size = Vector2(172, 88)
    # The overlay exists on the home screen too, even while the marker button
    # is hidden. Let empty overlay space pass through to top-right shell actions.
    box.mouse_filter = Control.MOUSE_FILTER_IGNORE
    _overlay.add_child(box)

    _marker_button = Button.new()
    _marker_button.text = MARKER_BUTTON_TEXT
    _marker_button.custom_minimum_size = Vector2(172, 48)
    _marker_button.pressed.connect(mark_issue.bind("user_issue"))
    _marker_button.visible = false
    box.add_child(_marker_button)

    _status_label = Label.new()
    _status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    _status_label.visible = false
    box.add_child(_status_label)

func start(runtime_player, renderer: String) -> bool:
    if active or not requested_enabled() or runtime_player == null:
        return active
    player = runtime_player
    profile = requested_profile()
    session_id = requested_session_id()
    session_dir = "user://diagnostics".path_join(session_id)
    DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(session_dir.path_join("incidents")))
    _event_file = FileAccess.open(session_dir.path_join("events.jsonl"), FileAccess.WRITE)
    if _event_file == null:
        push_error("Unable to open diagnostic event file: %s" % session_dir)
        return false
    active = true
    var mask := int(PROFILE_MASK.get(profile, PROFILE_MASK["baseline"]))
    if player.has_method("set_diagnostic_config"):
        var result := int(player.set_diagnostic_config(
            true, session_id, mask, int(slow_frame_threshold_ms), MAX_RING_EVENTS
        ))
        if result != 0:
            active = false
            _event_file = null
            push_error("Native diagnostic configuration failed: %s" % player.get_last_error())
            return false
    _write_metadata(renderer, mask)
    record("godot", "lifecycle", "info", "diagnostic_session_started", 0, {
        "profile": profile,
        "category_mask": mask,
        "output": ProjectSettings.globalize_path(session_dir),
    })
    flush()
    return true

func set_game_active(value: bool) -> void:
    if _marker_button != null:
        _marker_button.visible = active and value
        if not value:
            _reset_marker_feedback()
    if not value and _status_label != null:
        _status_label.visible = false

func record(layer: String, subsystem: String, level: String, event: String,
            duration_us: int = 0, fields: Dictionary = {}) -> void:
    if not active:
        return
    sequence += 1
    var item := {
        "schema": SCHEMA_VERSION,
        "session": session_id,
        "sequence": sequence,
        "monotonic_us": Time.get_ticks_usec(),
        "platform": OS.get_name(),
        "layer": layer,
        "subsystem": subsystem,
        "level": level,
        "event": event,
        "duration_us": duration_us,
        "queue_dropped": dropped_events,
        "fields": fields,
    }
    _accept_event(item, JSON.stringify(item))

func sample_frame(delta: float, tick_ms: float, update_ms: float,
                  renderer: String, texture_backend: String) -> void:
    if not active:
        return
    var frame_ms := delta * 1000.0
    _frame_samples.append(frame_ms)
    _frame_accum += delta
    var work_ms := tick_ms + update_ms
    if frame_ms >= slow_frame_threshold_ms or work_ms >= slow_frame_threshold_ms:
        record("godot", "render", "warning", "host_frame_spike", int(maxf(frame_ms, work_ms) * 1000.0), {
            "frame_ms": frame_ms,
            "tick_ms": tick_ms,
            "update_ms": update_ms,
            "renderer": renderer,
            "texture_backend": texture_backend,
        })
    if _frame_accum >= 1.0:
        _record_frame_summary(renderer, texture_backend)

func mark_issue(label: String = "user_issue") -> void:
    if not active:
        return
    if marker_count >= MAX_MARKERS:
        _show_marker_feedback("已达标记上限 (%d)" % MAX_MARKERS, false)
        if _marker_button != null:
            _marker_button.disabled = true
        print("[diagnostics] marker_rejected session=%s reason=max_markers count=%d" % [
            session_id, marker_count,
        ])
        return
    marker_count += 1
    var native_sequence := -1
    if player != null and player.has_method("mark_diagnostic_event"):
        native_sequence = int(player.mark_diagnostic_event(label))
        if native_sequence >= 0:
            # Persist the native marker in the same synchronous flush as the
            # Godot marker instead of leaving it queued for the next timer tick.
            _drain_native()
    record("godot", "lifecycle", "info", "issue_marker", 0, {
        "label": label,
        "marker_index": marker_count,
        "native_sequence": native_sequence,
    })
    var prefix := "marker-%02d" % marker_count
    var pre_lines := PackedStringArray()
    for item in _ring:
        pre_lines.append(String(item.get("line", "")))
    _write_lines(session_dir.path_join("incidents").path_join(prefix + "-pre.jsonl"), pre_lines)
    _pending_incidents.append({
        "index": marker_count,
        "label": label,
        "until_us": Time.get_ticks_usec() + POST_MARKER_USEC,
        "lines": PackedStringArray(),
    })
    flush()
    var feedback := "已标记 #%d" % marker_count
    _show_marker_feedback(feedback, true)
    if OS.get_name() in ["Android", "iOS"]:
        Input.vibrate_handheld(80, 0.45)
    print("[diagnostics] issue_marker session=%s marker=%d label=%s native_sequence=%d output=%s" % [
        session_id,
        marker_count,
        label,
        native_sequence,
        ProjectSettings.globalize_path(session_dir),
    ])
    marker_created.emit(marker_count, label)

func _show_marker_feedback(message: String, accepted: bool) -> void:
    if _marker_button != null:
        _marker_button.text = message
        _marker_button.disabled = accepted
    if _status_label != null:
        _status_label.text = "诊断日志已保存" if accepted else "请结束本次诊断会话"
        _status_label.visible = true
    _status_until_msec = Time.get_ticks_msec() + MARKER_FEEDBACK_MSEC

func _reset_marker_feedback() -> void:
    if _marker_button != null:
        if active and marker_count >= MAX_MARKERS:
            _marker_button.text = "标记已满 (%d)" % MAX_MARKERS
            _marker_button.disabled = true
        else:
            _marker_button.text = MARKER_BUTTON_TEXT
            _marker_button.disabled = false
    if _status_label != null:
        _status_label.visible = false

func finish() -> void:
    if not active:
        return
    _drain_native()
    _finish_all_incidents()
    record("godot", "lifecycle", "info", "diagnostic_session_finished", 0, {
        "markers": marker_count,
        "dropped": dropped_events,
    })
    flush()
    if OS.get_name() == "Web":
        _export_web_download()
    if player != null and player.has_method("set_diagnostic_config"):
        player.set_diagnostic_config(false, session_id, 0, int(slow_frame_threshold_ms), MAX_RING_EVENTS)
    active = false
    _event_file = null
    set_game_active(false)

func flush() -> void:
    if _event_file == null or _pending_lines.is_empty():
        return
    var flushed_lines := _pending_lines.duplicate()
    var output := "\n".join(flushed_lines) + "\n"
    if _event_file.get_position() + output.to_utf8_buffer().size() > MAX_FILE_BYTES:
        _rotate_event_file()
    _event_file.store_string(output)
    _event_file.flush()
    _pending_lines.clear()
    if OS.get_name() == "Web":
        _post_web_lines(flushed_lines)

func _process(delta: float) -> void:
    if not active:
        return
    _flush_accum += delta
    _drain_accum += delta
    if _drain_accum >= NATIVE_DRAIN_INTERVAL:
        _drain_accum = 0.0
        _drain_native()
    _finish_elapsed_incidents()
    if _flush_accum >= FLUSH_INTERVAL:
        _flush_accum = 0.0
        flush()
    if _status_label != null and _status_label.visible and Time.get_ticks_msec() >= _status_until_msec:
        _reset_marker_feedback()

func _notification(what: int) -> void:
    if what in [NOTIFICATION_APPLICATION_PAUSED, NOTIFICATION_APPLICATION_FOCUS_OUT,
                NOTIFICATION_WM_CLOSE_REQUEST, NOTIFICATION_PREDELETE]:
        if active:
            _drain_native()
            flush()

func _accept_event(item: Dictionary, line: String) -> void:
    var now := int(item.get("monotonic_us", Time.get_ticks_usec()))
    _ring.append({"monotonic_us": now, "line": line})
    while _ring.size() > MAX_RING_EVENTS:
        _ring.pop_front()
        dropped_events += 1
    while not _ring.is_empty():
        if now - int(_ring[0].get("monotonic_us", now)) <= RING_WINDOW_USEC:
            break
        _ring.pop_front()
    _pending_lines.append(line)
    for incident in _pending_incidents:
        var lines: PackedStringArray = incident.get("lines", PackedStringArray())
        lines.append(line)
        incident["lines"] = lines

func _drain_native() -> void:
    if player == null or not player.has_method("drain_diagnostic_events"):
        return
    # The bridge drains a bounded batch. Loop so shutdown/marker flushes do not
    # discard a backlog larger than one 256 KiB bridge buffer.
    for _batch in range(16):
        var text := String(player.drain_diagnostic_events())
        if text.is_empty():
            break
        for line in text.split("\n", false):
            var parsed = JSON.parse_string(line)
            if parsed is Dictionary:
                _accept_event(parsed, line)
            else:
                record("engine", "diagnostics", "warning", "invalid_native_event", 0, {"line": line})

func _record_frame_summary(renderer: String, texture_backend: String) -> void:
    _frame_accum = 0.0
    if _frame_samples.is_empty():
        return
    var samples := Array(_frame_samples)
    samples.sort()
    var total := 0.0
    for value in samples:
        total += float(value)
    var count := samples.size()
    record("godot", "render", "info", "frame_summary", 0, {
        "count": count,
        "average_ms": total / float(count),
        "p50_ms": float(samples[clampi(int(floor((count - 1) * 0.50)), 0, count - 1)]),
        "p95_ms": float(samples[clampi(int(floor((count - 1) * 0.95)), 0, count - 1)]),
        "p99_ms": float(samples[clampi(int(floor((count - 1) * 0.99)), 0, count - 1)]),
        "max_ms": float(samples[count - 1]),
        "renderer": renderer,
        "texture_backend": texture_backend,
    })
    _frame_samples.clear()

func _finish_elapsed_incidents() -> void:
    var now := Time.get_ticks_usec()
    for index in range(_pending_incidents.size() - 1, -1, -1):
        var incident: Dictionary = _pending_incidents[index]
        if now < int(incident.get("until_us", now + 1)):
            continue
        _finish_incident(incident)
        _pending_incidents.remove_at(index)

func _finish_all_incidents() -> void:
    for incident in _pending_incidents:
        _finish_incident(incident)
    _pending_incidents.clear()

func _finish_incident(incident: Dictionary) -> void:
    var prefix := "marker-%02d" % int(incident.get("index", 0))
    var lines: PackedStringArray = incident.get("lines", PackedStringArray())
    _write_lines(session_dir.path_join("incidents").path_join(prefix + "-post.jsonl"), lines)

func _write_lines(path: String, lines: PackedStringArray) -> void:
    var file := FileAccess.open(path, FileAccess.WRITE)
    if file == null:
        return
    if not lines.is_empty():
        file.store_string("\n".join(lines) + "\n")
    file.flush()

func _rotate_event_file() -> void:
    _event_file = null
    var current := ProjectSettings.globalize_path(session_dir.path_join("events.jsonl"))
    var previous := ProjectSettings.globalize_path(session_dir.path_join("events.previous.jsonl"))
    if FileAccess.file_exists(previous):
        DirAccess.remove_absolute(previous)
    if FileAccess.file_exists(current):
        DirAccess.rename_absolute(current, previous)
    _event_file = FileAccess.open(session_dir.path_join("events.jsonl"), FileAccess.WRITE)

func _write_metadata(renderer: String, mask: int) -> void:
    var metadata := {
        "schema": SCHEMA_VERSION,
        "session": session_id,
        "created_at": Time.get_datetime_string_from_system(true, true),
        "platform": OS.get_name(),
        "debug_build": OS.is_debug_build(),
        "profile": profile,
        "category_mask": mask,
        "slow_frame_threshold_ms": slow_frame_threshold_ms,
        "renderer": renderer,
        "engine_version": Engine.get_version_info(),
        "model": OS.get_model_name(),
        "locale": OS.get_locale(),
    }
    var file := FileAccess.open(session_dir.path_join("metadata.json"), FileAccess.WRITE)
    if file != null:
        file.store_string(JSON.stringify(metadata, "  "))
        file.flush()

func _post_web_lines(lines: PackedStringArray) -> void:
    if lines.is_empty():
        return
    var entries: Array[Dictionary] = []
    for line in lines:
        entries.append({
            "level": "diagnostic",
            "message": line,
            "timestamp": Time.get_datetime_string_from_system(true, true),
        })
    var source := "fetch('/__aetherkiri/client-log',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(%s),keepalive:true}).catch(()=>{});" % JSON.stringify(entries)
    JavaScriptBridge.eval(source, true)

func _export_web_download() -> void:
    var event_text := ""
    var event_path := session_dir.path_join("events.jsonl")
    var event_file := FileAccess.open(event_path, FileAccess.READ)
    if event_file != null:
        event_text = event_file.get_as_text()
    var metadata = {}
    var metadata_file := FileAccess.open(session_dir.path_join("metadata.json"), FileAccess.READ)
    if metadata_file != null:
        var parsed = JSON.parse_string(metadata_file.get_as_text())
        if parsed is Dictionary:
            metadata = parsed
    var payload := JSON.stringify({
        "schema": SCHEMA_VERSION,
        "metadata": metadata,
        "events_jsonl": event_text,
    })
    var source := "(()=>{const b=new Blob([%s],{type:'application/json'});const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download=%s;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);})();" % [
        JSON.stringify(payload),
        JSON.stringify(session_id + ".aetherdiag.json"),
    ]
    JavaScriptBridge.eval(source, true)
