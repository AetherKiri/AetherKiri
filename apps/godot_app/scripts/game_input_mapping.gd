extends RefCounted

# The frame texture defines the engine's pointer coordinate space. A runtime
# may accept a requested surface size before open and then replace it with the
# game's native size while booting, so the requested size must not be used to
# rescale input after a frame has been presented.
static func map_point(
    window_point: Vector2,
    target_rect: Rect2,
    texture_size: Vector2,
    clamp_to_bounds: bool = false
) -> Vector2:
    if texture_size.x <= 0.0 or texture_size.y <= 0.0:
        return Vector2(-1.0, -1.0)
    var scale := minf(
        target_rect.size.x / texture_size.x,
        target_rect.size.y / texture_size.y
    )
    if scale <= 0.0:
        return Vector2(-1.0, -1.0)
    var drawn_size := texture_size * scale
    var inside := window_point - target_rect.position - (
        target_rect.size - drawn_size
    ) * 0.5
    if (
        inside.x < 0.0
        or inside.y < 0.0
        or inside.x > drawn_size.x
        or inside.y > drawn_size.y
    ):
        if not clamp_to_bounds:
            return Vector2(-1.0, -1.0)
        inside = Vector2(
            clampf(inside.x, 0.0, drawn_size.x),
            clampf(inside.y, 0.0, drawn_size.y)
        )
    return inside / scale

static func map_delta(
    window_delta: Vector2,
    target_size: Vector2,
    texture_size: Vector2
) -> Vector2:
    if texture_size.x <= 0.0 or texture_size.y <= 0.0:
        return window_delta
    var scale := minf(
        target_size.x / texture_size.x,
        target_size.y / texture_size.y
    )
    return window_delta / maxf(0.0001, scale)


# iOS reports sub-pixel finger drift even for an intentional tap. Once the
# gesture has stayed below the drag threshold, keep its press and release at
# the original contact point so a hover-driven runtime cannot reinterpret the
# tap as cursor motion.
static func stable_tap_point(
    down_point: Vector2,
    up_point: Vector2,
    drag_threshold: float
) -> Vector2:
    if up_point.distance_to(down_point) < maxf(0.0, drag_threshold):
        return down_point
    return up_point
