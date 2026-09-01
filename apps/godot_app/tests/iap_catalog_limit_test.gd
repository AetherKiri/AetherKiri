extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

class BetaProbePlayer extends RefCounted:
    var scores := {}

    func probe_runtime(runtime_id: String, _game_path: String) -> int:
        return int(scores.get(runtime_id, 0))

func _initialize() -> void:
    var app = MAIN_SCRIPT.new()
    var games: Array[Dictionary] = [
        {"path": "/games/first"},
        {"path": "/games/second"},
    ]
    var videos: Array[Dictionary] = [
        {"path": "/videos/first.mp4"},
        {"path": "/videos/second.mp4"},
    ]
    app.known_games = games
    app.known_videos = videos

    assert(app._iap_item_is_first("game", {"path": "/games/first"}))
    assert(not app._iap_item_is_first("game", {"path": "/games/second"}))
    assert(app._iap_item_is_first("video", {"path": "/videos/first.mp4"}))
    assert(not app._iap_item_is_first("video", {"path": "/videos/second.mp4"}))
    assert(not app._iap_item_is_first("video", {"path": "/videos/missing.mp4"}))

    # Debug artifacts must never enforce the catalog limit or show its prompt.
    app.modal_layer = Control.new()
    app.modal_layer.visible = false
    assert(OS.is_debug_build())
    assert(not app._iap_enforcement_enabled())
    assert(not app._begin_iap_checked_access("game", games[1], "detail"))
    assert(app.iap_pending_launch.is_empty())
    assert(not app.modal_layer.visible)
    assert(app._runtime_requires_beta_access(app.RUNTIME_ONSCRIPTER))
    assert(not app._runtime_requires_beta_access(app.RUNTIME_KIRIKIRI))
    assert(app._provider_runtime_requires_beta_access("artemis"))
    assert(app._provider_runtime_requires_beta_access("catsystem2"))
    assert(not app._provider_runtime_requires_beta_access("fake"))
    var probe_player := BetaProbePlayer.new()
    probe_player.scores = {"catsystem2": 120}
    app.player = probe_player
    app.selected_game = {"path": "/games/catsystem2"}
    assert(app._selected_game_uses_beta_provider())
    probe_player.scores = {"fake": 120}
    assert(not app._selected_game_uses_beta_provider())
    assert(not app._beta_access_enforcement_enabled("Android"))
    assert(not app._beta_access_enforcement_enabled("iOS"))

    var settings_action := Button.new()
    app._configure_settings_action_button(settings_action)
    assert(settings_action.custom_minimum_size == Vector2(150, 54))
    assert(settings_action.size_flags_horizontal == Control.SIZE_SHRINK_END)
    assert(settings_action.size_flags_vertical == Control.SIZE_SHRINK_CENTER)

    for language in ["zh_hans", "zh_hant", "en", "ja", "ko"]:
        app.active_language = language
        assert(not String(app._t("iap.list_limit.title")).is_empty())
        assert(not String(app._t("iap.limit_body")).is_empty())
        assert(not String(app._t("iap.restore")).is_empty())
        assert(not String(app._t("iap.coffee.title")).is_empty())
        assert(not String(app._t("iap.coffee.desc")).is_empty())
        assert(not String(app._t("iap.coffee.active_until", ["2030-01-01"])).is_empty())
        assert(not String(app._t("iap.runtime_unavailable")).is_empty())

    settings_action.free()
    app.modal_layer.free()
    app.free()
    print("IAP_CATALOG_LIMIT_OK")
    quit(0)
