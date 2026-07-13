extends SceneTree

const DiagnosticSession = preload("res://scripts/diagnostic_session.gd")

class FakePlayer extends Node:
    var enabled := false
    var native_sequence := 0
    var native_batches: Array[String] = []

    func set_diagnostic_config(next_enabled: bool, _session: String,
            _mask: int, _slow_ms: int, _max_events: int) -> int:
        enabled = next_enabled
        return 0

    func mark_diagnostic_event(label: String) -> int:
        if not enabled:
            return -1
        native_sequence += 1
        return native_sequence if not label.is_empty() else -1

    func drain_diagnostic_events() -> String:
        return native_batches.pop_front() if not native_batches.is_empty() else ""

    func get_last_error() -> String:
        return ""

func _init() -> void:
    call_deferred("_run")

func _run() -> void:
    var session_id := "godot-test-%d" % Time.get_ticks_usec()
    OS.set_environment("AETHERKIRI_DIAGNOSTIC_SESSION", session_id)
    OS.set_environment("AETHERKIRI_DIAGNOSTIC_PROFILE", "baseline")

    var fake := FakePlayer.new()
    root.add_child(fake)
    var diagnostics = DiagnosticSession.new()
    root.add_child(diagnostics)
    diagnostics.build_overlay(root)
    if diagnostics._marker_button.get_parent().mouse_filter != Control.MOUSE_FILTER_IGNORE:
        _fail("hidden diagnostic overlay blocks shell input")
        return
    if diagnostics._marker_button.mouse_filter != Control.MOUSE_FILTER_STOP:
        _fail("visible diagnostic marker button does not receive input")
        return
    if not diagnostics.start(fake, "test-renderer"):
        _fail("session did not start")
        return

    diagnostics.record("test", "lifecycle", "warning", "test_warning", 123, {"raw": "/private/game"})
    diagnostics.sample_frame(0.025, 21.0, 2.0, "test-renderer", "test-texture")
    var native_time := Time.get_ticks_usec()
    fake.native_batches = [
        JSON.stringify({"monotonic_us": native_time, "event": "native_backlog_1"}) + "\n",
        JSON.stringify({"monotonic_us": native_time + 1, "event": "native_backlog_2"}) + "\n",
    ]
    diagnostics._drain_native()
    if not fake.native_batches.is_empty():
        _fail("native diagnostic backlog was not fully drained")
        return
    var drops_before_expiry: int = diagnostics.dropped_events
    diagnostics._ring.push_front({
        "monotonic_us": Time.get_ticks_usec() - diagnostics.RING_WINDOW_USEC - 1,
        "line": "old-retained-event",
    })
    diagnostics.record("test", "lifecycle", "info", "expire_ring_window")
    if diagnostics.dropped_events != drops_before_expiry:
        _fail("normal ring-window expiry was counted as a queue drop")
        return
    diagnostics.mark_issue("test_issue")
    diagnostics._pending_incidents[0]["until_us"] = 0
    diagnostics._finish_elapsed_incidents()
    if not diagnostics._pending_incidents.is_empty():
        _fail("elapsed marker incident was not finalized")
        return
    diagnostics.finish()

    var base := "user://diagnostics".path_join(session_id)
    if not FileAccess.file_exists(base.path_join("metadata.json")):
        _fail("metadata was not written")
        return
    if not FileAccess.file_exists(base.path_join("events.jsonl")):
        _fail("events were not written")
        return
    if not FileAccess.file_exists(base.path_join("incidents/marker-01-pre.jsonl")):
        _fail("marker pre-window was not sealed")
        return
    if not FileAccess.file_exists(base.path_join("incidents/marker-01-post.jsonl")):
        _fail("marker post-window was not sealed")
        return
    var file := FileAccess.open(base.path_join("events.jsonl"), FileAccess.READ)
    var contents := file.get_as_text() if file != null else ""
    if (not contents.contains("test_issue") or
            not contents.contains("host_frame_spike") or
            not contents.contains("native_backlog_2")):
        _fail("expected structured events are missing")
        return
    print("diagnostic_session_test: PASS output=%s" % ProjectSettings.globalize_path(base))
    quit(0)

func _fail(message: String) -> void:
    push_error("diagnostic_session_test: %s" % message)
    quit(1)
