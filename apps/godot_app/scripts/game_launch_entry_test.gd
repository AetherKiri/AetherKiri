extends SceneTree

const GameLaunchEntry = preload("res://scripts/game_launch_entry.gd")

var failures := 0

func _init() -> void:
    var root := "/Users/test/游戏/NOBLE☆WORKS"
    var exe_path := root.path_join("开始游戏.exe")
    var archive_path := root.path_join("patch/data.xp3")

    _expect_equal(GameLaunchEntry.rfvp_encoding({}), "sjis", "Japanese default")
    var chinese := {GameLaunchEntry.RFVP_ENCODING_FIELD: "gbk"}
    _expect_equal(GameLaunchEntry.rfvp_encoding(chinese), "gbk", "per-game GBK")
    var reloaded: Dictionary = JSON.parse_string(JSON.stringify(chinese))
    _expect_equal(GameLaunchEntry.rfvp_encoding(reloaded), "gbk", "persisted encoding")
    _expect_equal(GameLaunchEntry.rfvp_encoding({}), "sjis", "no cross-game leakage")
    _expect_equal(GameLaunchEntry.rfvp_encoding(chinese, " UTF8 "), "utf8", "environment override")
    _expect_equal(GameLaunchEntry.rfvp_encoding({GameLaunchEntry.RFVP_ENCODING_FIELD: "bad"}), "sjis", "invalid saved value")
    var configured := {GameLaunchEntry.FIELD: "Selected.hcb"}
    var detected := {GameLaunchEntry.FIELD: "Other.hcb"}
    GameLaunchEntry.backfill(configured, detected)
    _expect_equal(configured[GameLaunchEntry.FIELD], "Selected.hcb", "metadata preserves selection")
    configured[GameLaunchEntry.FIELD] = ""
    GameLaunchEntry.backfill(configured, detected)
    _expect_equal(configured[GameLaunchEntry.FIELD], "", "metadata preserves automatic choice")
    configured.clear()
    GameLaunchEntry.backfill(configured, detected)
    _expect_equal(configured[GameLaunchEntry.FIELD], "Other.hcb", "legacy metadata backfill")

    _expect_equal(GameLaunchEntry.resolve({"path": root}), root, "default directory entry")
    _expect_equal(
        GameLaunchEntry.resolve({"path": root, GameLaunchEntry.FIELD: "Script.HCB"}),
        root.path_join("Script.HCB"),
        "FVP script launch path"
    )
    _expect_equal(
        GameLaunchEntry.resolve_for_runtime(
            {"path": root, GameLaunchEntry.FIELD: "Script.HCB"},
            "rfvp"
        ),
        root.path_join("Script.HCB"),
        "RFVP configured script launch path"
    )
    _expect_equal(
        GameLaunchEntry.relative_path_for_selection(root, exe_path),
        "开始游戏.exe",
        "EXE selection"
    )
    _expect_equal(
        GameLaunchEntry.resolve({"path": root, GameLaunchEntry.FIELD: "开始游戏.exe"}),
        exe_path,
        "EXE launch path"
    )
    _expect_equal(
        GameLaunchEntry.resolve_for_runtime(
            {"path": root, GameLaunchEntry.FIELD: "开始游戏.exe"},
            "artemis"
        ),
        root,
        "Artemis directory launch path"
    )
    _expect_equal(
        GameLaunchEntry.resolve_for_runtime(
            {"path": root, GameLaunchEntry.FIELD: "开始游戏.exe"},
            "kirikiri"
        ),
        exe_path,
        "KiriKiri configured launch path"
    )
    _expect_equal(
        GameLaunchEntry.resolve_for_runtime(
            {"path": root, GameLaunchEntry.FIELD: "开始游戏.exe"},
            "kirikiri",
            true
        ),
        root,
        "provider runtime keeps directory root"
    )
    _expect_equal(
        GameLaunchEntry.relative_path_for_selection(root, archive_path),
        "patch/data.xp3",
        "nested XP3 selection"
    )
    _expect_equal(
        GameLaunchEntry.resolve({"path": root, GameLaunchEntry.FIELD: "patch/data.xp3"}),
        archive_path,
        "nested XP3 launch path"
    )
    _expect_equal(
        GameLaunchEntry.relative_path_for_selection(root, "/Users/test/other/game.exe"),
        "",
        "outside selection"
    )
    _expect_equal(
        GameLaunchEntry.relative_path_for_selection(root, root.path_join("readme.txt")),
        "",
        "unsupported selection"
    )
    _expect_equal(
        GameLaunchEntry.configured_relative_path({
            "path": root,
            GameLaunchEntry.FIELD: "../other/game.exe",
        }),
        "",
        "parent traversal"
    )
    _expect_equal(
        GameLaunchEntry.configured_relative_path({
            "path": root,
            GameLaunchEntry.FIELD: "C:\\Games\\other.exe",
        }),
        "",
        "absolute Windows path"
    )
    _expect_equal(
        GameLaunchEntry.configured_relative_path({
            "path": root,
            GameLaunchEntry.FIELD: "readme.txt",
        }),
        "",
        "unsupported configured entry"
    )
    _expect_equal(
        GameLaunchEntry.relative_path_for_selection(
            "C:\\Games\\Noble Works",
            "C:\\Games\\Noble Works\\game.exe"
        ),
        "game.exe",
        "Windows separators"
    )
    var spaced_root := "/Users/test/游戏/Visual Novel+ "
    _expect_equal(
        GameLaunchEntry.resolve({"path": spaced_root}),
        spaced_root,
        "trailing-space directory"
    )
    _expect_equal(
        GameLaunchEntry.resolve({
            "path": spaced_root,
            GameLaunchEntry.FIELD: "cs2.exe",
        }),
        spaced_root.path_join("cs2.exe"),
        "launch file inside trailing-space directory"
    )
    _expect_equal(
        GameLaunchEntry.resolve_for_runtime(
            {"path": root, GameLaunchEntry.FIELD: "launcher.exe"}, "renpy"
        ), root, "Ren'Py ignores legacy launcher selection"
    )
    var renpy_root := ProjectSettings.globalize_path("user://renpy_launch_%d" % Time.get_ticks_usec())
    DirAccess.make_dir_recursive_absolute(renpy_root.path_join("game"))
    var renpy_script := renpy_root.path_join("game/script.rpy")
    var script_file := FileAccess.open(renpy_script, FileAccess.WRITE)
    script_file.store_string("label start:\n    return\n")
    script_file.close()
    var renpy_launcher := renpy_root.path_join("launcher.exe")
    var launcher_file := FileAccess.open(renpy_launcher, FileAccess.WRITE)
    launcher_file.store_string("launcher")
    launcher_file.close()
    for selected in [renpy_root, renpy_root + "/", renpy_root.path_join("game"), renpy_script, renpy_launcher]:
        _expect_equal(GameLaunchEntry.resolve_for_runtime({"path": selected}, "renpy"), renpy_root, "Ren'Py normalized launch root")
    DirAccess.remove_absolute(renpy_script)
    DirAccess.remove_absolute(renpy_launcher)
    DirAccess.remove_absolute(renpy_root.path_join("game"))
    DirAccess.remove_absolute(renpy_root)
    if failures == 0:
        print("game_launch_entry_test: PASS")
        quit(0)
    else:
        quit(1)


func _expect_equal(actual: String, expected: String, label: String) -> void:
    if actual == expected:
        return
    push_error(
        "game_launch_entry_test: %s: expected %s, got %s" % [label, expected, actual]
    )
    failures += 1
