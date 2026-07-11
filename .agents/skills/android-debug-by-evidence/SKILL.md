---
name: android-debug-by-evidence
description: Diagnose Android-only performance stalls, rendering defects, missing UI, native-engine bugs, JNI failures, and ANR-like behavior through narrow instrumentation and user-operated reproduction. Use when an Android app behaves differently from Apple/desktop builds, when adb/logcat or an in-app log viewer is needed, or when a fix must be derived from Warning/Error evidence instead of speculation.
---

# Android Debug by Evidence

Use short diagnostic cycles: preserve a checkpoint, state one falsifiable hypothesis, add bounded logs, build only Android, let the user reproduce, read only relevant Warning/Error evidence, then keep, revise, or reject the hypothesis.

## Establish the baseline

1. Read the issue and local code before editing. Use `gh` when the issue is on GitHub.
2. Inspect `git status` and preserve unrelated user changes. If requested, commit the known-good improvement before adding new probes.
3. Record the exact reproduction path and visible symptom. Do not substitute a nearby screen or automate taps when the user offered to operate the UI.
4. Separate facts from hypotheses. Never infer a long freeze merely because the last log timestamp is old.
5. Build Android only. Do not build macOS/iOS to investigate an Android-only defect unless comparison was explicitly requested.

For this repository, the usual debug build is:

```sh
./build.sh android debug --abi=arm64-v8a
```

Discover the APK and package name from project configuration rather than guessing. In this repository they are normally:

```text
out/godot/android/debug/AetherKiri-debug.apk
org.github.krkr2.aetherkiri
```

## Design precise probes

Instrument the narrowest boundary that can disprove the current hypothesis. Prefer structured, searchable records:

```text
Warning: text_batch_drop reason=bitmap_destroy pending=12 size=640x80
Error: gpu_op_failed type=blend size=180x32 opacity=255 mode=4
```

Follow these rules:

- Emit Warning only for a meaningful anomaly or a deliberately bounded diagnostic summary; emit Error for failed operations or violated invariants.
- Use a stable subsystem prefix and `key=value` fields.
- Include phase, duration, count, dimensions, result, and reason when relevant.
- Log both sides of asynchronous boundaries. “Queued successfully” is not “executed successfully.”
- Add failure reporting where deferred work actually completes, such as render/GPU threads.
- Instrument ownership transitions: enqueue, flush, copy, replacement, destruction, cache eviction, JNI attach, and fallback.
- Rate-limit or aggregate hot-path probes. Never add unbounded per-frame, per-pixel, or per-glyph logs.
- Avoid broad Info/Debug traces. The final capture should be useful with Warning/Error filtering alone.
- Preserve timing enough to reproduce the bug. Note that synchronous readback, forced flushing, or excessive logging can mask races and stalls.

For missing text or UI, distinguish at least these stages:

1. text/glyph creation;
2. deferred draw enqueue;
3. draw flush and scratch-texture creation;
4. GPU upload/blend execution, including asynchronous failure;
5. bitmap/state copy, replacement, eviction, or destruction;
6. presentation.

For stalls, measure nested phase duration and self-time. Start at the input/event boundary, then narrow toward script calls, resource loading, decode, upload, cache policy, and presentation. Do not flood every function with entry/exit logs.

## Install and prepare one reproduction

Build first, then install the generated Android APK:

```sh
adb install -r /absolute/path/to/app-debug.apk
```

Immediately before the reproduction, clear old logs and launch the correct package:

```sh
adb logcat -c
adb shell monkey -p PACKAGE_NAME -c android.intent.category.LAUNCHER 1
```

Tell the user exactly what action to perform and ask them to reply when the symptom has appeared. Do not read an arbitrary long-running stream while they operate; use a clean bounded capture after completion.

## Capture Warning/Error evidence

Resolve the current PID after launch, then capture app-process warnings and errors plus the crash buffer:

```sh
adb shell pidof PACKAGE_NAME
adb logcat -d --pid=PID '*:W'
adb logcat -b crash -d
```

If native logs use a known tag, further filter by tag or stable diagnostic prefixes. Keep Android system warnings only when they correlate with the reproduction window or app PID. Do not treat unrelated platform noise as causation.

When logcat access is inconvenient, provide an in-app log viewer that:

- lives in the game page or a global overlay, not inside the screen that may freeze;
- opens from a top-level menu that does not intercept normal game input;
- displays only the bounded diagnostic ring;
- offers Copy and Clear actions;
- remains diagnostic-only and easy to remove.

If the entire UI thread is blocked, the overlay being untappable confirms only that its input path is also blocked. Use phase timing, ANR traces, Perfetto, or thread stacks to prove where execution is stuck.

## Interpret one cycle

After capture:

1. Align evidence to the user-reported reproduction window.
2. Identify the deepest confirmed boundary and the first missing or failed boundary.
3. Reject hypotheses contradicted by the logs.
4. Add the next probe only at the unresolved boundary.
5. Change behavior only when evidence supports a causal mechanism.
6. Build, reinstall, clear logs, and repeat the same user action.

If a diagnostic build appears fixed, report it as “not reproduced in this run.” Repeat the original path and compare build/config/cache state. Logging can change timing; a fresh rebuild or reinstall can also change state. Do not call the bug fixed until the causal change survives repeated reproduction.

## Finish the fix

Once the user confirms the defect is gone across repeated runs:

1. Remove temporary probes, diagnostic overlays, environment switches, and failed experimental changes.
2. Keep durable failure logs only when they are low-volume and operationally useful.
3. Rebuild Android and run the focused verification again.
4. Review the diff for unrelated or generated files.
5. Commit the causal fix separately from optional cleanup when practical.
6. Summarize the evidence chain: symptom, confirmed cause, fix, and verification. Do not claim more than was tested.
