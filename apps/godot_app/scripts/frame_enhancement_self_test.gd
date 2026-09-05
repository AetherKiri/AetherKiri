extends SceneTree

func _initialize() -> void:
    var player = ClassDB.instantiate("AetherRuntimePlayer")
    if player == null:
        printerr("Frame enhancement self-test failed: AetherRuntimePlayer is unavailable")
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
        "RAVU-Lite R2",
        "CuNNy 2x4C NVL",
        "NNEDI3 nns16 win8x4",
    ]
    var ok := bool(result.get("ok", false))
    for algorithm in expected:
        ok = ok and algorithms.has(algorithm)
    ok = ok and int(result.get("width", 0)) == 39
    ok = ok and int(result.get("height", 0)) == 39
    ok = ok and int(result.get("visible_pixels", 0)) > 0
    ok = ok and int(result.get("opaque_pixels", 0)) == 39 * 39
    var pipelines: Array = result.get("pipelines", [])
    ok = ok and pipelines.size() == 15
    ok = ok and String(pipelines[0]).contains("protected")
    ok = ok and String(pipelines[0]).contains("fsr1_easu")
    ok = ok and not String(pipelines[1]).contains("protected")
    ok = ok and String(pipelines[1]).contains("fsr1_easu")
    ok = ok and not String(pipelines[2]).contains("protected")
    ok = ok and String(pipelines[2]).contains("bicubic")
    ok = ok and not String(pipelines[3]).contains("protected")
    ok = ok and String(pipelines[3]).contains("lanczos")
    ok = ok and String(pipelines[4]).contains("ravu_lite")
    ok = ok and String(pipelines[5]).contains("cunny_2x4c")
    ok = ok and String(pipelines[6]).contains("nnedi3_nns16")
    for pipeline in pipelines:
        ok = ok and not String(pipeline).contains("fxaa")
        ok = ok and not String(pipeline).contains("deband")
    for index in range(7):
        ok = ok and String(pipelines[index]).contains("fsr1_rcas_low")
    var chain_modes := [
        "chain_4k_max", "chain_lossless", "chain_ultra", "chain_detail",
        "chain_balanced", "chain_soft", "chain_light", "chain_basic",
    ]
    for index in range(chain_modes.size()):
        ok = ok and String(pipelines[index + 7]) == "multi_pass_chain:%s" % chain_modes[index]
    var profiles: Array = provider.get("profiles", [])
    for profile in ["anime4k", "fsr1", "bicubic", "lanczos", "ravu", "cunny", "nnedi3"]:
        ok = ok and profiles.has(profile)
    for profile in chain_modes:
        ok = ok and profiles.has(profile)
    ok = ok and profiles.has("custom")
    var default_algorithms: Array = provider.get("default_algorithms", [])
    ok = ok and default_algorithms.has("Anime4K Restore CNN Soft S (detail protected)")
    ok = ok and default_algorithms.has("FSR1 RCAS (low strength)")
    ok = ok and int(provider.get("version", 0)) == 7
    ok = ok and String(provider.get("selection_policy", "")) == "user_fixed"
    ok = ok and not bool(provider.get("dynamic_downgrade", true))
    ok = ok and not bool(provider.get("memory_budget_enabled", true))
    ok = ok and bool(provider.get("fixed_weight_compute", false))
    ok = ok and String(provider.get("neural_runtime_dependency", "missing")) == "none"
    # The last profile is a chain, so the standard neural path must have
    # released its intermediates instead of retaining both texture sets.
    ok = ok and int(provider.get("neural_internal_width", -1)) == 0
    ok = ok and int(provider.get("neural_internal_height", -1)) == 0
    var compiled_pipeline_counts: Array = result.get("compiled_pipeline_counts", [])
    ok = ok and compiled_pipeline_counts == [
        7, 7, 8, 9, 10, 14, 16,
        55, 55, 55, 74, 87, 91, 95, 95,
    ]
    var allocated_texture_counts: Array = result.get("allocated_texture_counts", [])
    ok = ok and allocated_texture_counts == [
        6, 4, 4, 4, 6, 7, 6,
        58, 39, 39, 23, 17, 13, 8, 9,
    ]
    var allocated_uniform_set_counts: Array = result.get("allocated_uniform_set_counts", [])
    ok = ok and allocated_uniform_set_counts == [
        8, 4, 4, 4, 5, 8, 6,
        58, 39, 39, 23, 17, 13, 8, 9,
    ]
    var texture_layouts: Array = result.get("texture_layouts", [])
    ok = ok and texture_layouts.size() == 15
    ok = ok and String(texture_layouts[0]).begins_with("restore")
    for index in range(1, 4):
        ok = ok and String(texture_layouts[index]).begins_with("scaler_only")
    for index in range(4, 7):
        ok = ok and String(texture_layouts[index]).begins_with("neural_2x")
    for index in range(7, texture_layouts.size()):
        ok = ok and String(texture_layouts[index]).begins_with("multi_pass:")
    var chain_stage_orders: Array = result.get("chain_stage_orders", [])
    ok = ok and chain_stage_orders.size() == 15
    ok = ok and chain_stage_orders[7] == [
        "reconstruct_max", "restore_max", "precision_fit",
        "reconstruct_max", "final_supersample_fit",
    ]
    ok = ok and chain_stage_orders[8] == [
        "reconstruct_max", "restore_max", "precision_fit",
    ]
    ok = ok and chain_stage_orders[9] == [
        "reconstruct_max", "precision_fit", "restore_max",
    ]
    ok = ok and chain_stage_orders[10] == [
        "reconstruct_detail", "precision_fit", "restore_detail",
    ]
    ok = ok and chain_stage_orders[11] == [
        "reconstruct_balanced", "natural_fit", "restore_balanced",
    ]
    ok = ok and chain_stage_orders[12] == [
        "reconstruct_balanced", "natural_fit", "restore_soft",
    ]
    ok = ok and chain_stage_orders[13] == ["natural_fit", "restore_light"]
    ok = ok and chain_stage_orders[14] == ["reconstruct_balanced", "natural_fit"]
    var chain_dispatch_counts: Array = result.get("chain_dispatch_counts", [])
    ok = ok and chain_dispatch_counts == [
        0, 0, 0, 0, 0, 0, 0,
        57, 38, 38, 22, 16, 12, 7, 8,
    ]
    var chain_peak_widths: Array = result.get("chain_peak_widths", [])
    var chain_peak_heights: Array = result.get("chain_peak_heights", [])
    ok = ok and chain_peak_widths == [
        0, 0, 0, 0, 0, 0, 0,
        64, 33, 34, 35, 36, 37, 38, 39,
    ]
    ok = ok and chain_peak_heights == chain_peak_widths
    var allocated_texture_bytes: Array = result.get("allocated_texture_bytes", [])
    ok = ok and allocated_texture_bytes.size() == 15
    for allocated_bytes in allocated_texture_bytes:
        ok = ok and int(allocated_bytes) > 0
    ok = ok and int(result.get("processed_delta", 0)) == 16
    ok = ok and int(result.get("anime4k_restore_run_delta", 0)) == 1
    ok = ok and int(result.get("anime4k_restore_pass_delta", 0)) == 4
    ok = ok and int(result.get("neural_upscale_run_delta", 0)) == 3
    ok = ok and int(result.get("compiled_pipeline_delta", 0)) == 95
    ok = ok and int(result.get("pipeline_compile_attempt_delta", 0)) == 95
    ok = ok and int(result.get("texture_reuse_delta", 0)) >= 1
    ok = ok and int(result.get("uniform_set_cache_hit_delta", 0)) >= 1
    ok = ok and result.get("exact_resolution_sizes", []) == [
        "1920x1080", "2560x1440", "3840x2160",
    ]
    ok = ok and result.get("exact_resolution_bytes", []) == [
        8294400, 14745600, 33177600,
    ]
    var custom_algorithms: Array = result.get("custom_algorithms", [])
    ok = ok and custom_algorithms == [
        "anime4k_upscale_s", "anime4k_upscale_l", "anime4k_upscale_vl",
        "anime4k_restore_s", "anime4k_restore_soft_s",
        "anime4k_restore_soft_m", "anime4k_restore_l",
        "anime4k_restore_vl", "fsr1_easu", "fsr1_rcas", "bicubic",
        "lanczos", "fxaa", "ravu_lite_r2", "cunny_2x4c",
        "nnedi3_nns16",
    ]
    var custom_stage_orders: Array = result.get("custom_stage_orders", [])
    ok = ok and custom_stage_orders.size() == custom_algorithms.size()
    for index in range(custom_algorithms.size()):
        ok = ok and custom_stage_orders[index] == [custom_algorithms[index]]
    ok = ok and result.get("custom_dispatch_counts", []) == [
        7, 12, 20, 6, 6, 10, 11, 19, 3, 3, 3, 3, 3, 3, 6, 4,
    ]
    ok = ok and result.get("ordered_custom_stages", []) == [
        "anime4k_upscale_s", "bicubic",
        "anime4k_restore_soft_s", "fsr1_rcas",
    ]
    ok = ok and result.get("empty_custom_stages", []) == ["implicit_output_fit"]
    ok = ok and String(result.get("invalid_custom_error", "")).begins_with(
        "unknown_custom_algorithm:"
    )
    ok = ok and int(result.get("custom_pipeline_delta", 0)) == 10
    ok = ok and bool(result.get("custom_cache_verified", false))

    var user_root := OS.get_user_data_dir()
    ok = ok and player.initialize_engine(user_root, user_root.path_join("cache"))
    if player.is_initialized():
        var host_custom_chain := PackedStringArray([
            "anime4k_upscale_s", "bicubic", "fsr1_rcas",
        ])
        player.set_frame_enhancement_custom_chain(host_custom_chain)
        player.set_frame_enhancement_mode("custom")
        var custom_host_status: Dictionary = player.get_frame_enhancement_status()
        ok = ok and String(custom_host_status.get("mode", "")) == "custom"
        ok = ok and custom_host_status.get("custom_chain", PackedStringArray()) == host_custom_chain
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
        ok = ok and bool(surface_status.get("raw_source_output", false)) == bool(
            surface_status.get("platform_raw_source", false)
        )
        player.destroy_engine()

    player.queue_free()
    quit(0 if ok else 1)
