extends SceneTree

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    var player: Object = null
    if ClassDB.class_exists("AetherRuntimePlayer"):
        player = ClassDB.instantiate("AetherRuntimePlayer")
    if player == null or not player.has_method("archive_import_probe"):
        push_error("archive_import_smoke: native extension unavailable")
        if player != null:
            player.free()
        quit(1)
        return
    var source := OS.get_environment("AETHERKIRI_ARCHIVE_SMOKE_SOURCE")
    var destination := OS.get_environment("AETHERKIRI_ARCHIVE_SMOKE_DEST")
    if source.is_empty() or destination.is_empty():
        push_error("archive_import_smoke: source/dest env missing")
        player.free()
        quit(1)
        return
    var probe: Dictionary = player.archive_import_probe(source)
    if OS.get_environment("AETHERKIRI_ARCHIVE_SMOKE_EXPECT_UNRECOGNIZED") == "1":
        if bool(probe.get("recognized", false)):
            push_error("archive_import_smoke: unexpected recognition: %s" % str(probe))
            player.free()
            quit(1)
            return
        print("ARCHIVE_IMPORT_SMOKE_UNRECOGNIZED_OK")
        player.free()
        quit(0)
        return
    if not bool(probe.get("recognized", false)):
        push_error("archive_import_smoke: probe failed: %s" % str(probe))
        player.free()
        quit(1)
        return
    var result: Dictionary = player.archive_import_extract(
        source, destination, OS.get_environment("AETHERKIRI_ARCHIVE_SMOKE_PASSWORD"))
    if not bool(result.get("ok", false)):
        if bool(result.get("password_required", false)):
            print("ARCHIVE_IMPORT_SMOKE_PASSWORD_REQUIRED format=%s encrypted=%s" % [str(probe.get("format", "")), str(probe.get("encrypted", false))])
            player.free()
            quit(0)
            return
        push_error("archive_import_smoke: extract failed: %s" % str(result))
        player.free()
        quit(1)
        return
    var output_path := String(result.get("output_path", ""))
    if output_path.is_empty() or not DirAccess.dir_exists_absolute(output_path):
        push_error("archive_import_smoke: output path missing")
        player.free()
        quit(1)
        return
    print("ARCHIVE_IMPORT_SMOKE_OK format=%s output=%s" % [str(probe.get("format", "")), output_path])
    player.free()
    quit(0)
