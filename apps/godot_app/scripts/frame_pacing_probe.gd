extends SceneTree

const DEFAULT_FRAMES := 600

func _initialize() -> void:
    var frames := maxi(1, _env_int("AETHERKIRI_PROBE_MEASURE_FRAMES", DEFAULT_FRAMES))
    var frame_times: Array[float] = []
    var started := Time.get_ticks_usec()
    for i in range(frames):
        var frame_started := Time.get_ticks_usec()
        await process_frame
        frame_times.append(float(Time.get_ticks_usec() - frame_started) / 1000.0)

    var elapsed_seconds := maxf(
        0.000001,
        float(Time.get_ticks_usec() - started) / 1000000.0
    )
    frame_times.sort()
    var worst_count := maxi(1, int(ceil(float(frame_times.size()) * 0.01)))
    var worst_total_ms := 0.0
    for index in range(frame_times.size() - worst_count, frame_times.size()):
        worst_total_ms += frame_times[index]

    print("frame pacing probe fps=%.2f one_percent_low_fps=%.2f p50_ms=%.2f p95_ms=%.2f p99_ms=%.2f max_ms=%.2f over_16_67=%d over_20=%d over_33_33=%d" % [
        float(frame_times.size()) / elapsed_seconds,
        1000.0 / maxf(0.000001, worst_total_ms / float(worst_count)),
        _percentile(frame_times, 0.50),
        _percentile(frame_times, 0.95),
        _percentile(frame_times, 0.99),
        frame_times.back(),
        _count_over(frame_times, 16.67),
        _count_over(frame_times, 20.0),
        _count_over(frame_times, 33.33),
    ])
    quit(0)

func _percentile(sorted_values: Array[float], fraction: float) -> float:
    var index := int(ceil(clampf(fraction, 0.0, 1.0) * sorted_values.size())) - 1
    return sorted_values[clampi(index, 0, sorted_values.size() - 1)]

func _count_over(sorted_values: Array[float], threshold: float) -> int:
    var count := 0
    for value in sorted_values:
        if value > threshold:
            count += 1
    return count

func _env_int(name: String, fallback: int) -> int:
    var value := OS.get_environment(name)
    return fallback if value.is_empty() else int(value)
