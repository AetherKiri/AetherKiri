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
    assert(app.APP_SERVICE_FILING_NUMBER == "沪ICP备2026042398号-2A")
    assert(app.APP_SERVICE_FILING_URL == "https://beian.miit.gov.cn/#/Integrated/index")
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
        assert(not String(app._t("search.games_placeholder")).is_empty())
        assert(not String(app._t("search.videos_placeholder")).is_empty())
        assert(not String(app._t("search.no_results_title")).is_empty())
        assert(not String(app._t("settings.app_service_filing")).is_empty())
        assert(not String(app._t("settings.app_service_filing_open_failed")).is_empty())
    var filing_row: Control = app._settings_link_value_row(
        app._t("settings.app_service_filing"),
        app.APP_SERVICE_FILING_NUMBER,
        func(): pass
    )
    var filing_link := filing_row.get_child(0).get_child(1) as LinkButton
    assert(filing_link != null)
    assert(filing_link.text == app.APP_SERVICE_FILING_NUMBER)
    assert(filing_link.underline == LinkButton.UNDERLINE_MODE_ALWAYS)
    assert(app._library_search_matches(
        ["Cafe Stella", "/Games/CafeStella"],
        "cafe stel"
    ))
    assert(app._library_search_matches(
        ["[ANI] BanG Dream!", "episode-03.mkv"],
        "dream 03"
    ))
    assert(not app._library_search_matches(
        ["Cafe Stella", "/Games/CafeStella"],
        "nekopara"
    ))
    assert(app._is_runtime_exit_error("runtime requested termination"))
    assert(app._is_runtime_exit_error("runtime has been terminated"))
    assert(not app._is_runtime_exit_error("unexpected renderer failure"))
    app.free()
    print("BRANDING_OK user_dir=%s" % OS.get_user_data_dir())
    quit(0)
