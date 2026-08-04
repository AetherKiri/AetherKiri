extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")
const EXPECTED_SUBTITLES := {
    "zh_hans": "多功能媒体播放器",
    "zh_hant": "多功能媒體播放器",
    "en": "Multifunction Media Player",
    "ja": "多機能メディアプレーヤー",
    "ko": "다기능 미디어 플레이어",
}
const EXPECTED_LIBRARY_LABELS := {
    "zh_hans": "视觉小说",
    "zh_hant": "視覺小說",
    "en": "Visual Novels",
    "ja": "ビジュアルノベル",
    "ko": "비주얼 노벨",
}

func _initialize() -> void:
    assert(String(ProjectSettings.get_setting("application/config/name")) == "Aether")
    if OS.get_name() == "macOS":
        assert(OS.get_user_data_dir().ends_with("/Godot/app_userdata/AetherKiri"))
    var app = MAIN_SCRIPT.new()
    assert(app.APP_DISPLAY_NAME == "Aether")
    assert(app.style_mode == app.STYLE_CLASSIC)
    assert(app._normalize_style_mode("invalid") == app.STYLE_CLASSIC)
    var title_font: FontVariation = app._game_title_font()
    var text_server := TextServerManager.get_primary_interface()
    assert(int(title_font.opentype_features.get(text_server.name_to_tag("lnum"), 0)) == 1)
    assert(int(title_font.opentype_features.get(text_server.name_to_tag("onum"), 1)) == 0)
    for language in EXPECTED_SUBTITLES:
        app.active_language = language
        assert(String(app._t("home.subtitle")) == String(EXPECTED_SUBTITLES[language]))
        assert(String(app._t("nav.library")) == String(EXPECTED_LIBRARY_LABELS[language]))
    app.free()
    print("BRANDING_OK user_dir=%s" % OS.get_user_data_dir())
    quit(0)
