extends SceneTree

# Headful WA2 GUI runner: opens a real window, renders the game every frame,
# and forwards REAL mouse events into the runtime. No auto-quit.

const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3

const SURFACE_W := 1280
const SURFACE_H := 720

var player = null
var rect: TextureRect = null


class InputBridge extends Control:
    var player = null
    var rect: TextureRect = null

    func _surface_point(screen_pos: Vector2) -> Vector2:
        var win_size := rect.get_viewport_rect().size
        var scale_f: float = minf(win_size.x / 1280.0, win_size.y / 720.0)
        var offset := Vector2((win_size.x - 1280.0 * scale_f) * 0.5,
                              (win_size.y - 720.0 * scale_f) * 0.5)
        return (screen_pos - offset) / maxf(scale_f, 0.0001)

    func _unhandled_input(event: InputEvent) -> void:
        if player == null:
            return
        if event is InputEventMouseMotion:
            var p := _surface_point(event.position)
            player.send_pointer_event(POINTER_MOVE, 0, p.x, p.y, 0.0, 0.0, 0)
        elif event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
            var p2 := _surface_point(event.position)
            var kind := POINTER_DOWN if event.pressed else POINTER_UP
            print("[wa2gui] click %s surface=(%.0f,%.0f)" % ["down" if event.pressed else "up", p2.x, p2.y])
            player.send_pointer_event(kind, 0, p2.x, p2.y, 0.0, 0.0, 0)


var bridge: InputBridge = null


func _initialize() -> void:
    var ProbeConfig = preload("res://scripts/probe_config.gd")
    var test_config: Dictionary = ProbeConfig.load()

    root.size = Vector2i(SURFACE_W, SURFACE_H)

    rect = TextureRect.new()
    rect.set_anchors_preset(Control.PRESET_FULL_RECT)
    rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    rect.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
    root.add_child(rect)

    player = ClassDB.instantiate("AetherRuntimePlayer")
    root.add_child(player as Node)

    var user_dir := OS.get_environment("AETHERKIRI_PROBE_USER_DIR").strip_edges()
    if user_dir.is_empty():
        user_dir = OS.get_user_data_dir()
    else:
        DirAccess.make_dir_recursive_absolute(user_dir)
    var cache_dir := user_dir.path_join("cache")
    DirAccess.make_dir_recursive_absolute(cache_dir)
    if not player.initialize_engine(user_dir, cache_dir):
        printerr("initialize_engine failed: %s" % player.get_last_error())
        quit(1)
        return

    player.set_render_backend(ProbeConfig.backend(test_config, ""))
    player.set_surface_size(SURFACE_W, SURFACE_H)

    var game_path: String = ProbeConfig.require_game_path(test_config)
    if game_path.is_empty():
        quit(2)
        return
    var result: int = player.open_game(game_path, true)
    if result != 0:
        printerr("open_game failed: %s" % player.get_last_error())
        quit(1)
        return
    print("[wa2gui] open_game ok path=%s" % game_path)

    bridge = InputBridge.new()
    bridge.player = player
    bridge.rect = rect
    bridge.set_anchors_preset(Control.PRESET_FULL_RECT)
    bridge.mouse_filter = Control.MOUSE_FILTER_PASS
    root.add_child(bridge)

    _warmup()


func _warmup() -> void:
    for i in range(30):
        player.tick(1.0 / 60.0)
        var tex: Texture2D = player.update_frame_texture()
        if tex != null:
            rect.texture = tex
        await process_frame
    print("[wa2gui] ready renderer=\"%s\"" % player.get_renderer_info())
    _run_loop()


func _run_loop() -> void:
    var acc_tick := 0.0
    var acc_tex := 0.0
    var max_tick := 0.0
    var max_tex := 0.0
    var frames := 0
    var last_us := Time.get_ticks_usec()
    var acc_frame := 0.0
    while true:
        var now_us := Time.get_ticks_usec()
        acc_frame += float(now_us - last_us)
        last_us = now_us
        var t0 := Time.get_ticks_usec()
        player.tick(1.0 / 60.0)
        var t1 := Time.get_ticks_usec()
        var tex: Texture2D = player.update_frame_texture()
        if tex != null:
            rect.texture = tex
        var t2 := Time.get_ticks_usec()
        acc_tick += float(t1 - t0)
        acc_tex += float(t2 - t1)
        max_tick = maxf(max_tick, float(t1 - t0) * 0.001)
        max_tex = maxf(max_tex, float(t2 - t1) * 0.001)
        frames += 1
        if frames % 120 == 0:
            print("[wa2gui][perf] fps=%.1f frame_avg=%.1fms tick_avg=%.1fms tick_max=%.1fms tex_avg=%.1fms tex_max=%.1fms" % [
                1000.0 * 120.0 / acc_frame, acc_frame / 120.0,
                acc_tick / 120.0, max_tick, acc_tex / 120.0, max_tex])
            print("[wa2gui][rt] %s" % player.get_renderer_info())
            acc_frame = 0.0
            acc_tick = 0.0
            acc_tex = 0.0
            max_tick = 0.0
            max_tex = 0.0
        await process_frame
