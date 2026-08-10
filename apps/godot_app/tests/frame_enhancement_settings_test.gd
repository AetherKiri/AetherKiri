extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

func _initialize() -> void:
    var app = MAIN_SCRIPT.new()
    var snapshot: Dictionary = app._current_settings_snapshot()
    assert(not app.frame_enhancement_enabled)
    assert(app.frame_enhancement_kind == "off")
    assert(not bool(snapshot.get("frame_enhancement_enabled", true)))
    assert(String(snapshot.get("frame_enhancement_kind", "")) == "off")
    assert("frame_enhancement_enabled" in app.SETTINGS_DRAFT_KEYS)
    assert("frame_enhancement_kind" in app.SETTINGS_DRAFT_KEYS)
    assert(app.frame_enhancement_mode == "chain_soft")
    assert(String(snapshot.get("frame_enhancement_mode", "")) == "chain_soft")
    assert("frame_enhancement_mode" in app.SETTINGS_DRAFT_KEYS)
    assert("frame_enhancement_custom_chain" in app.SETTINGS_DRAFT_KEYS)
    assert(app.frame_enhancement_custom_chain == PackedStringArray([
        "anime4k_upscale_s", "bicubic", "anime4k_restore_soft_s",
    ]))
    assert(app.output_resolution == "1080p")
    assert(String(snapshot.get("output_resolution", "")) == "1080p")
    assert("output_resolution" in app.SETTINGS_DRAFT_KEYS)

    snapshot["frame_enhancement_kind"] = "preset"
    app._apply_settings_snapshot(snapshot)
    assert(app.frame_enhancement_enabled)
    assert(app.frame_enhancement_kind == "preset")
    snapshot = app._current_settings_snapshot()
    snapshot["frame_enhancement_kind"] = "custom"
    snapshot["frame_enhancement_custom_chain"] = [
        "anime4k_upscale_vl", "lanczos", "fsr1_rcas",
    ]
    app._apply_settings_snapshot(snapshot)
    assert(app.frame_enhancement_enabled)
    assert(app.frame_enhancement_kind == "custom")
    assert(app.frame_enhancement_custom_chain == PackedStringArray([
        "anime4k_upscale_vl", "lanczos", "fsr1_rcas",
    ]))
    snapshot = app._current_settings_snapshot()
    snapshot["frame_enhancement_kind"] = "off"
    app._apply_settings_snapshot(snapshot)
    assert(not app.frame_enhancement_enabled)

    app.last_texture_size = Vector2i(1920, 1080)
    app.current_surface_size = Vector2i(1920, 1080)
    app.last_source_texture_size = Vector2i(1280, 720)
    app.output_resolution = "1080p"
    assert(app._frame_enhancement_target_size() == Vector2i(1920, 1080))
    assert(app._game_input_content_size() == Vector2(1280, 720))
    assert(app._game_input_surface_size() == Vector2(1920, 1080))
    assert(app._desired_render_surface_size() == Vector2i(1920, 1080))
    app.output_resolution = "2k"
    assert(app._frame_enhancement_target_size() == Vector2i(2560, 1440))
    assert(app._desired_render_surface_size() == Vector2i(2560, 1440))
    app.output_resolution = "4k"
    assert(app._frame_enhancement_target_size() == Vector2i(3840, 2160))
    assert(app._desired_render_surface_size() == Vector2i(3840, 2160))
    app.output_resolution = "original"
    assert(app._frame_enhancement_target_size() == Vector2i(1280, 720))
    app.last_source_texture_size = Vector2i.ZERO
    assert(app._frame_enhancement_target_size() == Vector2i.ZERO)
    app.output_resolution = "1080p"
    app.last_source_texture_size = Vector2i(1024, 768)
    assert(app._frame_enhancement_target_size() == Vector2i(1440, 1080))
    app.current_surface_size = Vector2i(960, 540)
    assert(app._game_input_content_size() == Vector2(1024, 768))
    assert(app._game_input_surface_size() == Vector2(960, 540))
    app.current_surface_size = Vector2i.ZERO
    app.last_source_texture_size = Vector2i(1280, 720)
    assert(app._game_input_content_size() == Vector2(1280, 720))
    assert(app._game_input_surface_size() == Vector2(1280, 720))

    snapshot = app._current_settings_snapshot()
    snapshot["output_resolution"] = "invalid"
    app._apply_settings_snapshot(snapshot)
    assert(app.output_resolution == "1080p")
    assert(app._normalize_output_resolution("1440p") == "2k")
    assert(app._normalize_output_resolution("2160p") == "4k")

    snapshot = app._current_settings_snapshot()
    for legacy_mode in ["anime4k", "fsr1", "bicubic", "lanczos", "ravu", "cunny", "nnedi3"]:
        snapshot["frame_enhancement_mode"] = legacy_mode
        app._apply_settings_snapshot(snapshot)
        assert(app.frame_enhancement_mode == "chain_soft")
    snapshot["frame_enhancement_mode"] = "invalid"
    app._apply_settings_snapshot(snapshot)
    assert(app.frame_enhancement_mode == "chain_soft")
    assert(app._normalize_frame_enhancement_mode("auto") == "chain_soft")
    assert(app._normalize_frame_enhancement_mode("bicubic") == "chain_soft")
    assert(app._normalize_frame_enhancement_kind("PRESET") == "preset")
    assert(app._normalize_frame_enhancement_kind("unknown") == "off")
    assert(app._normalize_frame_enhancement_custom_chain([
        "fxaa", "invalid", "CUNNY_2X4C",
    ]) == PackedStringArray(["fxaa", "cunny_2x4c"]))
    app.settings_draft = app._current_settings_snapshot()
    app.settings_draft["frame_enhancement_kind"] = "custom"
    app.settings_draft["frame_enhancement_custom_chain"] = PackedStringArray([
        "anime4k_upscale_s", "bicubic", "fsr1_rcas",
    ])
    var custom_editor := app._frame_enhancement_custom_editor()
    assert(custom_editor.get_child_count() == 4)
    for row_index in range(3):
        var custom_row := custom_editor.get_child(row_index) as HBoxContainer
        assert(custom_row != null)
        assert(custom_row.get_child_count() == 3)
        var algorithm_select = custom_row.get_child(1)
        assert(algorithm_select.item_count == app.FRAME_ENHANCEMENT_ALGORITHMS.size())
    var add_algorithm_button := custom_editor.get_child(3) as Button
    assert(add_algorithm_button != null)
    assert(add_algorithm_button.text == app._t("settings.frame_enhancement_custom_add"))
    assert(not add_algorithm_button.clip_text)
    assert(add_algorithm_button.custom_minimum_size.x >= 220.0)
    custom_editor.free()
    app._add_frame_enhancement_custom_algorithm()
    assert(app._settings_draft_custom_chain().size() == 4)
    app._select_frame_enhancement_custom_algorithm(3, "fxaa")
    assert(app._settings_draft_custom_chain()[3] == "fxaa")
    app._remove_frame_enhancement_custom_algorithm(1)
    assert(app._settings_draft_custom_chain() == PackedStringArray([
        "anime4k_upscale_s", "fsr1_rcas", "fxaa",
    ]))
    for chain_mode in [
        "chain_4k_max", "chain_lossless", "chain_ultra", "chain_detail",
        "chain_balanced", "chain_soft", "chain_light", "chain_basic",
    ]:
        assert(app._normalize_frame_enhancement_mode(chain_mode) == chain_mode)
    assert(app.FRAME_ENHANCEMENT_PRESET_MODES.size() == 8)
    for legacy_mode in ["anime4k", "fsr1", "bicubic", "lanczos", "ravu", "cunny", "nnedi3"]:
        assert(legacy_mode not in app.FRAME_ENHANCEMENT_PRESET_MODES)

    for language in ["zh_hans", "zh_hant", "en", "ja", "ko"]:
        app.active_language = language
        assert(not String(app._t("settings.output_resolution")).is_empty())
        assert(not String(app._t("settings.output_resolution_desc")).is_empty())
        assert(not String(app._t("settings.output_resolution.original")).is_empty())
        assert(not String(app._t("settings.frame_enhancement")).is_empty())
        assert(not String(app._t("settings.frame_enhancement_desc")).is_empty())
        assert(not String(app._t("settings.frame_enhancement_unavailable_desc")).is_empty())
        assert(not String(app._t("settings.frame_enhancement_mode")).is_empty())
        assert(not String(app._t("settings.frame_enhancement_mode_desc")).is_empty())
        for kind in app.FRAME_ENHANCEMENT_KINDS:
            assert(not String(app._t("settings.frame_enhancement_kind.%s" % kind)).is_empty())
        assert(not String(app._t("settings.frame_enhancement_custom_desc")).is_empty())
        assert(not String(app._t("settings.frame_enhancement_custom_empty")).is_empty())
        assert(not String(app._t("settings.frame_enhancement_custom_add")).is_empty())
        assert(not String(app._t("settings.frame_enhancement_custom_remove")).is_empty())
        var public_effect_copy := String(app._t("settings.frame_enhancement_mode_desc")).to_lower()
        for mode in app.FRAME_ENHANCEMENT_PRESET_MODES:
            var effect_label := String(app._t("settings.frame_enhancement_mode.%s" % mode))
            assert(not effect_label.is_empty())
            public_effect_copy += " " + effect_label.to_lower()
        for private_name in ["anime4k", "fsr1", "bicubic", "lanczos", "ravu", "cunny", "nnedi3"]:
            assert(not public_effect_copy.contains(private_name))
        for algorithm_id in app.FRAME_ENHANCEMENT_ALGORITHMS:
            assert(algorithm_id in app.FRAME_ENHANCEMENT_ALGORITHM_LABELS)
            assert(not String(app.FRAME_ENHANCEMENT_ALGORITHM_LABELS[algorithm_id]).is_empty())
            assert(not String(app._t(
                "settings.frame_enhancement_algorithm.%s.desc" % algorithm_id
            )).is_empty())

    app.free()
    quit(0)
