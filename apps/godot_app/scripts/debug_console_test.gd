extends SceneTree

const DebugConsole = preload("res://scripts/debug_console.gd")
const DiagnosticLocalization = preload("res://scripts/diagnostic_localization.gd")

class SnapshotProvider extends RefCounted:
    var calls := 0

    func snapshot() -> Dictionary:
        calls += 1
        return {
            "session": {"profile": "baseline", "session": "fixture", "session_dir": "/tmp/fixture", "dropped_events": 0, "markers": 1, "frame_summary": {"p50_ms": 10.0, "p95_ms": 18.0, "p99_ms": 22.0, "max_ms": 25.0}},
            "performance": {"fps": 60.0, "tick_ms": 2.0, "update_ms": 1.0, "frame_ms": 16.6, "renderer": "fixture", "texture": "1280x720", "surface": "1280x720", "fallback": false, "errors": 0, "frame_summary": {"p50_ms": 10.0, "p95_ms": 18.0, "p99_ms": 22.0, "max_ms": 25.0}},
            "memory": {"current_bytes": 1024, "system_free_bytes": 2048, "system_total_bytes": 4096, "cache_bytes": 256},
            "plugins": {"plugin_load_success_count": 1, "loaded_plugins": ["fixture"]},
            "events": [{"sequence": 1, "monotonic_us": 10, "level": "info", "subsystem": "test", "event": "fixture", "duration_us": 0}],
            "logs": ["fixture log"],
            "input": {"last_event": "touch", "last_target": "game", "last_position": Vector2(10, 20), "points": {1: Vector2(10, 20)}},
            "advanced": {},
            "overhead": "low",
        }

func _init() -> void:
    call_deferred("_run")

func _run() -> void:
    var missing := DiagnosticLocalization.validate()
    if not missing.is_empty():
        _fail("missing localized diagnostics keys: %s" % ", ".join(missing))
        return
    var provider := SnapshotProvider.new()
    var console = DebugConsole.new()
    console.setup(root, func(key: String): return DiagnosticLocalization.get_text("en", key), provider.snapshot)
    console.set_available(true)
    console.set_game_active(true)
    await process_frame
    if provider.calls != 0:
        _fail("closed drawer formatted a runtime snapshot")
        return
    var press := InputEventScreenTouch.new()
    press.index = 3
    press.position = console._open_button.get_global_rect().get_center()
    press.pressed = true
    if not console.routes_pointer(press):
        _fail("debug launcher touch was not reserved for UI")
        return
    var release := InputEventScreenTouch.new()
    release.index = 3
    release.position = press.position
    release.pressed = false
    if not console.routes_pointer(release):
        _fail("captured debug launcher release leaked to the game")
        return
    console.open_drawer()
    if provider.calls != 1 or not console._overview.text.contains("60.0 FPS"):
        _fail("open drawer did not render the bounded snapshot")
        return
    console.close_drawer()
    console._process(1.0)
    if provider.calls != 1:
        _fail("closed drawer continued polling snapshots")
        return
    print("debug_console_test: PASS")
    quit(0)

func _fail(message: String) -> void:
    push_error("debug_console_test: %s" % message)
    quit(1)
