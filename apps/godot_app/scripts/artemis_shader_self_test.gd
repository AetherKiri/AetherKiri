extends SceneTree

func _initialize() -> void:
    var player = ClassDB.instantiate("AetherRuntimePlayer")
    root.add_child(player as Node)
    var result: Dictionary = player.debug_artemis_shader_self_test()
    if not bool(result.get("ok", false)):
        printerr("Artemis shader self-test failed: %s" % [result])
        quit(1)
        return
    print("Artemis shader self-test ok: %s" % [result])
    quit(0)
