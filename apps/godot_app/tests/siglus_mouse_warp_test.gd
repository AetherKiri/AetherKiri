extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    # Do not add Main to the tree: its startup must not open a game or move
    # the real desktop pointer. Only exercise the mapping and focus policy.
    var app = MAIN_SCRIPT.new()
    var surface := TextureRect.new()
    surface.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    surface.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    var texture := PlaceholderTexture2D.new()
    texture.size = Vector2(1920.0, 1080.0)
    surface.texture = texture
    root.add_child(surface)
    surface.position = Vector2(40.0, 30.0)
    surface.size = Vector2(800.0, 600.0)
    app.viewport = surface
    app.active_runtime_kind = app.RUNTIME_SIGLUS
    app.current_surface_size = Vector2i(1920, 1080)
    app.last_texture_size = Vector2i(1920, 1080)
    app.cached_startup_state = app.STARTUP_SUCCEEDED
    app.game_running = true
    app.siglus_pointer_inside_window = true

    # Aspect-fit content is 800 x 450 with 75 px top/bottom letterboxing.
    _assert_point(app._map_surface_point_to_viewport(Vector2.ZERO), Vector2(40.0, 105.0))
    _assert_point(app._map_surface_point_to_viewport(Vector2(960.0, 540.0)), Vector2(440.0, 330.0))
    _assert_point(app._map_surface_point_to_viewport(Vector2(1920.0, 1080.0)), Vector2(840.0, 555.0))

    # A 2x window stretch must not enter the coordinate passed to warp_mouse.
    # Use a SubViewport final transform to model HiDPI without resizing a
    # native window or relying on a particular display's physical scale.
    var scaled_viewport := SubViewport.new()
    scaled_viewport.size = Vector2i(1600, 1200)
    scaled_viewport.size_2d_override = Vector2i(800, 600)
    scaled_viewport.size_2d_override_stretch = true
    root.add_child(scaled_viewport)
    surface.reparent(scaled_viewport, false)
    _assert_point(app._map_surface_point_to_viewport(Vector2(960.0, 540.0)), Vector2(440.0, 330.0))
    var stretched := scaled_viewport.get_final_transform() * app._map_surface_point_to_viewport(Vector2(960.0, 540.0))
    _assert_point(stretched, Vector2(880.0, 660.0))

    assert(app._can_apply_siglus_mouse_warp(true))
    app.siglus_native_cursor_visible = false
    assert(app._siglus_cursor_mouse_mode(true) == Input.MOUSE_MODE_HIDDEN)
    assert(app._siglus_cursor_mouse_mode(false) == Input.MOUSE_MODE_VISIBLE)
    app.siglus_pointer_inside_window = false
    assert(not app._can_apply_siglus_mouse_warp(true))
    assert(app._siglus_cursor_mouse_mode(true) == Input.MOUSE_MODE_VISIBLE)
    app.siglus_pointer_inside_window = true
    assert(not app._can_apply_siglus_mouse_warp(false))
    app.app_lifecycle_paused = true
    assert(not app._can_apply_siglus_mouse_warp(true))
    assert(app._siglus_cursor_mouse_mode(true) == Input.MOUSE_MODE_VISIBLE)
    app.app_lifecycle_paused = false
    app.modal_layer = Control.new()
    assert(not app._can_apply_siglus_mouse_warp(true))
    assert(app._siglus_cursor_mouse_mode(true) == Input.MOUSE_MODE_VISIBLE)
    app.modal_layer.hide()
    assert(app._can_apply_siglus_mouse_warp(true))
    app.loading_panel = PanelContainer.new()
    assert(not app._can_apply_siglus_mouse_warp(true))
    app.loading_panel.hide()
    app.game_running = false
    assert(not app._can_apply_siglus_mouse_warp(true))
    app.game_running = true
    app.active_runtime_kind = app.RUNTIME_KIRIKIRI
    assert(not app._can_apply_siglus_mouse_warp(true))

    app.modal_layer.free()
    app.loading_panel.free()
    scaled_viewport.free()
    app.viewport = null
    app.active_runtime_kind = app.RUNTIME_SIGLUS
    assert(not app._can_apply_siglus_mouse_warp(true))
    app.ui_motion.free()
    app.free()
    print("siglus_mouse_warp_test: PASS")
    quit(0)

func _assert_point(actual: Vector2, expected: Vector2) -> void:
    assert(actual.is_equal_approx(expected), "Expected %s, got %s" % [expected, actual])
