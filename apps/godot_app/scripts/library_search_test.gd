extends SceneTree

func _init() -> void:
    call_deferred("_run")

func _run() -> void:
    var packed_scene := load("res://scenes/main.tscn") as PackedScene
    var scene: Node = packed_scene.instantiate()
    root.add_child(scene)
    for _frame in range(20):
        await process_frame

    var modal := scene.get("modal_layer") as Control
    if modal != null:
        modal.visible = false
    var test_games: Array[Dictionary] = [
        {
            "title": "Alpha Story",
            "name": "alpha",
            "path": "/private/var/mobile/Containers/Data/Application/common/Documents/Games/alpha",
        },
        {
            "title": "Distant Route",
            "name": "distant",
            "path": "/private/var/mobile/Containers/Data/Application/common/Documents/Games/beta",
        },
    ]
    scene.set("known_games", test_games)
    var games: Array = scene.get("known_games")
    if bool(scene.call("_game_matches_home_search", games[0], "d")):
        _fail("common iOS Documents path leaked into game search")
        return
    if not bool(scene.call("_game_matches_home_search", games[1], "d")):
        _fail("visible game title was not included in search")
        return

    scene.call("_on_home_search_text_changed", "d")
    var filtered_count := int(scene.get("home_filtered_game_count"))
    if filtered_count != 1:
        _fail("library cards were not filtered by visible metadata (count=%d queries=%s)" % [
            filtered_count,
            str(scene.get("home_search_queries")),
        ])
        return

    print("library_search_test: PASS")
    scene.queue_free()
    await process_frame
    quit(0)

func _fail(message: String) -> void:
    push_error("library_search_test: %s" % message)
    quit(1)
