extends SceneTree

func _initialize() -> void:
    var srt_path := "user://aetherkiri-subtitle-test.srt"
    var srt := FileAccess.open(srt_path, FileAccess.WRITE)
    srt.store_string("1\n00:00:01,000 --> 00:00:03,500\n<b>Hello</b>\nWorld\n\n2\n00:00:04.000 --> 00:00:05.000\nSecond\n")
    srt.close()
    var cues: Array[Dictionary] = VideoSubtitles.parse_file(srt_path)
    assert(cues.size() == 2)
    assert(String(VideoSubtitles.text_at(cues, 2.0).get("text")) == "Hello\nWorld")
    assert(String(VideoSubtitles.text_at(cues, 3.8).get("text")).is_empty())

    var ass_path := "user://aetherkiri-subtitle-test.ass"
    var ass := FileAccess.open(ass_path, FileAccess.WRITE)
    ass.store_string(
        "[Events]\n"
        + "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        + "Dialogue: 6,0:00:02.00,0:00:04.00,Text CN,,0,0,0,,{\\i1}中文\n"
        + "Dialogue: 5,0:00:02.00,0:00:04.00,Text JP,,0,0,0,,日本語\n"
        + "Dialogue: 5,0:00:02.00,0:00:04.00,Text JP,,0,0,0,,日本語\n"
        + "Dialogue: 0,0:00:03.00,0:00:05.00,Signs,,0,0,0,,Overlap\n"
    )
    ass.close()
    var ass_cues: Array[Dictionary] = VideoSubtitles.parse_file(ass_path)
    assert(ass_cues.size() == 2)
    assert(String(VideoSubtitles.text_at(ass_cues, 2.5).get("text")) == "中文\n日本語")
    assert(String(VideoSubtitles.text_at(ass_cues, 3.5).get("text")) == "中文\n日本語\nOverlap")
    assert(String(VideoSubtitles.text_at(ass_cues, 4.5).get("text")) == "Overlap")
    DirAccess.remove_absolute(ProjectSettings.globalize_path(srt_path))
    DirAccess.remove_absolute(ProjectSettings.globalize_path(ass_path))
    print("VIDEO_SUBTITLES_OK")
    quit(0)
