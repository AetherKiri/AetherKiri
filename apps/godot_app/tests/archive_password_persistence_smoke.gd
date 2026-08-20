extends SceneTree

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    var main_script: Script = load("res://scripts/main.gd")
    if main_script == null:
        push_error("archive_password_persistence_smoke: main script unavailable")
        quit(1)
        return
    var main: Object = main_script.new()
    var password_file := ProjectSettings.globalize_path("user://aetherkiri_archive_passwords.cfg")
    DirAccess.remove_absolute(password_file)
    main._remember_archive_password("alpha")
    main._remember_archive_password("beta")
    main._remember_archive_password("alpha")
    var passwords: Array[String] = main._load_archive_passwords()
    if passwords != ["alpha", "beta"]:
        push_error("archive_password_persistence_smoke: unexpected order %s" % str(passwords))
        main.free()
        quit(1)
        return
    print("ARCHIVE_PASSWORD_PERSISTENCE_SMOKE_OK")
    DirAccess.remove_absolute(password_file)
    main.free()
    quit(0)
