extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

class MockRuntimePlayer:
    extends Node

    var key_events: Array[Dictionary] = []

    func send_key_event(
        pressed: bool,
        key_code: int,
        modifiers: int,
        unicode_codepoint: int
    ) -> int:
        key_events.append({
            "pressed": pressed,
            "key_code": key_code,
            "modifiers": modifiers,
            "unicode": unicode_codepoint,
        })
        return 0

func _initialize() -> void:
    var app = MAIN_SCRIPT.new()

    assert(app.game_virtual_input_mode == "mouse")
    assert(app._normalize_game_virtual_input_mode("mouse") == "mouse")
    assert(app._normalize_game_virtual_input_mode("touch") == "touch")
    assert(app._normalize_game_virtual_input_mode("invalid") == "mouse")

    for runtime_kind in [app.RUNTIME_KIRIKIRI, app.RUNTIME_ONSCRIPTER]:
        app.active_runtime_kind = runtime_kind
        assert(app._should_enable_game_virtual_controls(true, true, false))
    assert(not app._should_enable_game_virtual_controls(false, true, false))
    assert(not app._should_enable_game_virtual_controls(true, false, false))
    assert(not app._should_enable_game_virtual_controls(true, true, true))

    var runtime_player := MockRuntimePlayer.new()
    var game_viewport := TextureRect.new()
    game_viewport.visible = true
    app.add_child(runtime_player)
    app.add_child(game_viewport)
    app.player = runtime_player
    app.viewport = game_viewport
    app.game_running = true
    app.cached_startup_state = app.STARTUP_SUCCEEDED
    for runtime_kind in [app.RUNTIME_KIRIKIRI, app.RUNTIME_ONSCRIPTER]:
        app.active_runtime_kind = runtime_kind
        app._on_game_virtual_key_event(true, 0x1b, 0)
        app._on_game_virtual_key_event(false, 0x1b, 0)
    assert(runtime_player.key_events.size() == 4)
    app._on_game_virtual_key_event(true, 0x57, 0)
    app._on_game_virtual_key_event(false, 0x57, 0)
    assert(runtime_player.key_events[-2].unicode == 0x77)
    assert(runtime_player.key_events[-1].unicode == 0)
    app._on_game_virtual_key_event(true, 0x31, 0)
    app._on_game_virtual_key_event(true, 0x20, 0)
    app._on_game_virtual_key_event(true, 0x41, 0x04)
    assert(runtime_player.key_events[-3].unicode == 0x31)
    assert(runtime_player.key_events[-2].unicode == 0x20)
    assert(runtime_player.key_events[-1].unicode == 0)

    var portrait := app._scaled_display_safe_rect(
        Vector2(430, 932),
        Vector2(1179, 2556),
        Rect2(0, 177, 1179, 2283)
    )
    assert(portrait.position.y > 60.0)
    assert(portrait.size.x == 430.0)
    assert(portrait.position.y + portrait.size.y < 900.0)

    var landscape := app._scaled_display_safe_rect(
        Vector2(932, 430),
        Vector2(2556, 1179),
        Rect2(177, 0, 2283, 1113)
    )
    assert(landscape.position.x > 60.0)
    assert(landscape.position.x + landscape.size.x < 900.0)
    assert(landscape.position.y + landscape.size.y < 410.0)

    var ipad_safe_rect := Rect2(0, 24, 1194, 786)
    var ipad_sidebar_backdrop := app._sidebar_backdrop_rect(
        Vector2(1194, 834),
        ipad_safe_rect,
        232.0
    )
    assert(is_equal_approx(ipad_sidebar_backdrop.position.y, -24.0))
    assert(is_equal_approx(ipad_sidebar_backdrop.end.y, 810.0))
    assert(is_equal_approx(ipad_sidebar_backdrop.size.x, 232.0))

    var portrait_settings: Dictionary = app._settings_layout_spec(portrait.size)
    var landscape_settings: Dictionary = app._settings_layout_spec(landscape.size)
    assert(bool(portrait_settings["stack_controls"]))
    assert(not bool(landscape_settings["stack_controls"]))
    assert(float(landscape_settings["content_width"]) > float(portrait_settings["content_width"]))

    var portrait_detail: Dictionary = app._detail_layout_spec(portrait.size)
    var landscape_detail: Dictionary = app._detail_layout_spec(landscape.size)
    assert(bool(portrait_detail["compact"]))
    assert(not bool(portrait_detail["phone_landscape"]))
    assert(not bool(landscape_detail["compact"]))
    assert(bool(landscape_detail["phone_landscape"]))
    assert(float(landscape_detail["content_width"]) > float(portrait_detail["content_width"]))
    assert(app._home_card_minimum_size(true) == Vector2(340, 112))
    assert(app._home_card_minimum_size(false) == Vector2(340, 132))

    var portrait_video_controls: Dictionary = app._video_controls_layout_spec(portrait.size)
    var landscape_video_controls: Dictionary = app._video_controls_layout_spec(landscape.size)
    assert(bool(portrait_video_controls["phone_portrait"]))
    assert(not bool(landscape_video_controls["phone_portrait"]))
    assert(float(portrait_video_controls["transport_width"]) < float(landscape_video_controls["transport_width"]))
    assert(float(portrait_video_controls["panel_height"]) > float(landscape_video_controls["panel_height"]))

    app._build_video_view()
    assert(app.video_back_button.text.is_empty())
    assert(app.video_back_button.get_child_count() > 0)
    app._apply_video_controls_layout(portrait.size)
    assert(app.video_action_groups.vertical)
    assert(app.video_rewind_button.custom_minimum_size == Vector2(70, 42))
    assert(app.video_rate_button.custom_minimum_size == Vector2(86, 42))
    assert(app.video_subtitle_button.custom_minimum_size == Vector2(148, 42))
    app._apply_video_controls_layout(landscape.size)
    assert(not app.video_action_groups.vertical)
    assert(app.video_rewind_button.custom_minimum_size == Vector2(86, 48))

    var landscape_controls_panel := app._video_controls_panel_rect(
        Vector2(932, 430),
        landscape,
        float(landscape_video_controls["panel_height"])
    )
    assert(is_equal_approx(landscape_controls_panel.end.y, 430.0))
    assert(is_equal_approx(
        landscape_controls_panel.position.y,
        landscape.end.y - float(landscape_video_controls["panel_height"])
    ))

    var dialog := PanelContainer.new()
    app._mark_legal_safe_dialog(dialog, false, true)
    app._layout_safe_dialog(dialog, portrait)
    var dialog_rect := dialog.get_rect()
    assert(dialog_rect.position.x >= portrait.position.x)
    assert(dialog_rect.position.y >= portrait.position.y)
    assert(dialog_rect.end.x <= portrait.end.x)
    assert(dialog_rect.end.y <= portrait.end.y)

    var runtime_dialog := PanelContainer.new()
    runtime_dialog.clip_contents = true
    root.add_child(runtime_dialog)
    var runtime_safe_rect := Rect2(40, 24, 852, 382)
    app._mark_centered_safe_dialog(runtime_dialog, Vector2(780, 520))
    app._layout_safe_dialog(runtime_dialog, runtime_safe_rect)
    app._build_runtime_dialog_content(runtime_dialog, {
        "title": "Help",
        "message": "\n".join(PackedStringArray(Array(range(120)).map(
            func(index: int): return "Long Artemis help line %d" % index
        ))),
        "yes_no": "0",
        "text_field": "0",
    })
    await process_frame
    await process_frame
    var runtime_dialog_rect := runtime_dialog.get_rect()
    assert(runtime_dialog_rect.position.x >= runtime_safe_rect.position.x)
    assert(runtime_dialog_rect.position.y >= runtime_safe_rect.position.y)
    assert(runtime_dialog_rect.end.x <= runtime_safe_rect.end.x)
    assert(runtime_dialog_rect.end.y <= runtime_safe_rect.end.y)
    var message_scroll := runtime_dialog.get_node(
        "ArtemisDialogMargin/ArtemisDialogContent/ArtemisDialogMessageScroll"
    ) as ScrollContainer
    var runtime_buttons := runtime_dialog.get_node(
        "ArtemisDialogMargin/ArtemisDialogContent/ArtemisDialogButtons"
    ) as HBoxContainer
    assert(message_scroll != null)
    assert(runtime_buttons != null)
    assert(message_scroll.vertical_scroll_mode == ScrollContainer.SCROLL_MODE_AUTO)
    assert(message_scroll.horizontal_scroll_mode == ScrollContainer.SCROLL_MODE_DISABLED)
    assert(not message_scroll.mouse_force_pass_scroll_events)
    assert(message_scroll.get_rect().end.y <= runtime_buttons.get_rect().position.y)
    var message_scroll_bar := message_scroll.get_v_scroll_bar()
    assert(message_scroll_bar.max_value > message_scroll_bar.page)
    message_scroll.scroll_vertical = 120
    await process_frame
    assert(message_scroll.scroll_vertical > 0)

    assert(app._edge_back_gesture_qualified(
        Vector2(8, 400),
        Vector2(120, 410),
        portrait.size.x
    ))
    assert(not app._edge_back_gesture_qualified(
        Vector2(8, 400),
        Vector2(45, 520),
        portrait.size.x
    ))
    assert(not app._edge_back_gesture_qualified(
        Vector2(8, 400),
        Vector2(120, 410),
        portrait.size.x,
        true
    ))

    app.shell_route = "settings"
    app.dirty_settings = false
    assert(not app._should_confirm_settings_navigation())
    app.dirty_settings = true
    assert(app._should_confirm_settings_navigation())
    app.shell_route = "library"
    assert(not app._should_confirm_settings_navigation())
    for language in ["zh_hans", "zh_hant", "en", "ja", "ko"]:
        app.active_language = language
        assert(not String(app._t("settings.unsaved_title")).is_empty())
        assert(not String(app._t("settings.unsaved_body")).is_empty())
        assert(not String(app._t("settings.unsaved_discard")).is_empty())
        assert(not String(app._t("settings.unsaved_close")).is_empty())

    var momentum := app._shell_scroll_momentum_spec(2500.0, 120.0, 0.0, 1200.0)
    assert(bool(momentum["active"]))
    assert(float(momentum["target"]) > 120.0)
    assert(float(momentum["duration"]) > 0.0)
    var stopped_momentum := app._shell_scroll_momentum_spec(50.0, 120.0, 0.0, 1200.0)
    assert(not bool(stopped_momentum["active"]))
    var bounded_momentum := app._shell_scroll_momentum_spec(5000.0, 1100.0, 0.0, 1200.0)
    assert(bool(bounded_momentum["active"]))
    assert(is_equal_approx(float(bounded_momentum["target"]), 1200.0))
    assert(is_equal_approx(app._shell_scroll_maximum(0.0, 1800.0, 600.0), 1200.0))
    assert(is_equal_approx(app._shell_scroll_maximum(20.0, 10.0, 100.0), 20.0))

    var released_scroll := ScrollContainer.new()
    var released_control := Button.new()
    var released_scroll_id := released_scroll.get_instance_id()
    var released_control_id := released_control.get_instance_id()
    var released_drag_state := {
        "scroll_id": released_scroll_id,
        "control_id": released_control_id,
    }
    assert(app._shell_scroll_from_drag_state(released_drag_state) == released_scroll)
    assert(app._shell_control_from_drag_state(released_drag_state) == released_control)
    released_scroll.free()
    released_control.free()
    assert(app._shell_scroll_from_drag_state(released_drag_state) == null)
    assert(app._shell_control_from_drag_state(released_drag_state) == null)
    app.shell_scroll_drag_states[17] = released_drag_state
    assert(not app._update_shell_scroll_drag(17, Vector2(0, 20), Vector2(0, 20)))
    assert(not app.shell_scroll_drag_states.has(17))

    var native_scroll_bar := VScrollBar.new()
    var scroll_bar_child := Control.new()
    native_scroll_bar.add_child(scroll_bar_child)
    assert(app._is_scroll_bar_control(native_scroll_bar))
    assert(app._is_scroll_bar_control(scroll_bar_child))
    assert(not app._is_scroll_bar_control(dialog))

    var launch_shell := Control.new()
    launch_shell.visible = true
    launch_shell.scale = Vector2(0.96, 0.96)
    launch_shell.modulate.a = 0.25
    app.shell_root = launch_shell
    app._begin_launch_transition()
    assert(not launch_shell.visible)
    assert(launch_shell.scale == Vector2.ONE)
    assert(is_equal_approx(launch_shell.modulate.a, 1.0))
    assert(app.LOADING_SPINNER_ROTATION > 0.0)
    assert(is_equal_approx(app.LOADING_SPINNER_ROTATION, TAU))
    assert(app.LOADING_SPINNER_FLIP_H)

    var immediate_loading_panel := Control.new()
    var immediate_loading_card := Control.new()
    immediate_loading_panel.add_child(immediate_loading_card)
    immediate_loading_panel.visible = false
    immediate_loading_panel.modulate.a = 0.0
    immediate_loading_card.scale = Vector2(0.5, 0.5)
    app.ui_motion.loading_in(immediate_loading_panel, immediate_loading_card, true)
    assert(immediate_loading_panel.visible)
    assert(is_equal_approx(immediate_loading_panel.modulate.a, 1.0))
    assert(immediate_loading_card.scale == Vector2.ONE)

    immediate_loading_panel.free()
    launch_shell.free()
    native_scroll_bar.free()
    dialog.free()
    runtime_dialog.free()
    app.free()
    print("MOBILE_SAFE_AREA_OK")
    quit(0)
