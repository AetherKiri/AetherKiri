extends CanvasLayer

signal marker_requested(label: String)
signal snapshot_requested
signal screenshot_requested
signal export_requested
signal copy_summary_requested
signal self_check_requested
signal capture_slow_frame_requested
signal advanced_toggle_requested(key: String, enabled: bool)
signal input_visualization_changed(enabled: bool)
signal logs_clear_requested
signal drawer_visibility_changed(open: bool)

const DebugInputOverlay = preload("res://scripts/debug_input_overlay.gd")
const REFRESH_INTERVAL := 0.25
const MAX_RENDERED_LOGS := 200
const MARKER_TYPES := ["user_issue", "slow_frame", "input_issue", "visual_issue", "audio_issue", "loading_issue"]

var _translate: Callable
var _snapshot_provider: Callable
var _available := false
var _game_active := false
var _drawer_open := false
var _refresh_accum := 0.0
var _paused_logs := false
var _frozen_logs: Array = []
var _captured_pointers := {}

var _root: Control
var _open_button: Button
var _drawer: PanelContainer
var _title: Label
var _status: Label
var _tabs: TabContainer
var _overview: RichTextLabel
var _events: RichTextLabel
var _logs: RichTextLabel
var _input: RichTextLabel
var _plugins: RichTextLabel
var _event_level: OptionButton
var _event_subsystem: LineEdit
var _log_search: LineEdit
var _log_level: OptionButton
var _marker_types: OptionButton
var _pause_button: Button
var _result: Label
var _input_overlay: Control
var _input_visualization: CheckButton
var _plugin_trace: CheckButton
var _full_trace: CheckButton
var _legacy_console: CheckButton
var _export_tjs: CheckButton

func setup(parent: Node, translator: Callable, snapshot_provider: Callable) -> void:
    _translate = translator
    _snapshot_provider = snapshot_provider
    layer = 145
    parent.add_child(self)
    _build_ui()
    refresh_language()
    _sync_visibility()

func _build_ui() -> void:
    _root = Control.new()
    _root.set_anchors_preset(Control.PRESET_FULL_RECT)
    _root.mouse_filter = Control.MOUSE_FILTER_IGNORE
    add_child(_root)
    _root.resized.connect(_layout)

    _input_overlay = DebugInputOverlay.new()
    _input_overlay.visible = false
    _root.add_child(_input_overlay)

    _open_button = Button.new()
    _open_button.name = "DebugConsoleButton"
    _open_button.text = "DBG"
    _open_button.anchor_left = 1.0
    _open_button.anchor_right = 1.0
    # Keep the debugger launcher clear of DiagnosticSession's marker button
    # (rightmost 190 px). Overlapping CanvasLayers make the lower button look
    # clickable while the higher layer silently consumes its pointer.
    _open_button.offset_left = -274.0
    _open_button.offset_right = -202.0
    _open_button.offset_top = 18.0
    _open_button.offset_bottom = 66.0
    _open_button.mouse_filter = Control.MOUSE_FILTER_STOP
    _open_button.focus_mode = Control.FOCUS_ALL
    _open_button.add_theme_font_size_override("font_size", 16)
    _open_button.pressed.connect(toggle_drawer)
    _root.add_child(_open_button)

    _drawer = PanelContainer.new()
    _drawer.name = "DebugConsoleDrawer"
    _drawer.anchor_left = 1.0
    _drawer.anchor_right = 1.0
    _drawer.anchor_bottom = 1.0
    _drawer.offset_left = -660.0
    _drawer.offset_right = 0.0
    _drawer.mouse_filter = Control.MOUSE_FILTER_STOP
    var panel_style := StyleBoxFlat.new()
    panel_style.bg_color = Color(0.035, 0.042, 0.065, 0.97)
    panel_style.border_color = Color(0.45, 0.58, 0.86, 0.75)
    panel_style.border_width_left = 1
    _drawer.add_theme_stylebox_override("panel", panel_style)
    _root.add_child(_drawer)

    var margin := MarginContainer.new()
    margin.add_theme_constant_override("margin_left", 18)
    margin.add_theme_constant_override("margin_top", 14)
    margin.add_theme_constant_override("margin_right", 18)
    margin.add_theme_constant_override("margin_bottom", 16)
    _drawer.add_child(margin)
    var body := VBoxContainer.new()
    body.add_theme_constant_override("separation", 10)
    margin.add_child(body)

    var header := HBoxContainer.new()
    body.add_child(header)
    var header_text := VBoxContainer.new()
    header_text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    header.add_child(header_text)
    _title = Label.new()
    _title.add_theme_font_size_override("font_size", 23)
    header_text.add_child(_title)
    _status = Label.new()
    _status.add_theme_font_size_override("font_size", 13)
    _status.add_theme_color_override("font_color", Color(0.68, 0.76, 0.92))
    header_text.add_child(_status)
    var close := Button.new()
    close.text = "×"
    close.custom_minimum_size = Vector2(48, 44)
    close.add_theme_font_size_override("font_size", 24)
    close.pressed.connect(close_drawer)
    header.add_child(close)

    _tabs = TabContainer.new()
    _tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
    _tabs.custom_minimum_size = Vector2(0, 310)
    _tabs.tab_changed.connect(func(_index: int): _refresh_now())
    body.add_child(_tabs)
    _overview = _add_text_tab("overview")
    _events = _add_filter_tab("events")
    _logs = _add_log_tab()
    _input = _add_input_tab()
    _plugins = _add_plugin_tab()

    var marker_row := HBoxContainer.new()
    marker_row.add_theme_constant_override("separation", 6)
    body.add_child(marker_row)
    _marker_types = OptionButton.new()
    _marker_types.name = "MarkerType"
    _marker_types.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    for marker in MARKER_TYPES:
        _marker_types.add_item(marker.replace("_", " "))
        _marker_types.set_item_metadata(_marker_types.item_count - 1, marker)
    marker_row.add_child(_marker_types)
    marker_row.add_child(_action_button("debug.action.mark", func():
        marker_requested.emit(String(_marker_types.get_item_metadata(_marker_types.selected)))
    ))
    marker_row.add_child(_action_button("debug.action.slow", func(): capture_slow_frame_requested.emit()))

    var action_grid := GridContainer.new()
    action_grid.columns = 3
    action_grid.add_theme_constant_override("h_separation", 6)
    action_grid.add_theme_constant_override("v_separation", 6)
    body.add_child(action_grid)
    action_grid.add_child(_action_button("debug.action.snapshot", func(): snapshot_requested.emit()))
    action_grid.add_child(_action_button("debug.action.screenshot", func(): screenshot_requested.emit()))
    action_grid.add_child(_action_button("debug.action.self_check", func(): self_check_requested.emit()))
    action_grid.add_child(_action_button("debug.action.copy", func(): copy_summary_requested.emit()))
    action_grid.add_child(_action_button("debug.action.export", func(): export_requested.emit()))

    _result = Label.new()
    _result.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    _result.add_theme_font_size_override("font_size", 13)
    _result.add_theme_color_override("font_color", Color(0.72, 0.90, 0.76))
    body.add_child(_result)
    _layout()

func _layout() -> void:
    if _drawer == null:
        return
    var viewport_width := get_viewport().get_visible_rect().size.x
    _drawer.offset_left = -minf(660.0, maxf(300.0, viewport_width * 0.92))

func _add_text_tab(tab_name: String) -> RichTextLabel:
    var text := RichTextLabel.new()
    text.name = tab_name
    text.fit_content = false
    text.scroll_active = true
    text.bbcode_enabled = false
    text.add_theme_font_size_override("normal_font_size", 15)
    _tabs.add_child(text)
    return text

func _add_filter_tab(tab_name: String) -> RichTextLabel:
    var box := VBoxContainer.new()
    box.name = tab_name
    _tabs.add_child(box)
    var filters := HBoxContainer.new()
    box.add_child(filters)
    _event_level = OptionButton.new()
    for value in ["all", "info", "warning", "error"]:
        _event_level.add_item(value)
        _event_level.set_item_metadata(_event_level.item_count - 1, value)
    _event_level.item_selected.connect(func(_index: int): _refresh_now())
    filters.add_child(_event_level)
    _event_subsystem = LineEdit.new()
    _event_subsystem.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    _event_subsystem.text_changed.connect(func(_value: String): _refresh_now())
    filters.add_child(_event_subsystem)
    var text := RichTextLabel.new()
    text.size_flags_vertical = Control.SIZE_EXPAND_FILL
    text.add_theme_font_size_override("normal_font_size", 14)
    box.add_child(text)
    return text

func _add_log_tab() -> RichTextLabel:
    var box := VBoxContainer.new()
    box.name = "logs"
    _tabs.add_child(box)
    var filters := HBoxContainer.new()
    box.add_child(filters)
    _log_level = OptionButton.new()
    for value in ["all", "warning", "error"]:
        _log_level.add_item(value)
        _log_level.set_item_metadata(_log_level.item_count - 1, value)
    _log_level.item_selected.connect(func(_index: int): _refresh_now())
    filters.add_child(_log_level)
    _log_search = LineEdit.new()
    _log_search.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    _log_search.text_changed.connect(func(_value: String): _refresh_now())
    filters.add_child(_log_search)
    _pause_button = Button.new()
    _pause_button.toggle_mode = true
    _pause_button.toggled.connect(_on_log_pause_toggled)
    filters.add_child(_pause_button)
    var copy := Button.new()
    copy.set_meta("translation_key", "debug.logs.copy")
    copy.text = _t("debug.logs.copy")
    copy.pressed.connect(func(): DisplayServer.clipboard_set(_logs.text))
    filters.add_child(copy)
    var clear := Button.new()
    clear.set_meta("translation_key", "debug.logs.clear")
    clear.text = _t("debug.logs.clear")
    clear.pressed.connect(func(): logs_clear_requested.emit())
    filters.add_child(clear)
    var text := RichTextLabel.new()
    text.size_flags_vertical = Control.SIZE_EXPAND_FILL
    text.add_theme_font_size_override("normal_font_size", 14)
    box.add_child(text)
    return text

func _add_input_tab() -> RichTextLabel:
    var box := VBoxContainer.new()
    box.name = "input"
    _tabs.add_child(box)
    _input_visualization = CheckButton.new()
    _input_visualization.toggled.connect(func(enabled: bool):
        _input_overlay.visible = enabled and _game_active
        input_visualization_changed.emit(enabled)
    )
    box.add_child(_input_visualization)
    var text := RichTextLabel.new()
    text.size_flags_vertical = Control.SIZE_EXPAND_FILL
    text.add_theme_font_size_override("normal_font_size", 14)
    box.add_child(text)
    return text

func _add_plugin_tab() -> RichTextLabel:
    var box := VBoxContainer.new()
    box.name = "plugins"
    _tabs.add_child(box)
    var toggles := GridContainer.new()
    toggles.columns = 2
    box.add_child(toggles)
    _plugin_trace = _advanced_toggle("debug.advanced.plugin", "plugin_trace", toggles)
    _full_trace = _advanced_toggle("debug.advanced.full", "trace_log", toggles)
    _legacy_console = _advanced_toggle("debug.advanced.console", "console_log_file", toggles)
    _export_tjs = _advanced_toggle("debug.advanced.tjs", "export_scripts", toggles)
    var text := RichTextLabel.new()
    text.size_flags_vertical = Control.SIZE_EXPAND_FILL
    text.add_theme_font_size_override("normal_font_size", 14)
    box.add_child(text)
    return text

func _advanced_toggle(key: String, option: String, parent: Control) -> CheckButton:
    var toggle := CheckButton.new()
    toggle.set_meta("translation_key", key)
    toggle.toggled.connect(func(enabled: bool): advanced_toggle_requested.emit(option, enabled))
    parent.add_child(toggle)
    return toggle

func _action_button(key: String, callback: Callable) -> Button:
    var button := Button.new()
    button.set_meta("translation_key", key)
    button.text = _t(key)
    button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    button.custom_minimum_size = Vector2(0, 42)
    button.pressed.connect(callback)
    return button

func _t(key: String) -> String:
    return String(_translate.call(key)) if _translate.is_valid() else key

func _tf(key: String, values: Array) -> String:
    return _t(key) % values

func refresh_language() -> void:
    if _title == null:
        return
    _title.text = _t("debug.title")
    _open_button.tooltip_text = _t("debug.open")
    _tabs.set_tab_title(_overview.get_index(), _t("debug.tab.overview"))
    _tabs.set_tab_title(_events.get_parent().get_index(), _t("debug.tab.events"))
    _tabs.set_tab_title(_logs.get_parent().get_index(), _t("debug.tab.logs"))
    _tabs.set_tab_title(_input.get_parent().get_index(), _t("debug.tab.input"))
    _tabs.set_tab_title(_plugins.get_parent().get_index(), _t("debug.tab.plugins"))
    _event_subsystem.placeholder_text = _t("debug.filter.subsystem")
    _log_search.placeholder_text = _t("debug.filter.search")
    _pause_button.text = _t("debug.logs.pause")
    _input_visualization.text = _t("debug.input.visualize")
    for index in range(_marker_types.item_count):
        _marker_types.set_item_text(index, _t("debug.marker.%s" % String(_marker_types.get_item_metadata(index))))
    for option in [_event_level, _log_level]:
        for index in range(option.item_count):
            option.set_item_text(index, _t("debug.filter.value.%s" % String(option.get_item_metadata(index))))
    for button in _root.find_children("*", "Button", true, false):
        if button.has_meta("translation_key"):
            button.text = _t(String(button.get_meta("translation_key")))
    _refresh_now()

func set_available(value: bool) -> void:
    _available = value
    if not value:
        _drawer_open = false
    _sync_visibility()

func set_game_active(value: bool) -> void:
    _game_active = value
    if not value:
        _drawer_open = false
    _input_overlay.visible = value and _input_visualization.button_pressed
    _sync_visibility()

func is_open() -> bool:
    return _drawer_open

func _sync_visibility() -> void:
    if _root == null:
        return
    _root.visible = _available and _game_active
    _open_button.visible = not _drawer_open
    _drawer.visible = _drawer_open

func toggle_drawer() -> void:
    _drawer_open = not _drawer_open
    _sync_visibility()
    drawer_visibility_changed.emit(_drawer_open)
    if _drawer_open:
        _refresh_now()

func open_drawer() -> void:
    _drawer_open = true
    _sync_visibility()
    drawer_visibility_changed.emit(true)
    _refresh_now()

func close_drawer() -> void:
    _drawer_open = false
    _sync_visibility()
    drawer_visibility_changed.emit(false)

func show_result(message: String, is_error: bool = false) -> void:
    _result.text = message
    _result.add_theme_color_override("font_color", Color(1.0, 0.48, 0.48) if is_error else Color(0.72, 0.90, 0.76))

func _process(delta: float) -> void:
    if not _drawer_open or not _available or not _game_active:
        return
    _refresh_accum += delta
    if _refresh_accum >= REFRESH_INTERVAL:
        _refresh_now()

func _refresh_now() -> void:
    _refresh_accum = 0.0
    if not _drawer_open or not _snapshot_provider.is_valid():
        return
    var snapshot: Dictionary = _snapshot_provider.call()
    _update_status(snapshot)
    var current := _tabs.current_tab
    if current == _overview.get_index():
        _render_overview(snapshot)
    elif current == _events.get_parent().get_index():
        _render_events(snapshot)
    elif current == _logs.get_parent().get_index():
        _render_logs(snapshot)
    elif current == _input.get_parent().get_index():
        _render_input(snapshot)
    elif current == _plugins.get_parent().get_index():
        _render_plugins(snapshot)
    _sync_advanced(snapshot.get("advanced", {}))

func _update_status(snapshot: Dictionary) -> void:
    var session: Dictionary = snapshot.get("session", {})
    var overhead := String(snapshot.get("overhead", "low"))
    _status.text = "%s · %s · %s: %s" % [
        String(session.get("profile", "off")),
        String(session.get("session", "-")),
        _t("debug.overhead"),
        _t("debug.overhead.%s" % overhead),
    ]

func _render_overview(snapshot: Dictionary) -> void:
    var perf: Dictionary = snapshot.get("performance", {})
    var frame: Dictionary = perf.get("frame_summary", {})
    var memory: Dictionary = snapshot.get("memory", {})
    var session: Dictionary = snapshot.get("session", {})
    var startup: Dictionary = snapshot.get("startup", {})
    var slowest: Dictionary = session.get("slowest_events", {})
    var lines := PackedStringArray()
    lines.append("%s  %.1f FPS" % [_t("debug.metric.frame"), float(perf.get("fps", 0.0))])
    lines.append(_tf("debug.overview.percentiles", [float(frame.get("p50_ms", 0.0)), float(frame.get("p95_ms", 0.0)), float(frame.get("p99_ms", 0.0)), float(frame.get("max_ms", 0.0))]))
    lines.append(_tf("debug.overview.phases", [float(perf.get("tick_ms", 0.0)), float(perf.get("update_ms", 0.0)), float(perf.get("frame_ms", 0.0))]))
    lines.append("Startup  state=%s  shell %.1f  dispatch %.1f  runtime %.1f  first tick %.1f  first present %.1f ms" % [
        String(startup.get("state", "unknown")),
        float(startup.get("shell_prepare_ms", 0.0)),
        float(startup.get("open_dispatch_ms", 0.0)),
        float(startup.get("runtime_ready_ms", 0.0)),
        float(startup.get("first_tick_ms", 0.0)),
        float(startup.get("first_present_ms", 0.0)),
    ])
    var native_startup := PackedStringArray()
    for value in session.get("startup_events", []):
        var startup_event: Dictionary = value
        var startup_event_name := String(startup_event.get("event", ""))
        if startup_event_name.begins_with("engine_startup_"):
            native_startup.append("%s %.1f" % [
                startup_event_name.trim_prefix("engine_startup_"),
                float(startup_event.get("duration_us", 0)) / 1000.0,
            ])
    if not native_startup.is_empty():
        lines.append("Native startup (ms)  %s" % "  ".join(native_startup))
    var worst_tick: Dictionary = slowest.get("engine_tick_spike", {})
    var worst_fields: Dictionary = worst_tick.get("fields", {})
    lines.append("Worst tick  total %.1f  app %.1f  draw %.1f  capture %.1f ms  spikes %d" % [
        float(worst_tick.get("duration_us", 0)) / 1000.0,
        float(worst_fields.get("app_us", 0)) / 1000.0,
        float(worst_fields.get("draw_us", 0)) / 1000.0,
        float(worst_fields.get("capture_us", 0)) / 1000.0,
        int((session.get("spike_counts", {}) as Dictionary).get("engine_tick_spike", 0)),
    ])
    lines.append("")
    lines.append("%s: %s" % [_t("debug.metric.renderer"), String(perf.get("renderer", "-"))])
    var fallback_enabled: bool = perf.get("fallback", false)
    lines.append(_tf("debug.overview.render", [String(perf.get("texture", "-")), String(perf.get("surface", "-")), _t("debug.value.yes") if fallback_enabled else _t("debug.value.no")]))
    lines.append(_tf("debug.overview.memory", [_bytes(int(memory.get("current_bytes", 0))), _bytes(int(memory.get("system_free_bytes", 0))), _bytes(int(memory.get("system_total_bytes", 0))), _bytes(int(memory.get("cache_bytes", 0)))]))
    lines.append(_tf("debug.overview.memory_extended", [
        _bytes(int(memory.get("peak_bytes", 0))),
        _bytes(int(memory.get("available_bytes", 0))),
        _bytes(int(memory.get("godot_static_bytes", 0))),
        _bytes(int(memory.get("gpu_total_bytes", 0))),
        _bytes(int(memory.get("gpu_texture_bytes", 0))),
        _bytes(int(memory.get("gpu_buffer_bytes", 0))),
    ]))
    lines.append("")
    lines.append(_tf("debug.overview.counts", [int(perf.get("errors", 0)), int(session.get("dropped_events", 0)), int(session.get("markers", 0))]))
    lines.append("%s: %s" % [_t("debug.metric.directory"), String(session.get("session_dir", "-"))])
    _overview.text = "\n".join(lines)

func _render_events(snapshot: Dictionary) -> void:
    var level := String(_event_level.get_item_metadata(_event_level.selected))
    var subsystem := _event_subsystem.text.strip_edges().to_lower()
    var lines := PackedStringArray()
    for value in snapshot.get("events", []):
        var event: Dictionary = value
        var event_level := String(event.get("level", "info"))
        var event_subsystem := String(event.get("subsystem", ""))
        if level != "all" and event_level != level:
            continue
        if not subsystem.is_empty() and not event_subsystem.to_lower().contains(subsystem):
            continue
        lines.append("#%s +%.3fs [%s/%s] %s %.2fms" % [String(event.get("sequence", "?")), float(event.get("monotonic_us", 0)) / 1000000.0, event_level, event_subsystem, String(event.get("event", "")), float(event.get("duration_us", 0)) / 1000.0])
    _events.text = "\n".join(lines) if not lines.is_empty() else _t("debug.empty.events")

func _render_logs(snapshot: Dictionary) -> void:
    var source: Array = _frozen_logs if _paused_logs else snapshot.get("logs", [])
    if not _paused_logs:
        _frozen_logs = source.duplicate()
    var search := _log_search.text.to_lower()
    var level := String(_log_level.get_item_metadata(_log_level.selected))
    var lines := PackedStringArray()
    for value in source.slice(maxi(0, source.size() - MAX_RENDERED_LOGS)):
        var line := String(value)
        var lower := line.to_lower()
        if not search.is_empty() and not lower.contains(search):
            continue
        if level == "warning" and not (lower.contains("warn") or lower.contains("警告")):
            continue
        if level == "error" and not (lower.contains("error") or lower.contains("fatal") or lower.contains("failed") or lower.contains("错误") or lower.contains("失敗")):
            continue
        lines.append(line)
    _logs.text = "\n".join(lines) if not lines.is_empty() else _t("debug.empty.logs")

func _render_input(snapshot: Dictionary) -> void:
    var input_state: Dictionary = snapshot.get("input", {})
    _input.text = "%s: %s\n%s: %s\n%s\n%s\n%s" % [
        _t("debug.input.last"), String(input_state.get("last_event", "-")),
        _t("debug.input.target"), String(input_state.get("last_target", "-")),
        _tf("debug.input.primary_counts", [int(input_state.get("received", 0)), int(input_state.get("forwarded", 0)), int(input_state.get("blocked", 0))]),
        _tf("debug.input.secondary_counts", [int(input_state.get("throttled", 0)), int(input_state.get("busy", 0)), int(input_state.get("outside", 0)), int(input_state.get("suppressed", 0))]),
        _tf("debug.input.active_count", [int(input_state.get("active_count", 0))]),
    ]
    _input_overlay.update_state(input_state.get("points", {}), Vector2(input_state.get("last_position", Vector2.ZERO)), String(input_state.get("last_event", "")))

func _render_plugins(snapshot: Dictionary) -> void:
    var plugins: Dictionary = snapshot.get("plugins", {})
    _plugins.text = "%s\n%s\n\n%s\n%s\n%s\n%s" % [
        _tf("debug.plugins.counts", [int(plugins.get("plugin_load_success_count", 0)), int(plugins.get("plugin_load_failure_count", 0)), int(plugins.get("plugin_load_fallback_count", 0)), int(plugins.get("missing_member_count", 0))]),
        _tf("debug.plugins.calls", [int(plugins.get("method_call_count", 0)), int(plugins.get("property_call_count", 0))]),
        _tf("debug.plugins.loaded", [", ".join(plugins.get("loaded_plugins", []))]),
        _tf("debug.plugins.failed", [", ".join(plugins.get("failed_plugins", []))]),
        _tf("debug.plugins.fallback", [", ".join(plugins.get("fallback_plugins", []))]),
        _tf("debug.plugins.missing", [", ".join(plugins.get("recent_missing_members", []))]),
    ]

func _sync_advanced(advanced: Dictionary) -> void:
    for entry in [[_plugin_trace, "plugin_trace"], [_full_trace, "trace_log"], [_legacy_console, "console_log_file"], [_export_tjs, "export_scripts"]]:
        var toggle: CheckButton = entry[0]
        var enabled := bool(advanced.get(entry[1], false))
        toggle.set_pressed_no_signal(enabled)
        var remaining := int(advanced.get("%s_remaining_sec" % entry[1], 0))
        var base := _t(String(toggle.get_meta("translation_key")))
        toggle.text = "%s (%ds)" % [base, remaining] if enabled and remaining > 0 else base

func _on_log_pause_toggled(value: bool) -> void:
    _paused_logs = value
    _pause_button.text = _t("debug.logs.resume") if value else _t("debug.logs.pause")
    _refresh_now()

func _bytes(value: int) -> String:
    if value >= 1024 * 1024 * 1024:
        return "%.2f GiB" % (float(value) / float(1024 * 1024 * 1024))
    if value >= 1024 * 1024:
        return "%.1f MiB" % (float(value) / float(1024 * 1024))
    if value >= 1024:
        return "%.1f KiB" % (float(value) / 1024.0)
    return "%d B" % value

func routes_pointer(event: InputEvent) -> bool:
    if not _available or not _game_active or _root == null or not _root.visible:
        return false
    var pointer_id := -1
    var position := Vector2.ZERO
    var pressed := false
    var released := false
    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        pointer_id = touch.index
        position = touch.position
        pressed = touch.pressed
        released = not touch.pressed
    elif event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        pointer_id = drag.index
        position = drag.position
    elif event is InputEventMouseButton:
        var mouse := event as InputEventMouseButton
        if mouse.button_index != MOUSE_BUTTON_LEFT:
            return false
        pointer_id = -1000
        position = mouse.position
        pressed = mouse.pressed
        released = not mouse.pressed
    elif event is InputEventMouseMotion:
        if not Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
            return false
        pointer_id = -1000
        position = (event as InputEventMouseMotion).position
    else:
        return false
    if _captured_pointers.has(pointer_id):
        if released:
            _captured_pointers.erase(pointer_id)
        return true
    if pressed and ((_drawer_open and _drawer.get_global_rect().has_point(position)) or (not _drawer_open and _open_button.get_global_rect().has_point(position))):
        _captured_pointers[pointer_id] = true
        return true
    return false
