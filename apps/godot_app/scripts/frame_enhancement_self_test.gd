extends SceneTree

func _initialize() -> void:
    var player = ClassDB.instantiate("AetherKiriPlayer")
    if player == null:
        printerr("Frame enhancement self-test failed: AetherKiriPlayer is unavailable")
        quit(1)
        return
    root.add_child(player as Node)

    var result: Dictionary = player.debug_frame_enhancement_self_test()
    print("Frame enhancement self-test: %s" % JSON.stringify(result))
    var provider: Dictionary = result.get("provider", {})
    var algorithms: Array = provider.get("algorithms", [])
    var expected := [
        "Anime4K Restore CNN Soft S",
        "FSR1 EASU",
        "FSR1 RCAS",
        "Bicubic",
        "Lanczos3",
    ]
    var ok := bool(result.get("ok", false))
    for algorithm in expected:
        ok = ok and algorithms.has(algorithm)
    ok = ok and int(result.get("width", 0)) == 26
    ok = ok and int(result.get("height", 0)) == 26
    ok = ok and int(result.get("visible_pixels", 0)) > 0
    ok = ok and int(result.get("opaque_pixels", 0)) == 26 * 26
    var pipelines: Array = result.get("pipelines", [])
    ok = ok and pipelines.size() == 4
    ok = ok and String(pipelines[0]).contains("protected")
    ok = ok and String(pipelines[0]).contains("fsr1_easu")
    ok = ok and not String(pipelines[1]).contains("protected")
    ok = ok and String(pipelines[1]).contains("fsr1_easu")
    ok = ok and not String(pipelines[2]).contains("protected")
    ok = ok and String(pipelines[2]).contains("bicubic")
    ok = ok and not String(pipelines[3]).contains("protected")
    ok = ok and String(pipelines[3]).contains("lanczos")
    for pipeline in pipelines:
        ok = ok and not String(pipeline).contains("fxaa")
        ok = ok and not String(pipeline).contains("deband")
        ok = ok and String(pipeline).contains("fsr1_rcas_low")
    var profiles: Array = provider.get("profiles", [])
    for profile in ["anime4k", "fsr1", "bicubic", "lanczos"]:
        ok = ok and profiles.has(profile)
    var default_algorithms: Array = provider.get("default_algorithms", [])
    ok = ok and default_algorithms.has("Anime4K Restore CNN Soft S (detail protected)")
    ok = ok and default_algorithms.has("FSR1 RCAS (low strength)")
    ok = ok and int(provider.get("version", 0)) == 4
    ok = ok and String(provider.get("selection_policy", "")) == "user_fixed"
    ok = ok and not bool(provider.get("dynamic_downgrade", true))
    ok = ok and not bool(provider.get("memory_budget_enabled", true))
    var compiled_pipeline_counts: Array = result.get("compiled_pipeline_counts", [])
    ok = ok and compiled_pipeline_counts == [7, 7, 8, 9]
    var allocated_texture_counts: Array = result.get("allocated_texture_counts", [])
    ok = ok and allocated_texture_counts == [6, 4, 4, 4]
    var allocated_uniform_set_counts: Array = result.get("allocated_uniform_set_counts", [])
    ok = ok and allocated_uniform_set_counts == [8, 4, 4, 4]
    var texture_layouts: Array = result.get("texture_layouts", [])
    ok = ok and texture_layouts.size() == 4
    ok = ok and String(texture_layouts[0]).begins_with("restore")
    for index in range(1, texture_layouts.size()):
        ok = ok and String(texture_layouts[index]).begins_with("scaler_only")
    var allocated_texture_bytes: Array = result.get("allocated_texture_bytes", [])
    ok = ok and allocated_texture_bytes.size() == 4
    for index in range(1, allocated_texture_bytes.size()):
        ok = ok and int(allocated_texture_bytes[index]) < int(allocated_texture_bytes[0])
    ok = ok and int(result.get("processed_delta", 0)) == 5
    ok = ok and int(result.get("anime4k_restore_run_delta", 0)) == 1
    ok = ok and int(result.get("anime4k_restore_pass_delta", 0)) == 4
    ok = ok and int(result.get("compiled_pipeline_delta", 0)) == 9
    ok = ok and int(result.get("pipeline_compile_attempt_delta", 0)) == 9
    ok = ok and int(result.get("texture_reuse_delta", 0)) >= 1
    ok = ok and int(result.get("uniform_set_cache_hit_delta", 0)) >= 1

    var user_root := OS.get_user_data_dir()
    ok = ok and player.initialize_engine(user_root, user_root.path_join("cache"))
    if player.is_initialized():
        player.set_frame_native_output_enabled(false)
        player.set_frame_enhancement_enabled(true)
        var enabled_status: Dictionary = player.get_frame_enhancement_status()
        ok = ok and bool(enabled_status.get("raw_source_output", false))
        ok = ok and not bool(enabled_status.get("native_output_requested", true))
        player.set_frame_native_output_enabled(true)
        player.set_frame_enhancement_enabled(false)
        var native_status: Dictionary = player.get_frame_enhancement_status()
        ok = ok and bool(native_status.get("native_output_requested", false))
        ok = ok and bool(native_status.get("raw_source_output", false))
        player.set_frame_native_output_enabled(false)
        var surface_status: Dictionary = player.get_frame_enhancement_status()
        ok = ok and not bool(surface_status.get("raw_source_output", true))
        player.destroy_engine()

    player.queue_free()
    quit(0 if ok else 1)
