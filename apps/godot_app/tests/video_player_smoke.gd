extends SceneTree

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    var path := OS.get_environment("AETHERKIRI_TEST_VIDEO")
    if path.is_empty():
        push_error("AETHERKIRI_TEST_VIDEO is required")
        quit(2)
        return
    var instance: Object = ClassDB.instantiate("AetherKiriPlayer")
    if instance == null or not (instance is Node):
        push_error("AetherKiriPlayer is unavailable")
        quit(3)
        return
    root.add_child(instance as Node)
    var player = instance
    var test_root := ProjectSettings.globalize_path("user://video-smoke")
    DirAccess.make_dir_recursive_absolute(test_root)
    if not player.initialize_engine(test_root, test_root.path_join("cache")):
        push_error("engine init failed: %s" % player.get_last_error())
        quit(4)
        return
    if not player.media_open(path):
        push_error("media open failed: %s" % player.get_last_error())
        player.destroy_engine()
        quit(5)
        return
    player.media_play()
    var deadline := Time.get_ticks_msec() + 5000
    var received_frame := false
    var observed_duration := 0.0
    while Time.get_ticks_msec() < deadline:
        var state: Dictionary = player.media_get_state()
        observed_duration = maxf(observed_duration, float(state.get("duration", 0.0)))
        if bool(state.get("frame_ready", false)):
            var texture = player.media_update_texture()
            if texture != null and texture.get_width() > 0 and texture.get_height() > 0:
                received_frame = true
                break
        await create_timer(0.02).timeout
    player.media_seek(10.0)
    player.media_play()
    var controls_worked := true
    var rate_dwell_seconds := 0.15
    var configured_dwell := OS.get_environment("AETHERKIRI_TEST_RATE_DWELL_SECONDS")
    if not configured_dwell.is_empty():
        rate_dwell_seconds = maxf(0.15, configured_dwell.to_float())
    for expected_rate in [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]:
        player.media_set_rate(expected_rate)
        var rate_deadline := Time.get_ticks_msec() + 1000
        var observed_rate := false
        while Time.get_ticks_msec() < rate_deadline:
            var control_state: Dictionary = player.media_get_state()
            if (
                float(control_state.get("position", 0.0)) >= 9.0
                and absf(float(control_state.get("rate", 0.0)) - expected_rate) < 0.01
            ):
                observed_rate = true
                break
            await create_timer(0.02).timeout
        if not observed_rate:
            controls_worked = false
            break
        await create_timer(rate_dwell_seconds).timeout
    player.media_set_rate(1.25)
    player.media_pause()
    await create_timer(0.1).timeout
    player.media_play()
    var resume_deadline := Time.get_ticks_msec() + 1000
    var resume_kept_rate := false
    while Time.get_ticks_msec() < resume_deadline:
        var resume_state: Dictionary = player.media_get_state()
        if absf(float(resume_state.get("rate", 0.0)) - 1.25) < 0.01:
            resume_kept_rate = true
            break
        await create_timer(0.02).timeout
    controls_worked = controls_worked and resume_kept_rate
    player.media_pause()
    player.media_close()
    player.destroy_engine()
    if not received_frame:
        push_error("media decoder did not produce a frame")
        quit(6)
        return
    if not controls_worked:
        push_error("media seek/rate controls did not update state")
        quit(7)
        return
    print("VIDEO_SMOKE_OK duration=%.3f" % observed_duration)
    quit(0)
