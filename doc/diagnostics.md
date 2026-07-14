# Unified Diagnostic Sessions

English | [简体中文](diagnostics.zh-CN.md)

Diagnostic sessions replace manual correlation of Godot output, native logs, platform logs, and temporary performance probes. Debug builds use the bounded low-overhead `baseline` profile by default; Release defaults to off and can be enabled explicitly in settings or by a host capture request.

## Maintainer quick path

| Need | Entry point | Result |
| --- | --- | --- |
| Inspect live frame, log, input, or plugin state | Settings → Diagnostics → Baseline, then open `DBG` in game | In-app debug drawer; no computer required |
| Capture a hard-to-describe issue for another maintainer | `python3 tools/diagnose.py run <platform>` | `out/diagnostics/...zip` |
| UI is frozen and cannot mark | Press Enter in the capture terminal | Host marker with `ui_marker_missing=true` plus platform evidence |
| Investigate an existing ZIP | Give it to Codex with `unpack-investigate-artifacts` | Static evidence report; no launch or collection |

## In-app debugging

Settings separate **Diagnostics**, **Compatibility**, and folded **Advanced Developer Tools**. Diagnostics chooses one collection profile, the off/summary/detailed performance overlay, and error-context attachment. Compatibility controls plugin load scope and missing-plugin compatibility objects. Advanced plugin/full traces, legacy console-file logging, and TJS export apply to the current run only; high-overhead traces time out after 30 seconds.

The old developer switches now have explicit boundaries:

| Previous switch | New location | Purpose and overlap |
| --- | --- | --- |
| Plugin Load Mode | Compatibility → Plugin Scope | Runtime compatibility policy, not a logging level; `core` is the default and `full` retains legacy registration. |
| Mock Bypass | Compatibility → Missing Plugin Compatibility | Returns compatibility objects for absent plugins. Keep it on for normal play; turn it off only to expose the true compatibility boundary. |
| Plugin Call Trace | Advanced Developer Tools | Per-call `plugin_trace.log`; finer and more expensive than the `plugin` profile, so use only for a narrow window. |
| Trace Log | Advanced Developer Tools | Native spdlog trace detail; partially overlaps `full`, but remains useful when structured events stop at a native boundary. |
| Console Log File | Advanced Developer Tools | Legacy `krkr.console.log`; mostly superseded by unified/platform logs and retained for old workflows only. |
| Export TJS Scripts | Advanced Developer Tools | Exports TJS bytecode disassembly while loading; a script compatibility tool rather than general logging. |
| Log Alerts | Removed | Modal dialogs for every warning/error duplicated Events, interrupted play, and changed timing. |
| Attach Logs to Errors | Diagnostics → Error Context | Adds the latest 20 engine lines only to real error dialogs, so an explicit failure remains understandable outside the drawer. |

During play, **Mark Issue** synchronously inserts Godot and C++ markers, seals the ten seconds before plus five seconds after it, and saves bounded performance/input/memory/plugin context without taking a screenshot. The adjacent **DBG** button opens a drawer whose Overview, Events, Logs, Input, and Plugins tabs expose bounded live state. It can create a typed marker, state snapshot, explicit screenshot, next-slow-frame capture, self-check, copied summary, or ZIP export. Closed drawers do not format runtime text; open drawers refresh at 4 Hz, and rendered logs are capped at 200 lines.

## One-command reproduction

```bash
python3 tools/diagnose.py run android
python3 tools/diagnose.py run macos
python3 tools/diagnose.py run ios --device "device name or UDID"
python3 tools/diagnose.py run ios-simulator
python3 tools/diagnose.py run web
```

The command uses the already installed or exported app by default, restarts it, and begins capture without building or installing. Use `--build-install` only when the app must be updated; combine it with `--reuse-build` to install an existing artifact without rebuilding. `--profile` is authoritative for that host session and overrides the saved in-app profile. Reproduce the symptom, tap **Mark Issue**, then press Enter in the terminal. Use `--duration 30` without an interactive terminal.

An accepted marker changes the button to **Marked #N**, shows a saved confirmation, vibrates briefly on mobile, and emits a `[diagnostics] issue_marker` console/logcat line. The event stream and matching pre/post incident files are flushed immediately. The button explicitly reports when the eight-marker session limit is reached.

Android validates `adb devices` before capture and preserves complete wireless mDNS serials even when Bonjour adds a suffix containing spaces. It prefers the ADB under `ANDROID_SDK_ROOT`/`ANDROID_HOME`. Installation only occurs in `--build-install` mode and retries once only for a transport drop. A missing, unauthorized, or offline device fails immediately; use `--device SERIAL` when more than one ready device is connected.

Raw mobile diagnostic files are accessible without root:

- Android: `/storage/emulated/0/Documents/AetherKiri/Diagnostics/<session>/`
- iOS/iPadOS: Files → On My iPhone/iPad → AetherKiri → `AetherKiri/Diagnostics/<session>/`; Finder/iTunes file sharing is also enabled.

Results are written to `out/diagnostics/<timestamp>-<platform>-<session>/` and a matching ZIP. A bundle contains build metadata, unified JSONL events, each marker window, platform evidence, explicit state snapshots/screenshots under `attachments/`, and a summary that does not claim causation from timing alone. If the UI is blocked, collection adds a host marker with `ui_marker_missing=true`.

The bundle contract is `metadata.json`, normalized `events.jsonl`, `incidents/`, optional `attachments/`, raw `platform/` evidence, and generated `summary.md`. Treat the summary as a lead and verify material conclusions against structured and raw evidence.

## Profiles

`--profile` accepts `baseline`, `input`, `render`, `storage`, `script`, `audio`, `video`, `plugin`, `system`, and `full`. Baseline records lifecycle events, warnings/errors, one-second frame aggregates, and phase breakdowns above 20 ms. `system` enables bounded platform tracing where available. `full` is deliberately high-overhead and should only be used for short captures.

Repeat the capture command directly for consecutive reproductions. Baseline never flushes per frame: it flushes once per second, on marker, on backgrounding, and on shutdown. The in-memory ring is capped at 2,000 events, event files rotate at 4 MiB, and one session accepts at most eight incident markers.

## Platform evidence

- Android: PID logcat, Warning/Error and crash buffers, meminfo, gfx framestats, app diagnostic files, and optional Perfetto.
- iOS: devicectl/simctl console, app data container, and device crash logs.
- macOS: stdout/stderr, unified logs, and the Godot app-data directory.
- Web: structured events through the existing Vite client-log endpoint; deployed builds download an `.aetherdiag.json` fallback.

Raw paths, game names, and log text are retained by default. Screenshots remain opt-in so capture does not perturb small performance defects. Validation commands are listed in the Chinese document.

## Common failures and artifact investigation

- Android wireless transports must report the real `device` state in `adb devices -l`; offline or unauthorized transports are not ready devices.
- If an installed app never creates its public diagnostic directory, update it once with `--build-install`.
- A successful app marker provides button feedback, status text, mobile vibration, and a `[diagnostics] issue_marker` platform line.
- Keep a bundle without an app marker; its host marker still anchors platform evidence, while clearly identifying the missing app-side boundary.
- With queue drops, shorten the capture before increasing the profile. Use `full` and per-call traces only for narrow windows.

The `unpack-investigate-artifacts` skill consumes an existing ZIP or directory only. It does not run `tools/diagnose.py`. It validates archive safety and provenance, then aligns sessions, clocks, sequences, queue drops, markers, snapshots, screenshots, self-checks, and raw platform evidence before naming the deepest confirmed boundary.
