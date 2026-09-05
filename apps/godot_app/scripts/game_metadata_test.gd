extends SceneTree

const GameMetadata = preload("res://scripts/game_metadata.gd")

var failures := 0
var fixture_root := ""


func _init() -> void:
    fixture_root = ProjectSettings.globalize_path(
        "user://game_metadata_test_%d" % Time.get_ticks_usec()
    )
    DirAccess.make_dir_recursive_absolute(fixture_root)

    var artemis_root := fixture_root.path_join("mobile_artemis")
    DirAccess.make_dir_recursive_absolute(artemis_root)
    _write(artemis_root.path_join("launcher.exe"), "launcher")
    _write(artemis_root.path_join("root.pfs"), "pf fixture")
    _write(
        artemis_root.path_join("system.ini"),
        "[ANDROID]\nWIDTH = 1024\nHEIGHT = 768\nboot = system/first.iet\n"
    )
    var artemis := GameMetadata.inspect(artemis_root)
    _expect_equal(String(artemis.engine), "artemis", "PFS plus IET detection")
    _expect_equal(String(artemis.launchFile), "", "Artemis ignores Windows launcher")
    _expect_true(
        Array(artemis.signals).has("artemis-pfs-boot"),
        "structural Artemis signal"
    )

    var kirikiri_root := fixture_root.path_join("kirikiri")
    DirAccess.make_dir_recursive_absolute(kirikiri_root)
    _write(kirikiri_root.path_join("game.exe"), "launcher")
    _write(kirikiri_root.path_join("data.xp3"), "archive")
    _write(kirikiri_root.path_join("system.ini"), "[Game]\nBOOT = startup.tjs\n")
    var kirikiri := GameMetadata.inspect(kirikiri_root)
    _expect_equal(String(kirikiri.engine), "kirikiri", "KiriKiri remains default")
    _expect_equal(String(kirikiri.launchFile), "game.exe", "KiriKiri launcher retained")

    _remove_tree(fixture_root)
    if failures == 0:
        print("game_metadata_test: PASS")
        quit(0)
    else:
        quit(1)


func _write(path: String, contents: String) -> void:
    var file := FileAccess.open(path, FileAccess.WRITE)
    if file == null:
        push_error("game_metadata_test: could not create %s" % path)
        failures += 1
        return
    file.store_string(contents)


func _remove_tree(path: String) -> void:
    var dir := DirAccess.open(path)
    if dir == null:
        return
    dir.list_dir_begin()
    var entry := dir.get_next()
    while not entry.is_empty():
        var child := path.path_join(entry)
        if dir.current_is_dir():
            _remove_tree(child)
        else:
            DirAccess.remove_absolute(child)
        entry = dir.get_next()
    dir.list_dir_end()
    DirAccess.remove_absolute(path)


func _expect_equal(actual: String, expected: String, label: String) -> void:
    if actual == expected:
        return
    push_error(
        "game_metadata_test: %s: expected %s, got %s" % [label, expected, actual]
    )
    failures += 1


func _expect_true(actual: bool, label: String) -> void:
    if actual:
        return
    push_error("game_metadata_test: %s: expected true" % label)
    failures += 1
