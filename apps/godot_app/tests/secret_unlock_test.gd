extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

func _initialize() -> void:
    var app = MAIN_SCRIPT.new()
    app.modal_layer = Control.new()
    app.modal_layer.visible = false

    # The prompt must stay hidden before the 20th tap inside the window.
    for tap in range(19):
        app._register_secret_version_tap()
    assert(not app.modal_layer.visible)
    assert(app.secret_version_tap_count == 19)

    # Idling past the sliding window restarts the count from zero.
    app.secret_version_last_tap_msec -= app.SECRET_UNLOCK_TAP_WINDOW_MSEC + 1
    app._register_secret_version_tap()
    assert(app.secret_version_tap_count == 1)
    assert(not app.modal_layer.visible)

    # Completing the window opens the passphrase prompt.
    for tap in range(19):
        app._register_secret_version_tap()
    assert(app.modal_layer.visible)

    # Verification requires the compiled unlock gate; without the bridge
    # nothing unlocks and no grant is persisted.
    assert(not app._verify_secret_unlock("irrelevant"))
    assert(not app.secret_iap_unlocked)

    # A secret coffee grant follows wall-clock expiry.
    var now := int(Time.get_unix_time_from_system())
    app.secret_coffee_until_unix = now - 1
    assert(not app._secret_coffee_active())
    app.secret_coffee_until_unix = now + 3600
    assert(app._secret_coffee_active())
    assert(not String(app._secret_coffee_expiry_text()).is_empty())

    # Granting the unlock keeps one month of beta access and removes the
    # catalog limit for this installation.
    app._apply_secret_unlock()
    assert(app.secret_iap_unlocked)
    assert(app._secret_iap_unlock_active())
    assert(app.secret_coffee_until_unix >= now + app.SECRET_UNLOCK_COFFEE_SEC)
    assert(app._iap_enforcement_enabled() == (
        app._iap_supported_platform()
        and not OS.is_debug_build()
        and not app._secret_iap_unlock_active()
    ))

    for language in ["zh_hans", "zh_hant", "en", "ja", "ko"]:
        app.active_language = language
        assert(not String(app._t("secret.unlock.title")).is_empty())
        assert(not String(app._t("secret.unlock.body")).is_empty())
        assert(not String(app._t("secret.unlock.confirm")).is_empty())
        assert(not String(app._t("secret.unlock.failed")).is_empty())
        assert(not String(app._t("secret.unlock.success", ["2030-01-01 00:00"])).is_empty())

    app.modal_layer.free()
    app.free()
    print("SECRET_UNLOCK_OK")
    quit(0)
