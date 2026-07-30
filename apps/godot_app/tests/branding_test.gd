extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")
const EXPECTED_SUBTITLES := {
    "zh_hans": "多功能媒体播放器",
    "zh_hant": "多功能媒體播放器",
    "en": "Multifunction Media Player",
    "ja": "多機能メディアプレーヤー",
    "ko": "다기능 미디어 플레이어",
}

func _initialize() -> void:
    assert(String(ProjectSettings.get_setting("application/config/name")) == "Aether")
    if OS.get_name() == "macOS":
        assert(OS.get_user_data_dir().ends_with("/Godot/app_userdata/AetherKiri"))
    var app = MAIN_SCRIPT.new()
    for language in EXPECTED_SUBTITLES:
        app.active_language = language
        assert(String(app._t("home.subtitle")) == String(EXPECTED_SUBTITLES[language]))
    app.free()
    print("BRANDING_OK user_dir=%s" % OS.get_user_data_dir())
    quit(0)
