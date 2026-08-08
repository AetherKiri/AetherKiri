extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

func _initialize() -> void:
    var app = MAIN_SCRIPT.new()
    app.frame_enhancement_enabled = true
    var snapshot: Dictionary = app._current_settings_snapshot()
    assert(bool(snapshot.get("frame_enhancement_enabled", false)))
    assert("frame_enhancement_enabled" in app.SETTINGS_DRAFT_KEYS)
    assert(app.frame_enhancement_mode == "anime4k")
    assert(String(snapshot.get("frame_enhancement_mode", "")) == "anime4k")
    assert("frame_enhancement_mode" in app.SETTINGS_DRAFT_KEYS)
    assert(app.output_resolution == "1080p")
    assert(String(snapshot.get("output_resolution", "")) == "1080p")
    assert("output_resolution" in app.SETTINGS_DRAFT_KEYS)

    snapshot["frame_enhancement_enabled"] = false
    app._apply_settings_snapshot(snapshot)
    assert(not app.frame_enhancement_enabled)

    app.last_texture_size = Vector2i(1920, 1080)
    app.current_surface_size = Vector2i(1920, 1080)
    app.last_source_texture_size = Vector2i(1280, 720)
    app.output_resolution = "1080p"
    assert(app._frame_enhancement_target_size() == Vector2i(1920, 1080))
    assert(app._game_input_texture_size() == Vector2(1920, 1080))
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
    assert(app._game_input_texture_size() == Vector2(960, 540))
    app.current_surface_size = Vector2i.ZERO
    app.last_source_texture_size = Vector2i(1280, 720)
    assert(app._game_input_texture_size() == Vector2(1280, 720))

    snapshot = app._current_settings_snapshot()
    snapshot["output_resolution"] = "invalid"
    app._apply_settings_snapshot(snapshot)
    assert(app.output_resolution == "1080p")
    assert(app._normalize_output_resolution("1440p") == "2k")
    assert(app._normalize_output_resolution("2160p") == "4k")

    snapshot = app._current_settings_snapshot()
    snapshot["frame_enhancement_mode"] = "fsr1"
    app._apply_settings_snapshot(snapshot)
    assert(app.frame_enhancement_mode == "fsr1")
    snapshot["frame_enhancement_mode"] = "lanczos"
    app._apply_settings_snapshot(snapshot)
    assert(app.frame_enhancement_mode == "lanczos")
    for neural_mode in ["ravu", "cunny", "nnedi3"]:
        snapshot["frame_enhancement_mode"] = neural_mode
        app._apply_settings_snapshot(snapshot)
        assert(app.frame_enhancement_mode == neural_mode)
    snapshot["frame_enhancement_mode"] = "invalid"
    app._apply_settings_snapshot(snapshot)
    assert(app.frame_enhancement_mode == "anime4k")
    assert(app._normalize_frame_enhancement_mode("auto") == "anime4k")
    assert(app._normalize_frame_enhancement_mode("bicubic") == "bicubic")
    for chain_mode in [
        "chain_4k_max", "chain_lossless", "chain_ultra", "chain_detail",
        "chain_balanced", "chain_soft", "chain_light", "chain_basic",
    ]:
        assert(app._normalize_frame_enhancement_mode(chain_mode) == chain_mode)

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
        var public_effect_copy := String(app._t("settings.frame_enhancement_mode_desc")).to_lower()
        for mode in app.FRAME_ENHANCEMENT_MODES:
            var effect_label := String(app._t("settings.frame_enhancement_mode.%s" % mode))
            assert(not effect_label.is_empty())
            public_effect_copy += " " + effect_label.to_lower()
        for private_name in ["anime4k", "fsr1", "bicubic", "lanczos", "ravu", "cunny", "nnedi3"]:
            assert(not public_effect_copy.contains(private_name))

    app.free()
    quit(0)
