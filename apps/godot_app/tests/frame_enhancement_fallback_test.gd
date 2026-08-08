extends SceneTree

func _initialize() -> void:
    var player = ClassDB.instantiate("AetherKiriPlayer")
    assert(player != null)
    root.add_child(player as Node)

    assert(not player.is_frame_enhancement_built())
    assert(not player.is_frame_enhancement_available())
    player.set_frame_enhancement_mode("fsr1")
    player.set_frame_enhancement_target_size(1280, 720)
    player.set_frame_enhancement_enabled(true)

    var status: Dictionary = player.get_frame_enhancement_status()
    assert(not bool(status.get("built", true)))
    assert(not bool(status.get("available", true)))
    assert(not bool(status.get("active", true)))

    var user_dir := OS.get_user_data_dir()
    assert(player.initialize_engine(user_dir, user_dir.path_join("cache")))
    player.set_frame_native_output_enabled(true)
    var native_status: Dictionary = player.get_frame_enhancement_status()
    assert(bool(native_status.get("native_output_requested", false)))
    assert(bool(native_status.get("raw_source_output", false)))
    player.set_frame_native_output_enabled(false)
    var surface_status: Dictionary = player.get_frame_enhancement_status()
    assert(not bool(surface_status.get("raw_source_output", true)))
    player.destroy_engine()

    player.queue_free()
    quit(0)
