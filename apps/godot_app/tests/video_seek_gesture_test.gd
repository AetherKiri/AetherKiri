extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

class FakePlayer:
    extends RefCounted

    var last_seek := -1.0

    func media_seek(position: float) -> int:
        last_seek = position
        return 0

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    var app = MAIN_SCRIPT.new()
    root.add_child(app)
    app.video_view = Control.new()
    app.video_view.size = Vector2(1000, 600)
    app.add_child(app.video_view)
    app.video_top_bar = PanelContainer.new()
    app.video_controls = PanelContainer.new()
    app.video_subtitle_label = Label.new()
    app.video_progress_slider = HSlider.new()
    app.video_time_label = Label.new()
    app.video_seek_feedback = PanelContainer.new()
    app.video_seek_feedback_label = Label.new()
    app.video_view.add_child(app.video_top_bar)
    app.video_view.add_child(app.video_controls)
    app.video_view.add_child(app.video_subtitle_label)
    app.video_view.add_child(app.video_progress_slider)
    app.video_view.add_child(app.video_time_label)
    app.video_view.add_child(app.video_seek_feedback)
    app.video_seek_feedback.add_child(app.video_seek_feedback_label)
    app.active_video_state = {
        "position": 100.0,
        "duration": 1000.0,
        "status": 1,
    }
    app.active_video_duration = 1000.0
    app.player = FakePlayer.new()

    app.video_seek_mouse_pressed = true
    app._begin_video_seek_gesture(Vector2(500, 300))
    assert(app._update_video_seek_gesture(Vector2(750, 300)))
    assert(is_equal_approx(app.video_seek_target_position, 125.0))
    assert(app.video_seek_feedback.visible)
    app.video_seek_mouse_pressed = false
    app._finish_video_seek_gesture(Vector2(750, 300))
    assert(is_equal_approx(app.player.last_seek, 125.0))
    assert(not app.video_seek_gesture_active)
    assert(not app.video_seek_feedback.visible)

    app.active_video_state["position"] = 100.0
    app.video_seek_touch_index = 0
    app._begin_video_seek_gesture(Vector2(500, 300))
    assert(app._update_video_seek_gesture(Vector2(300, 300)))
    assert(is_equal_approx(app.video_seek_target_position, 80.0))
    app.video_seek_touch_index = -1
    app._finish_video_seek_gesture(Vector2(300, 300))
    assert(is_equal_approx(app.player.last_seek, 80.0))

    app._set_video_controls_visible(false, false)
    app._begin_video_seek_gesture(Vector2(500, 300))
    app._finish_video_seek_gesture(Vector2(500, 300))
    assert(app.video_controls_visible)

    app.video_pending_resume_position = 321.0
    var waiting_state := {"position": 0.0, "duration": 0.0, "seekable": false}
    assert(not app._apply_pending_video_resume(waiting_state))
    assert(is_equal_approx(app.video_pending_resume_position, 321.0))
    var ready_state := {"position": 0.0, "duration": 1000.0, "seekable": true}
    assert(app._apply_pending_video_resume(ready_state))
    assert(is_equal_approx(app.player.last_seek, 321.0))
    assert(is_equal_approx(float(ready_state["position"]), 321.0))
    assert(is_zero_approx(app.video_pending_resume_position))

    var progress_card := Control.new()
    app.add_child(progress_card)
    app._add_video_card_progress(progress_card, 250.0, 1000.0)
    var progress_track := progress_card.get_node("PlaybackProgressTrack") as ColorRect
    var progress_fill := progress_track.get_node("PlaybackProgressFill") as ColorRect
    assert(is_equal_approx(progress_fill.anchor_right, 0.25))

    app.queue_free()
    print("VIDEO_SEEK_GESTURE_OK")
    quit(0)
