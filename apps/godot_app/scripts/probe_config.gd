extends RefCounted

static func load() -> Dictionary:
    var path := OS.get_environment("AETHERKIRI_TEST_CONFIG")
    if path.is_empty():
        path = OS.get_environment("AETHERKIRI_PROBE_CONFIG")
    if path.is_empty():
        return {}
    var file := FileAccess.open(path, FileAccess.READ)
    if file == null:
        printerr("test config not found: %s" % path)
        return {}
    var parsed = JSON.parse_string(file.get_as_text())
    if parsed is Dictionary:
        return parsed
    printerr("test config is not a JSON object: %s" % path)
    return {}

static func string_value(config: Dictionary, key: String, fallback: String = "") -> String:
    if OS.has_environment(_env_name(key)):
        return OS.get_environment(_env_name(key))
    if config.has(key):
        return String(config[key])
    return fallback

static func bool_value(config: Dictionary, key: String, fallback: bool = false) -> bool:
    if OS.has_environment(_env_name(key)):
        return OS.get_environment(_env_name(key)) == "1"
    if config.has(key):
        return bool(config[key])
    return fallback

static func int_value(config: Dictionary, key: String, fallback: int) -> int:
    if OS.has_environment(_env_name(key)):
        return int(OS.get_environment(_env_name(key)))
    if config.has(key):
        return int(config[key])
    return fallback

static func game_path(config: Dictionary, fallback: String = "") -> String:
    var value := OS.get_environment("AETHERKIRI_SMOKE_GAME")
    if value.is_empty():
        value = string_value(config, "game_path", fallback)
    return value

static func require_game_path(config: Dictionary) -> String:
    var value := game_path(config)
    if value.is_empty():
        printerr("game path is not configured; set AETHERKIRI_SMOKE_GAME or game_path in AETHERKIRI_TEST_CONFIG")
    return value

static func backend(config: Dictionary, env_name: String = "AETHERKIRI_RENDER_BACKEND") -> String:
    var value := OS.get_environment(env_name)
    if value.is_empty():
        value = string_value(config, "backend", "Godot Native")
    return value

static func surface_size(config: Dictionary, fallback: Vector2i = Vector2i(1280, 720)) -> Vector2i:
    return _vector2i(config, "surface_size", fallback)

static func window_size(config: Dictionary, fallback: Vector2i = Vector2i(1280, 720)) -> Vector2i:
    return _vector2i(config, "window_size", fallback)

static func coord_size(config: Dictionary, fallback: Vector2i = Vector2i(1600, 900)) -> Vector2i:
    return _vector2i(config, "coord_size", fallback)

static func clicks(config: Dictionary, env_name: String = "AETHERKIRI_PROBE_CLICKS") -> Array[Dictionary]:
    var spec := OS.get_environment(env_name)
    if not spec.is_empty():
        return _parse_click_spec(spec)
    if not config.has("clicks") or not config["clicks"] is Array:
        return []
    var result: Array[Dictionary] = []
    for item in config["clicks"]:
        if item is Dictionary:
            result.append(item)
        elif item is Array and item.size() >= 2:
            result.append({"x": float(item[0]), "y": float(item[1])})
    return result

static func click_position(click: Dictionary) -> Vector2:
    if click.has("pos") and click["pos"] is Array and click["pos"].size() >= 2:
        return Vector2(float(click["pos"][0]), float(click["pos"][1]))
    return Vector2(float(click.get("x", 0.0)), float(click.get("y", 0.0)))

static func perf_click(config: Dictionary, key: String, fallback: Vector2) -> Vector2:
    if config.has("perf_input") and config["perf_input"] is Dictionary:
        var perf: Dictionary = config["perf_input"]
        if perf.has(key):
            var value = perf[key]
            if value is Array and value.size() >= 2:
                return Vector2(float(value[0]), float(value[1]))
            if value is Dictionary:
                return click_position(value)
    return fallback

static func nested_int(config: Dictionary, section: String, key: String, fallback: int) -> int:
    if config.has(section) and config[section] is Dictionary:
        var dict: Dictionary = config[section]
        if dict.has(key):
            return int(dict[key])
    return fallback

static func _vector2i(config: Dictionary, key: String, fallback: Vector2i) -> Vector2i:
    if not config.has(key):
        return fallback
    var value = config[key]
    if value is Array and value.size() >= 2:
        return Vector2i(int(value[0]), int(value[1]))
    if value is Dictionary:
        return Vector2i(int(value.get("width", fallback.x)), int(value.get("height", fallback.y)))
    return fallback

static func _parse_click_spec(spec: String) -> Array[Dictionary]:
    var result: Array[Dictionary] = []
    for item in spec.split(";"):
        var parts := item.split(",")
        if parts.size() == 2:
            result.append({"x": float(parts[0]), "y": float(parts[1])})
    return result

static func _env_name(key: String) -> String:
    return "AETHERKIRI_" + key.to_upper()
