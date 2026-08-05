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
const LOW_FPS_WINDOW_SAMPLES := 2400
const LEGACY_REQUEST_FILE := "user://diagnostic-request.json"
const MOBILE_DIAGNOSTIC_SUBDIR := "Aether/Diagnostics"
const ANDROID_DOCUMENTS_DIR := "/storage/emulated/0/Documents"
const PROFILE_CATALOG_FILE := "res://config/diagnostic_profiles.json"
const MARKER_FEEDBACK_MSEC := 1800
const PROFILE_NAMES := [
    "baseline", "input", "render", "storage", "script", "audio",
    "video", "plugin", "system", "full",
]

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

static func profile_catalog() -> Dictionary:
    var file := FileAccess.open(PROFILE_CATALOG_FILE, FileAccess.READ)
    if file == null:
        return {}
    var parsed = JSON.parse_string(file.get_as_text())
    if not parsed is Dictionary:
        return {}
    var result := {}
    for value in (parsed as Dictionary).get("profiles", []):
        if value is Dictionary:
            var name := String((value as Dictionary).get("name", ""))
            if not name.is_empty():
                result[name] = (value as Dictionary).duplicate(true)
    return result

var active := false
var profile := "baseline"
var session_id := ""
var session_dir := ""
var player = null
var sequence := 0
var dropped_events := 0
var marker_count := 0
var slow_frame_threshold_ms := 20.0
var preferred_profile := "baseline" if OS.is_debug_build() else "off"
var external_request := false
var started_at_usec := 0
var total_event_count := 0
var latest_frame_summary: Dictionary = {}
var last_action_error := ""

var _ring: Array[Dictionary] = []
var _pending_lines: PackedStringArray = []
var _event_file: FileAccess
var _flush_accum := 0.0
var _drain_accum := 0.0
var _frame_accum := 0.0
var _frame_samples: PackedFloat64Array = []
var _low_fps_samples: PackedFloat64Array = []
var _low_fps_cursor := 0
var _pending_incidents: Array[Dictionary] = []
var _overlay: CanvasLayer
var _marker_button: Button
var _status_label: Label
var _status_until_msec := 0
var _marker_touch_captures: Dictionary = {}
var _marker_mouse_captured := false
var _next_slow_frame_label := ""
var _snapshot_count := 0
var _translate: Callable
var _marker_context_provider: Callable

func set_translator(translator: Callable) -> void:
    _translate = translator

func set_marker_context_provider(provider: Callable) -> void:
    _marker_context_provider = provider

func refresh_language() -> void:
    _reset_marker_feedback()

func _tr(key: String, args: Array = []) -> String:
    if not _translate.is_valid():
        return key
    var value := String(_translate.call(key))
    return value % args if not args.is_empty() else value

static func diagnostic_root_for_platform(platform: String, documents_dir: String) -> String:
    if platform == "Android":
        return ANDROID_DOCUMENTS_DIR.path_join(MOBILE_DIAGNOSTIC_SUBDIR)
    if platform == "iOS":
        if documents_dir.is_empty():
            return ""
        return documents_dir.path_join(MOBILE_DIAGNOSTIC_SUBDIR)
    return "user://diagnostics"

static func diagnostic_root_dir() -> String:
    var documents_dir := ""
    if OS.get_name() == "iOS":
        documents_dir = OS.get_system_dir(OS.SYSTEM_DIR_DOCUMENTS)
    return diagnostic_root_for_platform(OS.get_name(), documents_dir)

static func _request_paths() -> PackedStringArray:
    var paths := PackedStringArray()
    var root := diagnostic_root_dir()
    if not root.is_empty() and OS.get_name() in ["Android", "iOS"]:
        paths.append(root.path_join("diagnostic-request.json"))
    paths.append(LEGACY_REQUEST_FILE)
    return paths

static func _request_data() -> Dictionary:
    for path in _request_paths():
        if not FileAccess.file_exists(path):
            continue
        var file := FileAccess.open(path, FileAccess.READ)
        if file == null:
            continue
        var parsed = JSON.parse_string(file.get_as_text())
        if parsed is Dictionary:
            return parsed
    return {}

static func _clear_request_files() -> void:
    for path in _request_paths():
        if FileAccess.file_exists(path):
            DirAccess.remove_absolute(ProjectSettings.globalize_path(path))

static func requested_profile() -> String:
    var requested := OS.get_environment("AETHERKIRI_DIAGNOSTIC_PROFILE").strip_edges().to_lower()
    if requested.is_empty():
        requested = String(_request_data().get("profile", "")).strip_edges().to_lower()
    if requested.is_empty() and OS.get_name() == "Web":
        var value = JavaScriptBridge.eval(
            "new URLSearchParams(globalThis.location.search).get('diagnostic_profile') || ''",
            true
        )
        requested = String(value).strip_edges().to_lower()
    return requested if PROFILE_MASK.has(requested) else "baseline"

static func external_request_present() -> bool:
    for name in [
        "AETHERKIRI_DIAGNOSTICS",
        "AETHERKIRI_DIAGNOSTIC_PROFILE",
        "AETHERKIRI_DIAGNOSTIC_SESSION",
    ]:
        if not OS.get_environment(name).strip_edges().is_empty():
            return true
    for path in _request_paths():
        if FileAccess.file_exists(path):
            return true
    if OS.get_name() == "Web":
        return bool(JavaScriptBridge.eval(
            "new URLSearchParams(globalThis.location.search).has('diagnostic_session')",
            true
        ))
    return false

static func requested_session_id() -> String:
    var requested := OS.get_environment("AETHERKIRI_DIAGNOSTIC_SESSION").strip_edges()
    if requested.is_empty():
        requested = String(_request_data().get("session", "")).strip_edges()
    if requested.is_empty() and OS.get_name() == "Web":
        var value = JavaScriptBridge.eval(
            "new URLSearchParams(globalThis.location.search).get('diagnostic_session') || ''",
            true
        )
        requested = String(value).strip_edges()
    if requested.is_empty():
        requested = "%s-%d-%d" % [Time.get_datetime_string_from_system(false, true), OS.get_process_id(), Time.get_ticks_msec()]
    return _safe_name(requested)

static func requested_enabled() -> bool:
    return external_request_present()

func configure_preference(value: String) -> void:
    var normalized := value.strip_edges().to_lower()
    preferred_profile = normalized if normalized in PROFILE_NAMES else "off"

func apply_preference(value: String, runtime_player, renderer: String) -> bool:
    configure_preference(value)
    if external_request:
        return active
    if active:
        finish()
    if preferred_profile == "off":
        return false
    return start(runtime_player, renderer)

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
    _marker_button.text = _tr("debug.action.mark")
    _marker_button.custom_minimum_size = Vector2(172, 48)
    _marker_button.pressed.connect(mark_issue.bind("user_issue"))
    _marker_button.visible = false
    box.add_child(_marker_button)

    _status_label = Label.new()
    _status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    _status_label.visible = false
    box.add_child(_status_label)

func start(runtime_player, renderer: String) -> bool:
    if active or runtime_player == null:
        return active
    external_request = external_request_present()
    if not external_request and preferred_profile == "off":
        return false
    player = runtime_player
    profile = requested_profile() if external_request else preferred_profile
    session_id = requested_session_id()
    var diagnostic_root := diagnostic_root_dir()
    if diagnostic_root.is_empty():
        push_error("Platform Documents directory is unavailable for diagnostics")
        return false
    session_dir = diagnostic_root.path_join(session_id)
    var mkdir_result := DirAccess.make_dir_recursive_absolute(
        ProjectSettings.globalize_path(session_dir.path_join("incidents"))
    )
    if mkdir_result != OK:
        push_error("Unable to create public diagnostic directory: %s (%d)" % [
            session_dir, mkdir_result,
        ])
        return false
    _event_file = FileAccess.open(session_dir.path_join("events.jsonl"), FileAccess.WRITE)
    if _event_file == null:
        push_error("Unable to open diagnostic event file: %s" % session_dir)
        return false
    _clear_request_files()
    active = true
    started_at_usec = Time.get_ticks_usec()
    sequence = 0
    dropped_events = 0
    marker_count = 0
    total_event_count = 0
    _ring.clear()
    _pending_lines.clear()
    _pending_incidents.clear()
    latest_frame_summary.clear()
    _frame_samples.clear()
    _low_fps_samples.clear()
    _low_fps_cursor = 0
    _next_slow_frame_label = ""
    _snapshot_count = 0
    var catalog := profile_catalog()
    var profile_definition: Dictionary = catalog.get(profile, {})
    var mask := int(profile_definition.get("category_mask", PROFILE_MASK.get(profile, PROFILE_MASK["baseline"])))
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
    if not value:
        _marker_touch_captures.clear()
        _marker_mouse_captured = false

func routes_pointer_to_marker(event: InputEvent) -> bool:
    if _marker_button == null or not active or not _marker_button.visible:
        return false
    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        if touch.pressed and _marker_button.get_global_rect().has_point(touch.position):
            _marker_touch_captures[touch.index] = true
            print("[diagnostics] marker_input phase=press type=touch pointer=%d pos=%s" % [
                touch.index, str(touch.position),
            ])
            return true
        if _marker_touch_captures.has(touch.index):
            if not touch.pressed:
                _marker_touch_captures.erase(touch.index)
            return true
        return false
    if event is InputEventScreenDrag:
        return _marker_touch_captures.has((event as InputEventScreenDrag).index)
    if event is InputEventMouseButton:
        var button := event as InputEventMouseButton
        if button.button_index != MOUSE_BUTTON_LEFT:
            return false
        if button.pressed and _marker_button.get_global_rect().has_point(button.position):
            _marker_mouse_captured = true
            print("[diagnostics] marker_input phase=press type=mouse pos=%s" % str(button.position))
            return true
        if _marker_mouse_captured:
            if not button.pressed:
                _marker_mouse_captured = false
            return true
        return false
    if event is InputEventMouseMotion:
        return _marker_mouse_captured
    return false

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
    var frame_ms := delta * 1000.0
    _frame_samples.append(frame_ms)
    if _low_fps_samples.size() < LOW_FPS_WINDOW_SAMPLES:
        _low_fps_samples.append(frame_ms)
    else:
        _low_fps_samples[_low_fps_cursor] = frame_ms
        _low_fps_cursor = (_low_fps_cursor + 1) % LOW_FPS_WINDOW_SAMPLES
    _frame_accum += delta
    var work_ms := tick_ms + update_ms
    if active and (frame_ms >= slow_frame_threshold_ms or work_ms >= slow_frame_threshold_ms):
        record("godot", "render", "warning", "host_frame_spike", int(maxf(frame_ms, work_ms) * 1000.0), {
            "frame_ms": frame_ms,
            "tick_ms": tick_ms,
            "update_ms": update_ms,
            "renderer": renderer,
            "texture_backend": texture_backend,
        })
        if not _next_slow_frame_label.is_empty():
            var label := _next_slow_frame_label
            _next_slow_frame_label = ""
            record("godot", "render", "info", "armed_slow_frame_captured", int(maxf(frame_ms, work_ms) * 1000.0), {
                "label": label,
                "frame_ms": frame_ms,
                "tick_ms": tick_ms,
                "update_ms": update_ms,
            })
            mark_issue(label)
    if _frame_accum >= 1.0:
        _record_frame_summary(renderer, texture_backend)

func mark_issue(label: String = "user_issue") -> bool:
    if not active:
        return false
    if marker_count >= MAX_MARKERS:
        _show_marker_feedback(_tr("debug.result.marker_limit", [MAX_MARKERS]), false)
        if _marker_button != null:
            _marker_button.disabled = true
        print("[diagnostics] marker_rejected session=%s reason=max_markers count=%d" % [
            session_id, marker_count,
        ])
        return false
    marker_count += 1
    var marker_context: Dictionary = {}
    if _marker_context_provider.is_valid():
        var provided = _marker_context_provider.call()
        if provided is Dictionary:
            marker_context = (provided as Dictionary).duplicate(true)
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
        "context": marker_context,
    })
    var prefix := "marker-%02d" % marker_count
    var pre_lines := PackedStringArray()
    for item in _ring:
        pre_lines.append(String(item.get("line", "")))
    _write_lines(session_dir.path_join("incidents").path_join(prefix + "-pre.jsonl"), pre_lines)
    var state_file := FileAccess.open(session_dir.path_join("incidents").path_join(prefix + "-state.json"), FileAccess.WRITE)
    if state_file != null:
        state_file.store_string(JSON.stringify({
            "schema": SCHEMA_VERSION,
            "session": session_id,
            "marker_index": marker_count,
            "label": label,
            "monotonic_us": Time.get_ticks_usec(),
            "context": marker_context,
        }, "  ") + "\n")
        state_file.flush()
    _pending_incidents.append({
        "index": marker_count,
        "label": label,
        "until_us": Time.get_ticks_usec() + POST_MARKER_USEC,
        "lines": PackedStringArray(),
    })
    flush()
    var feedback := "✓ #%d" % marker_count
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
    return true

func arm_next_slow_frame(label: String = "slow_frame") -> bool:
    if not active:
        last_action_error = "diagnostic session is not active"
        return false
    _next_slow_frame_label = label
    record("godot", "render", "info", "slow_frame_capture_armed", 0, {
        "label": label,
        "threshold_ms": slow_frame_threshold_ms,
    })
    flush()
    last_action_error = ""
    return true

func write_state_snapshot(fields: Dictionary) -> String:
    if not active:
        last_action_error = "diagnostic session is not active"
        return ""
    _snapshot_count += 1
    var item := {
        "schema": SCHEMA_VERSION,
        "session": session_id,
        "sequence": _snapshot_count,
        "monotonic_us": Time.get_ticks_usec(),
        "fields": fields,
    }
    var path := session_dir.path_join("state-snapshot-%02d.json" % _snapshot_count)
    var file := FileAccess.open(path, FileAccess.WRITE)
    if file == null:
        last_action_error = "unable to write state snapshot"
        return ""
    file.store_string(JSON.stringify(item, "  ") + "\n")
    file.flush()
    record("godot", "system", "info", "state_snapshot", 0, {
        "index": _snapshot_count,
        "path": path.get_file(),
    })
    flush()
    last_action_error = ""
    return ProjectSettings.globalize_path(path)

func status_snapshot() -> Dictionary:
    return {
        "active": active,
        "profile": profile if active else preferred_profile,
        "external_request": external_request,
        "session": session_id,
        "session_dir": ProjectSettings.globalize_path(session_dir) if not session_dir.is_empty() else "",
        "elapsed_seconds": maxf(0.0, float(Time.get_ticks_usec() - started_at_usec) / 1_000_000.0) if active else 0.0,
        "events": total_event_count,
        "dropped": dropped_events,
        "dropped_events": dropped_events,
        "markers": marker_count,
        "max_markers": MAX_MARKERS,
        "slow_frame_armed": not _next_slow_frame_label.is_empty(),
        "frame_summary": latest_frame_summary.duplicate(true),
    }

func recent_events(limit: int = 50) -> Array[Dictionary]:
    var output: Array[Dictionary] = []
    var first := maxi(0, _ring.size() - maxi(0, limit))
    for index in range(first, _ring.size()):
        var event = _ring[index].get("event", {})
        if event is Dictionary:
            output.append((event as Dictionary).duplicate(true))
    return output

func summary_text(extra: Dictionary = {}) -> String:
    var status := status_snapshot()
    var frame: Dictionary = status.get("frame_summary", {})
    var lines := PackedStringArray([
        "Aether diagnostic summary",
        "session: %s" % String(status.get("session", "")),
        "platform: %s" % OS.get_name(),
        "profile: %s" % String(status.get("profile", "off")),
        "elapsed: %.1fs" % float(status.get("elapsed_seconds", 0.0)),
        "events: %d (dropped: %d)" % [int(status.get("events", 0)), int(status.get("dropped", 0))],
        "markers: %d" % int(status.get("markers", 0)),
        "frame p95/max: %.2f / %.2f ms" % [
            float(frame.get("p95_ms", 0.0)),
            float(frame.get("max_ms", 0.0)),
        ],
    ])
    for key in extra.keys():
        lines.append("%s: %s" % [String(key), String(extra[key])])
    lines.append("output: %s" % String(status.get("session_dir", "")))
    return "\n".join(lines)

func export_zip() -> String:
    if not active or session_dir.is_empty():
        last_action_error = "diagnostic session is not active"
        return ""
    _drain_native()
    flush()
    var zip_path := diagnostic_root_dir().path_join(session_id + ".zip")
    var packer := ZIPPacker.new()
    var open_result := packer.open(zip_path)
    if open_result != OK:
        last_action_error = "unable to create ZIP (%d)" % open_result
        return ""
    var files := PackedStringArray()
    _collect_files_recursive(session_dir, "", files)
    for relative in files:
        var source := FileAccess.open(session_dir.path_join(relative), FileAccess.READ)
        if source == null:
            continue
        var start_result := packer.start_file(relative)
        if start_result != OK:
            packer.close()
            last_action_error = "unable to add %s to ZIP (%d)" % [relative, start_result]
            return ""
        packer.write_file(source.get_buffer(source.get_length()))
        packer.close_file()
    packer.close()
    last_action_error = ""
    return ProjectSettings.globalize_path(zip_path)

func _collect_files_recursive(root: String, relative: String, output: PackedStringArray) -> void:
    var path := root if relative.is_empty() else root.path_join(relative)
    var directory := DirAccess.open(path)
    if directory == null:
        return
    directory.list_dir_begin()
    var name := directory.get_next()
    while not name.is_empty():
        if name != "." and name != "..":
            var child := name if relative.is_empty() else relative.path_join(name)
            if directory.current_is_dir():
                _collect_files_recursive(root, child, output)
            else:
                output.append(child)
        name = directory.get_next()
    directory.list_dir_end()

func _show_marker_feedback(message: String, accepted: bool) -> void:
    if _marker_button != null:
        _marker_button.text = message
        _marker_button.disabled = accepted
    if _status_label != null:
        _status_label.text = _tr("debug.result.marked", ["#%d" % marker_count]) if accepted else message
        _status_label.visible = true
    _status_until_msec = Time.get_ticks_msec() + MARKER_FEEDBACK_MSEC

func _reset_marker_feedback() -> void:
    if _marker_button != null:
        if active and marker_count >= MAX_MARKERS:
            _marker_button.text = "%s (%d/%d)" % [_tr("debug.action.mark"), marker_count, MAX_MARKERS]
            _marker_button.disabled = true
        else:
            _marker_button.text = _tr("debug.action.mark")
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
    total_event_count += 1
    _ring.append({"monotonic_us": now, "line": line, "event": item.duplicate(true)})
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
    var average_ms := total / float(count)
    var low_samples := Array(_low_fps_samples)
    low_samples.sort()
    var low_count := low_samples.size()
    var one_percent_count := maxi(1, int(ceil(float(low_count) * 0.01)))
    var point_one_percent_count := maxi(1, int(ceil(float(low_count) * 0.001)))
    var one_percent_total := 0.0
    var point_one_percent_total := 0.0
    for index in range(low_count - one_percent_count, low_count):
        one_percent_total += float(low_samples[index])
    for index in range(low_count - point_one_percent_count, low_count):
        point_one_percent_total += float(low_samples[index])
    var variance := 0.0
    for value in samples:
        var difference := float(value) - average_ms
        variance += difference * difference
    latest_frame_summary = {
        "count": count,
        "average_ms": average_ms,
        "average_fps": 1000.0 / maxf(average_ms, 0.001),
        "one_percent_low_fps": 1000.0 / maxf(one_percent_total / float(one_percent_count), 0.001),
        "point_one_percent_low_fps": 1000.0 / maxf(point_one_percent_total / float(point_one_percent_count), 0.001),
        "jitter_ms": sqrt(variance / float(count)),
        "p50_ms": float(samples[clampi(int(floor((count - 1) * 0.50)), 0, count - 1)]),
        "p95_ms": float(samples[clampi(int(floor((count - 1) * 0.95)), 0, count - 1)]),
        "p99_ms": float(samples[clampi(int(floor((count - 1) * 0.99)), 0, count - 1)]),
        "max_ms": float(samples[count - 1]),
        "renderer": renderer,
        "texture_backend": texture_backend,
    }
    record("godot", "render", "info", "frame_summary", 0, latest_frame_summary)
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
