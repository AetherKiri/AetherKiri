extends SceneTree

func _initialize() -> void:
    var player = ClassDB.instantiate("AetherKiriPlayer")
    root.add_child(player as Node)

    var modes := ["AlphaBlend", "AlphaBlend_d", "AlphaBlend_a", "CopyColor", "FillARGB", "RemoveConstOpacity", "ConstAlphaBlend_d", "PsScreenBlend", "PsMulBlend", "PsAddBlend", "PsSubBlend"]
    var failed := false
    for mode in modes:
        var opacities := [255, 192, 96]
        if mode in ["PsMulBlend", "PsAddBlend", "PsSubBlend"]:
            opacities.append_array([128, 0])
        for opacity in opacities:
            var result: Dictionary = player.debug_gpu_blend_self_test(mode, opacity)
            print("gpu blend self-test %s opacity=%d result=%s" % [
                mode,
                opacity,
                JSON.stringify(result),
            ])
            if not bool(result.get("ok", false)):
                failed = true

    var modes2 := [
        "ConstAlphaBlend_SD",
        "ConstAlphaBlend_SD_d",
        "AlphaBlend_d_Mask_Multiply",
        "AlphaBlend_d_Mask_Threshold",
    ]
    for mode in modes2:
        for opacity in [255, 192, 96]:
            var result: Dictionary = player.debug_gpu_blend2_self_test(mode, opacity)
            print("gpu blend2 self-test %s opacity=%d result=%s" % [
                mode,
                opacity,
                JSON.stringify(result),
            ])
            if not bool(result.get("ok", false)):
                failed = true

    player.queue_free()
    quit(1 if failed else 0)
