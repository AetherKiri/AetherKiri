extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    var app = MAIN_SCRIPT.new()
    var snapshot: Dictionary = app._current_settings_snapshot()

    assert(app.game_virtual_menu_enabled)
    assert(is_equal_approx(app.game_virtual_keyboard_opacity, 1.0))
    assert(bool(snapshot.get("game_virtual_menu_enabled", false)))
    assert(is_equal_approx(
        float(snapshot.get("game_virtual_keyboard_opacity", 0.0)),
        1.0
    ))
    assert("game_virtual_menu_enabled" in app.SETTINGS_DRAFT_KEYS)
    assert("game_virtual_keyboard_opacity" in app.SETTINGS_DRAFT_KEYS)

    assert(is_equal_approx(
        app._normalize_game_virtual_keyboard_opacity(-1.0),
        app.GAME_VIRTUAL_KEYBOARD_OPACITY_MIN
    ))
    assert(is_equal_approx(
        app._normalize_game_virtual_keyboard_opacity(2.0),
        app.GAME_VIRTUAL_KEYBOARD_OPACITY_MAX
    ))
    assert(is_equal_approx(
        app._normalize_game_virtual_keyboard_opacity(0.63),
        0.65
    ))

    app._begin_settings_edit()
    app._on_setting_toggle("game_virtual_menu", false)
    assert(not bool(app.settings_draft.get("game_virtual_menu_enabled", true)))
    assert(app.dirty_settings)

    var opacity_control := app._keyboard_controls_opacity_control()
    root.add_child(opacity_control)
    await process_frame
    var slider := opacity_control.get_node(
        "KeyboardControlsOpacitySlider"
    ) as HSlider
    var value_label := opacity_control.get_node(
        "KeyboardControlsOpacityValue"
    ) as Label
    assert(slider != null)
    assert(value_label != null)
    assert(slider.custom_minimum_size.x >= 220.0)
    assert(is_equal_approx(slider.min_value, 0.2))
    assert(is_equal_approx(slider.max_value, 1.0))
    assert(slider.get_theme_stylebox("slider") is StyleBoxFlat)
    assert(slider.get_theme_stylebox("grabber_area") is StyleBoxFlat)
    assert(not slider.mouse_force_pass_scroll_events)
    assert(app._nearest_horizontal_slider(slider) == slider)

    var drag_state := {
        "axis_lock": app.SHELL_SCROLL_AXIS_PENDING,
        "gesture_delta": Vector2.ZERO,
    }
    assert(app._update_shell_scroll_axis_lock(
        drag_state,
        Vector2(2.0, 5.0)
    ) == app.SHELL_SCROLL_AXIS_PENDING)
    assert(app._update_shell_scroll_axis_lock(
        drag_state,
        Vector2(24.0, 1.0)
    ) == app.SHELL_SCROLL_AXIS_HORIZONTAL)
    assert(app._update_shell_scroll_axis_lock(
        drag_state,
        Vector2(0.0, 120.0)
    ) == app.SHELL_SCROLL_AXIS_HORIZONTAL)

    var vertical_drag_state := {
        "axis_lock": app.SHELL_SCROLL_AXIS_PENDING,
        "gesture_delta": Vector2.ZERO,
    }
    assert(app._update_shell_scroll_axis_lock(
        vertical_drag_state,
        Vector2(1.0, 20.0)
    ) == app.SHELL_SCROLL_AXIS_VERTICAL)

    var regression_scroll := ScrollContainer.new()
    regression_scroll.size = Vector2(320.0, 120.0)
    root.add_child(regression_scroll)
    var scroll_content := Control.new()
    scroll_content.custom_minimum_size = Vector2(320.0, 800.0)
    regression_scroll.add_child(scroll_content)
    opacity_control.reparent(scroll_content)
    opacity_control.position = Vector2(20.0, 140.0)
    app.shell_root = regression_scroll
    app.settings_view = regression_scroll
    await process_frame
    regression_scroll.scroll_vertical = 100
    await process_frame
    var initial_scroll := regression_scroll.scroll_vertical
    var pointer := slider.get_global_rect().get_center()
    var pointer_key := 73
    app._start_shell_scroll_drag(pointer_key, pointer)
    assert(bool(app.shell_scroll_drag_states[pointer_key].get(
        "scroll_locked",
        false
    )))
    assert(not app._update_shell_scroll_drag(
        pointer_key,
        pointer + Vector2(1.0, 80.0),
        Vector2(1.0, 80.0)
    ))
    assert(regression_scroll.scroll_vertical == initial_scroll)
    app._finish_shell_scroll_drag(pointer_key)
    assert(not app.shell_scroll_drag_states.has(pointer_key))
    app.shell_root = null
    app.settings_view = null
    opacity_control.reparent(root)
    regression_scroll.free()

    slider.value = 0.65
    await process_frame
    assert(is_equal_approx(
        float(app.settings_draft.get("game_virtual_keyboard_opacity", 0.0)),
        0.65
    ))
    assert(value_label.text == "65%")

    snapshot = app._current_settings_snapshot()
    snapshot["game_virtual_menu_enabled"] = false
    snapshot["game_virtual_keyboard_opacity"] = 0.65
    app._apply_settings_snapshot(snapshot)
    assert(not app.game_virtual_menu_enabled)
    assert(is_equal_approx(app.game_virtual_keyboard_opacity, 0.65))

    for language in ["zh_hans", "zh_hant", "en", "ja", "ko"]:
        app.active_language = language
        assert(not String(app._t("settings.virtual_control_menu")).is_empty())
        assert(not String(app._t("settings.virtual_control_menu_desc")).is_empty())
        assert(not String(app._t("settings.keyboard_control_opacity")).is_empty())
        assert(not String(app._t(
            "settings.keyboard_control_opacity_desc"
        )).is_empty())

    opacity_control.free()
    app.free()
    print("virtual_control_settings_test: PASS")
    quit(0)
