# Unified Diagnostic Sessions

English | [简体中文](diagnostics.zh-CN.md)

Diagnostic sessions replace manual correlation of Godot output, native logs, platform logs, and temporary performance probes. Debug builds enable the bounded low-overhead `baseline` profile by default. Release builds require `AETHERKIRI_DIAGNOSTICS=1` or an explicit Web diagnostic query.

## One-command reproduction

```bash
python3 tools/diagnose.py run android
python3 tools/diagnose.py run macos --reuse-build
python3 tools/diagnose.py run ios --device "device name or UDID"
python3 tools/diagnose.py run ios-simulator
python3 tools/diagnose.py run web
```

The command builds Debug by default, launches the app, and waits. Reproduce the symptom, tap **标记问题** in the game overlay, then press Enter in the terminal. Use `--duration 30` without an interactive terminal.

An accepted marker changes the button to **已标记 #N**, shows a saved confirmation, vibrates briefly on mobile, and emits a `[diagnostics] issue_marker` console/logcat line. The event stream and matching pre/post incident files are flushed immediately. The button explicitly reports when the eight-marker session limit is reached.

Android validates `adb devices` before starting a build. A missing, unauthorized, or offline device fails immediately; use `--device SERIAL` when more than one ready device is connected.

Results are written to `out/diagnostics/<timestamp>-<platform>-<session>/` and a matching ZIP. A bundle contains build metadata, unified JSONL events, the 10 seconds before and 5 seconds after each marker, platform evidence, and a summary that does not claim causation from timing alone. If the UI is blocked, collection adds a host marker with `ui_marker_missing=true`.

## Profiles

`--profile` accepts `baseline`, `input`, `render`, `storage`, `script`, `audio`, `video`, `plugin`, `system`, and `full`. Baseline records lifecycle events, warnings/errors, one-second frame aggregates, and phase breakdowns above 20 ms. `system` enables bounded platform tracing where available. `full` is deliberately high-overhead and should only be used for short captures.

Use `--reuse-build` for repeated captures. Baseline never flushes per frame: it flushes once per second, on marker, on backgrounding, and on shutdown. The in-memory ring is capped at 2,000 events, event files rotate at 4 MiB, and one session accepts at most eight incident markers.

## Platform evidence

- Android: PID logcat, Warning/Error and crash buffers, meminfo, gfx framestats, app diagnostic files, and optional Perfetto.
- iOS: devicectl/simctl console, app data container, and device crash logs.
- macOS: stdout/stderr, unified logs, and the Godot app-data directory.
- Web: structured events through the existing Vite client-log endpoint; deployed builds download an `.aetherdiag.json` fallback.

Raw paths, game names, and log text are retained by default. Screenshots remain opt-in so capture does not perturb small performance defects. Validation commands are listed in the Chinese document.
