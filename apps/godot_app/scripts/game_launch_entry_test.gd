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
