extends SceneTree

const GameLaunchEntry = preload("res://scripts/game_launch_entry.gd")

var failures := 0

func _init() -> void:
    var root := "/Users/test/游戏/NOBLE☆WORKS"
    var exe_path := root.path_join("开始游戏.exe")
    var archive_path := root.path_join("patch/data.xp3")

    _expect_equal(GameLaunchEntry.resolve({"path": root}), root, "default directory entry")
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
