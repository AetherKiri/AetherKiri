extends SceneTree

func _initialize() -> void:
    assert(ClassDB.class_exists("AetherOnscripterPlayer"))
    var player = ClassDB.instantiate("AetherOnscripterPlayer")
    assert(player != null)
    root.add_child(player as Node)

    assert(player.has_method("is_frame_enhancement_built"))
    assert(player.has_method("is_frame_enhancement_available"))
    assert(player.has_method("set_frame_enhancement_enabled"))
    assert(player.has_method("set_frame_native_output_enabled"))
    assert(player.has_method("set_frame_enhancement_mode"))
    assert(player.has_method("set_frame_enhancement_custom_chain"))
    assert(player.has_method("set_frame_enhancement_target_size"))
    assert(player.has_method("get_frame_source_size"))
    assert(player.has_method("get_frame_enhancement_status"))

    player.set_frame_enhancement_mode("chain_soft")
    player.set_frame_enhancement_custom_chain(PackedStringArray([
        "anime4k_upscale_s", "bicubic", "anime4k_restore_soft_s",
    ]))
    player.set_frame_enhancement_target_size(1440, 1080)
    player.set_frame_enhancement_enabled(true)
    var status: Dictionary = player.get_frame_enhancement_status()
    assert(bool(status.get("enabled", false)))
    assert(String(status.get("mode", "")) == "chain_soft")
    assert(String(status.get("runtime", "")) == "onscripter")
    assert(bool(status.get("raw_source_output", false)))
    assert(int(status.get("target_width", 0)) == 1440)
    assert(int(status.get("target_height", 0)) == 1080)

    player.destroy_engine()
    player.queue_free()
    print("ONSCRIPTER_FRAME_ENHANCEMENT_CONTRACT_OK")
    quit(0)
