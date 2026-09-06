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

# --- SetMovie video overlay (docs/V_REPORT.md §5) ---------------------------
# The runtime latches kMovie on SetMovie and reports movie_state/movie_id/
# movie_path through get_renderer_info(); this host drives the FFmpeg channel
# (media_open/media_play/media_update_texture) and stacks a full-screen
# TextureRect ABOVE the composed game frame but BELOW the input bridge.
const MOVIE_STATUS_ENDED := 3   # engine_media_status_t ENDED
const MOVIE_STATUS_ERROR := 4

var movie_rect: TextureRect = null
var movie_playing := false
var movie_failed_token := ""


func _decode_percent(v: String) -> String:
    if not v.contains("%"):
        return v
    var bytes := PackedByteArray()
    var i := 0
    var n := v.length()
    while i < n:
        if v[i] == "%" and i + 2 < n:
            bytes.append(("0x" + v.substr(i + 1, 2)).hex_to_int())
            i += 3
        else:
            bytes.append(v.unicode_at(i))
            i += 1
    return bytes.get_string_from_utf8()


func _renderer_fields(info: String) -> Dictionary:
    var out := {}
    for token in info.split(" ", false):
        var eq := token.find("=")
        if eq > 0:
            out[token.substr(0, eq)] = token.substr(eq + 1)
    return out


func _close_movie() -> void:
    player.media_close()
    movie_rect.visible = false
    movie_rect.texture = null
    movie_playing = false


func _finish_movie() -> void:
    # EOF / error path: drop the player first, then release the runtime's
    # kMovie latch through the provider option channel (idempotent).
    _close_movie()
    player.set_engine_option("wa2.movie.finish", "")


func _poll_movie() -> void:
    var fields := _renderer_fields(player.get_renderer_info())
    var state: String = fields.get("movie_state", "idle")
    if state != "pending":
        if movie_playing:
            # Latch released from inside the runtime (skip click) — stop
            # pulling frames and restore plain rendering.
            print("[wa2gui][movie] latch gone -> close media")
            _close_movie()
        return
    var token: String = fields.get("movie_path", "-")
    var movie_id: String = fields.get("movie_id", "0")
    if not movie_playing:
        if token == "-" or token == movie_failed_token:
            return  # nothing to open (missing file) or already failed once
        var path := _decode_percent(token)
        if not player.media_open(path):
            printerr("[wa2gui][movie] open FAILED path=%s err=%s" % [path, player.get_last_error()])
            movie_failed_token = token
            # Treat like the native missing-file case: finish immediately.
            _finish_movie()
            return
        if player.media_play() != 0:
            printerr("[wa2gui][movie] play FAILED path=%s err=%s" % [path, player.get_last_error()])
            movie_failed_token = token
            _finish_movie()
            return
        movie_playing = true
        movie_rect.visible = true
        print("[wa2gui][movie] playing path=%s id=%s" % [path, movie_id])
        return
    # Playing: pull the latest RGBA frame into the overlay texture.
    var tex: Texture2D = player.media_update_texture()
    if tex != null:
        movie_rect.texture = tex
    var st: Dictionary = player.media_get_state()
    var status := int(st.get("status", 0))
    if status == MOVIE_STATUS_ENDED or status == MOVIE_STATUS_ERROR:
        print("[wa2gui][movie] ended status=%d pos=%.1fs/%.1fs" % [
            status, float(st.get("position", 0.0)), float(st.get("duration", 0.0))])
        _finish_movie()


# --- WA2 audio playback pool (docs/M2_REPORT.md §5) --------------------------
# The runtime translates audio commands into pendingAudio events (absolute
# extracted file paths + channel/volume/fade); this host owns playback via an
# AudioStreamPlayer pool and acknowledges events through
# set_engine_option("wa2.audio.finished", <seq>).
#
# F4 contract (fixes restart-loop / freeze / stutter):
#   - Each event is HANDLED exactly once (audio_handled guard): a pending
#     event must never re-enter _handle_audio_event, otherwise every play
#     would p.play() from sample 0 again each frame (F3 regression: the same
#     seq was restarted 400+ times in /tmp/wa2_gui.log).
#   - Looping streams (BGM, SEP rain) never reach ENDED, so they ack ON
#     CONSUMPTION (runtime header contract: "everything else on consumption");
#     finite streams ack through the finished signal.
#   - Decoded AudioStream resources are cached per path (LRU): decoding a
#     100 s stereo OGG on the main thread every frame starved the mix.
#   - Lost-ack recovery: an ack whose event is still visible in pendingAudio
#     after the grace window is re-sent (bounded retries) — VW/SEW latches
#     must not starve on a dropped set_option.
const AUDIO_ONESHOT_COUNT := 8
const AUDIO_STREAM_CACHE_MAX := 16
const AUDIO_ACK_RESEND_GRACE_FRAMES := 20     # ~0.33 s before presuming loss
const AUDIO_ACK_RESEND_INTERVAL_FRAMES := 30  # ~0.5 s between resends
const AUDIO_ACK_RESEND_MAX_TRIES := 5

var bgm_players: Array = []
var se_players: Array = []
var oneshot_players: Array = []
var voice_player: AudioStreamPlayer = null
var audio_state := {}       # player instance_id -> event state dict
var audio_handled := {}     # seq -> true (exactly-once processing guard)
var audio_ack_sent := {}    # seq -> true (set_option sent at least once)
var audio_resend := {}      # seq -> {tries, due_frame} lost-ack recovery
var audio_stream_cache := {}  # path -> AudioStream, insertion order = LRU
var audio_frame := 0
var oneshot_cursor := 0
var audio_host_announced := false


func _audio_db(volume: int) -> float:
    # 256-fixed volume -> linear -> dB (docs/M2_REPORT.md §3.1 formula).
    var lin: float = clampf(float(volume) / 256.0, 0.0001, 1.0)
    return linear_to_db(lin)


func _player_for_channel(channel: String) -> AudioStreamPlayer:
    if channel.begins_with("bgm"):
        var idx := int(channel.substr(3))
        if idx >= 0 and idx < bgm_players.size():
            return bgm_players[idx]
    elif channel.begins_with("se") and channel != "se_oneshot":
        var idx2 := int(channel.substr(2))
        if idx2 >= 0 and idx2 < se_players.size():
            return se_players[idx2]
    elif channel == "se_oneshot":
        var p: AudioStreamPlayer = oneshot_players[oneshot_cursor]
        oneshot_cursor = (oneshot_cursor + 1) % oneshot_players.size()
        return p
    elif channel.begins_with("voice"):
        return voice_player
    return null


func _players_on_channel(channel: String) -> Array:
    var out: Array = []
    if channel == "bgm":
        out += bgm_players
    elif channel == "se_oneshot":
        out += oneshot_players
    else:
        var p := _player_for_channel(channel)
        if p != null:
            out.append(p)
    return out


func _load_audio_stream(path: String) -> AudioStream:
    # LRU decode cache (F4): the same file used to be re-read + re-decoded on
    # EVERY poll frame while its event sat un-acked in the runtime queue.
    if audio_stream_cache.has(path):
        var cached: AudioStream = audio_stream_cache[path]
        audio_stream_cache.erase(path)
        audio_stream_cache[path] = cached  # touch -> most recently used
        return cached
    var bytes := FileAccess.get_file_as_bytes(path)
    if bytes.is_empty():
        printerr("[wa2gui][audio] cannot read path=%s" % path)
        return null
    var stream: AudioStream = null
    if path.to_lower().ends_with(".ogg"):
        if ClassDB.class_has_method("AudioStreamOggVorbis", "load_from_buffer"):
            stream = AudioStreamOggVorbis.load_from_buffer(bytes)
    else:
        if ClassDB.class_has_method("AudioStreamWAV", "load_from_buffer"):
            stream = AudioStreamWAV.load_from_buffer(bytes)
    if stream == null:
        printerr("[wa2gui][audio] no decoder for path=%s" % path)
        return null
    audio_stream_cache[path] = stream
    while audio_stream_cache.size() > AUDIO_STREAM_CACHE_MAX:
        var oldest: String = audio_stream_cache.keys()[0]
        audio_stream_cache.erase(oldest)
    return stream


func _ack_audio(seq: int) -> void:
    if audio_ack_sent.has(seq):
        return
    _send_audio_ack(seq)


func _send_audio_ack(seq: int) -> void:
    player.set_engine_option("wa2.audio.finished", str(seq))
    audio_ack_sent[seq] = true
    if audio_ack_sent.size() > 4096:
        audio_ack_sent.clear()
    # Arm lost-ack recovery: if this seq is still visible in pendingAudio
    # after the grace window, _prune_and_resend re-sends it.
    audio_resend[seq] = {
        "tries": 0,
        "due": audio_frame + AUDIO_ACK_RESEND_GRACE_FRAMES,
    }


func _on_audio_finished(p: AudioStreamPlayer) -> void:
    var st: Dictionary = audio_state.get(p.get_instance_id(), {})
    if st.is_empty():
        return
    if int(st.get("loop", 0)) != 0:
        p.play()  # host-side loop keeps the runtime queue untouched
        return
    _ack_audio(int(st.get("seq", 0)))
    audio_state.erase(p.get_instance_id())


func _handle_audio_event(ev: Dictionary) -> void:
    var seq := int(ev.get("id", 0))
    # Exactly-once guard (F4): mark BEFORE dispatch. A pending event that is
    # not acked yet (finite stream still playing) must never be re-handled —
    # re-playing it restarts the sample and cancels its own ENDED forever.
    if audio_handled.has(seq):
        return
    audio_handled[seq] = true
    if audio_handled.size() > 4096:
        audio_handled.clear()
    var action := String(ev.get("action", ""))
    var channel := String(ev.get("channel", ""))
    match action:
        "play":
            var p := _player_for_channel(channel)
            if p == null:
                _ack_audio(seq)
                return
            var stream := _load_audio_stream(String(ev.get("path", "")))
            if stream == null:
                # Treat like the native missing-file path: ack immediately so
                # VW-style waits never deadlock.
                _ack_audio(seq)
                return
            for other in _players_on_channel(channel):
                if other != p and audio_state.has(other.get_instance_id()):
                    # Voice preemption / restart semantics: drop stale state.
                    audio_state.erase(other.get_instance_id())
            p.stream = stream
            var vol := int(ev.get("volume", 255))
            var fade := int(ev.get("fade", 0))
            var target_db := _audio_db(vol)
            var start_db := -60.0 if fade > 0 else target_db
            p.volume_db = start_db
            audio_state[p.get_instance_id()] = {
                "seq": seq, "channel": channel,
                "loop": int(ev.get("loop", 0)),
                "from_db": start_db, "to_db": target_db,
                "t": 0, "dur": maxf(1.0, float(fade)),
            }
            p.play()
            print("[wa2gui][audio] play seq=%d ch=%s vol=%d fade=%d loop=%d path=%s" % [
                seq, channel, vol, fade, int(ev.get("loop", 0)), ev.get("path", "")])
            if int(ev.get("loop", 0)) != 0:
                # Looping streams never reach ENDED: ack on consumption so the
                # runtime queue drains (F4 — this event used to sit pending
                # forever and be restarted every frame).
                _ack_audio(seq)
            # Finite streams ack through the finished signal (see above).
        "stop":
            var fade_stop := int(ev.get("fade", 0))
            for p2 in _players_on_channel(channel):
                var sid: int = p2.get_instance_id()
                var st: Dictionary = audio_state.get(sid, {})
                if not st.is_empty():
                    st["to_db"] = -60.0
                    st["from_db"] = p2.volume_db
                    st["t"] = 0
                    st["dur"] = maxf(1.0, float(fade_stop))
                    st["stopping"] = true
                    st["fade_frames_left_stop"] = fade_stop
                    audio_state[sid] = st
                elif fade_stop <= 0:
                    p2.stop()
            _ack_audio(seq)
            print("[wa2gui][audio] stop seq=%d ch=%s fade=%d" % [seq, channel, fade_stop])
        "volume":
            var vol3 := int(ev.get("volume", 255))
            var fade3 := int(ev.get("fade", 0))
            for p3 in _players_on_channel(channel):
                var sid3: int = p3.get_instance_id()
                var st3: Dictionary = audio_state.get(sid3, {})
                if st3.is_empty():
                    continue
                st3["from_db"] = p3.volume_db
                st3["to_db"] = _audio_db(vol3)
                st3["t"] = 0
                st3["dur"] = maxf(1.0, float(fade3))
                audio_state[sid3] = st3
            _ack_audio(seq)
            print("[wa2gui][audio] volume seq=%d ch=%s vol=%d fade=%d" % [seq, channel, vol3, fade3])
        _:
            _ack_audio(seq)


func _update_audio_fades() -> void:
    var finished_stops: Array = []
    for key in audio_state.keys():
        var st: Dictionary = audio_state[key]
        var dur: float = maxf(1.0, float(st.get("dur", 1.0)))
        var t: float = float(int(st.get("t", 0))) + 1.0
        var from_db: float = float(st.get("from_db", 0.0))
        var to_db: float = float(st.get("to_db", 0.0))
        var node := instance_from_id(key) as AudioStreamPlayer
        if node == null:
            audio_state.erase(key)
            continue
        node.volume_db = lerpf(from_db, to_db, minf(t / dur, 1.0))
        st["t"] = int(t)
        if st.has("stopping") and t >= dur:
            node.stop()
            finished_stops.append(key)
        audio_state[key] = st
    for key2 in finished_stops:
        audio_state.erase(key2)


func _prune_and_resend(pending_seqs: Dictionary) -> void:
    # Lost-ack recovery (F4): an ack we already sent whose event is STILL
    # visible in pendingAudio after the grace window is presumed dropped by
    # the transport — re-send it with bounded retries so runtime VW/SEW-style
    # latches and the EndMessage voice hold cannot starve. Events that left
    # the queue were delivered -> prune.
    var done: Array = []
    for seq in audio_resend.keys():
        if not pending_seqs.has(seq):
            done.append(seq)
            continue
        var entry: Dictionary = audio_resend[seq]
        if audio_frame < int(entry.get("due", 0)):
            continue
        entry["tries"] = int(entry.get("tries", 0)) + 1
        if int(entry["tries"]) > AUDIO_ACK_RESEND_MAX_TRIES:
            done.append(seq)
            print("[wa2gui][audio] ack resend GIVE UP seq=%d after %d tries" % [
                seq, AUDIO_ACK_RESEND_MAX_TRIES])
            continue
        entry["due"] = audio_frame + AUDIO_ACK_RESEND_INTERVAL_FRAMES
        audio_resend[seq] = entry
        player.set_engine_option("wa2.audio.finished", str(seq))
        print("[wa2gui][audio] ack RESEND seq=%d try=%d" % [seq, int(entry["tries"])])
    for seq2 in done:
        audio_resend.erase(seq2)


func _poll_audio() -> void:
    audio_frame += 1
    if not audio_host_announced:
        audio_host_announced = true
        # Announce a live output path: enables the message-engine implicit
        # voice hold (text never runs ahead of speech, docs/M2_REPORT.md §3.5).
        player.set_engine_option("wa2.audio.host", "1")
    var info: String = player.get_plugin_debug_info()
    var events: Array = []
    if not info.is_empty():
        var parsed = JSON.parse_string(info)
        if parsed != null and (parsed is Dictionary):
            events = parsed.get("pendingAudio", [])
    var pending_seqs := {}
    for ev in events:
        if ev is Dictionary:
            var seq := int(ev.get("id", 0))
            pending_seqs[seq] = true
            if not audio_handled.has(seq):
                _handle_audio_event(ev)
    _prune_and_resend(pending_seqs)
    _update_audio_fades()


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

    # Movie overlay: above the composed game frame, below the input bridge
    # (mouse_filter IGNORE keeps clicks flowing to the runtime, where a press
    # maps to the movie skip/finish path).
    movie_rect = TextureRect.new()
    movie_rect.set_anchors_preset(Control.PRESET_FULL_RECT)
    movie_rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    movie_rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    movie_rect.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
    movie_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
    movie_rect.visible = false
    root.add_child(movie_rect)

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

    # Audio pool (docs/M2_REPORT.md §5): BGM crossfade pair, 4 SE stream
    # channels, 8 rotating one-shots, single preemptive voice channel.
    for i in range(2):
        var bp := AudioStreamPlayer.new()
        root.add_child(bp)
        bgm_players.append(bp)
    for i in range(4):
        var sp := AudioStreamPlayer.new()
        root.add_child(sp)
        se_players.append(sp)
    for i in range(AUDIO_ONESHOT_COUNT):
        var op := AudioStreamPlayer.new()
        root.add_child(op)
        oneshot_players.append(op)
    voice_player = AudioStreamPlayer.new()
    root.add_child(voice_player)
    for p in bgm_players + se_players + oneshot_players + [voice_player]:
        p.finished.connect(_on_audio_finished.bind(p))

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
        _poll_movie()
        _poll_audio()
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
