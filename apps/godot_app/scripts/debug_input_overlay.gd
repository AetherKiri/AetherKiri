extends Control

var points: Dictionary = {}
var last_position := Vector2(-1, -1)
var last_label := ""

func _ready() -> void:
    mouse_filter = Control.MOUSE_FILTER_IGNORE
    set_anchors_preset(Control.PRESET_FULL_RECT)
    visible = false

func update_state(next_points: Dictionary, next_position: Vector2, next_label: String) -> void:
    points = next_points.duplicate(true)
    last_position = next_position
    last_label = next_label
    queue_redraw()

func _draw() -> void:
    for key in points.keys():
        var position = points[key]
        if not position is Vector2:
            continue
        var point := position as Vector2
        draw_circle(point, 22.0, Color(0.22, 0.80, 1.0, 0.24))
        draw_arc(point, 22.0, 0.0, TAU, 32, Color(0.22, 0.80, 1.0, 0.95), 3.0)
        draw_line(point - Vector2(30, 0), point + Vector2(30, 0), Color(0.22, 0.80, 1.0, 0.75), 2.0)
        draw_line(point - Vector2(0, 30), point + Vector2(0, 30), Color(0.22, 0.80, 1.0, 0.75), 2.0)
        draw_string(ThemeDB.fallback_font, point + Vector2(28, -18), "#%s" % String(key), HORIZONTAL_ALIGNMENT_LEFT, -1, 18, Color.WHITE)
    if last_position.x >= 0.0 and last_position.y >= 0.0:
        draw_arc(last_position, 12.0, 0.0, TAU, 24, Color(1.0, 0.72, 0.22, 0.95), 2.0)
        if not last_label.is_empty():
            draw_string(ThemeDB.fallback_font, last_position + Vector2(18, 28), last_label, HORIZONTAL_ALIGNMENT_LEFT, 420, 16, Color(1.0, 0.86, 0.58))
