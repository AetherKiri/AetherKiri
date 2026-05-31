# AetherKiri Development Guide

[English](development.md) | [简体中文](development.zh-CN.md)

This document is the main engineering reference for AetherKiri. It covers the
current Godot-native architecture, repository layout, important files, build
flows, test flows, probe configuration, platform notes, and debugging habits.

## Current Product Shape

AetherKiri is a Godot-hosted KiriKiri2 runtime:

```text
Godot App Shell
  -> Godot 4.6 GDExtension Host
    -> C ABI Engine Bridge
      -> C++ Engine Core
        -> KiriKiri Runtime / Plugins
```

The product path is the Godot app in `apps/godot_app`. Flutter and ANGLE are no
longer part of the product architecture.

## Rendering Architecture

The app exposes three renderer choices in the Godot settings UI:

| Backend | Role | Production target |
| --- | --- | --- |
| Godot Native | Default Godot-owned GPU rendering path. Textures and render resources are owned by Godot and displayed directly by the Godot UI. | Yes |
| GPU Bridge | Compatibility/performance bridge for external native GPU render targets imported into Godot. | Yes, as explicit fallback/comparison |
| Debug CPU | RGBA readback/upload fallback. Useful for debugging correctness when GPU paths are suspect. | No |

CPU upload must remain a debug fallback. Performance work should target Godot
Native first, then GPU Bridge where native coverage is incomplete.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `apps/godot_app/` | Godot project, scene, scripts, assets, export presets, and GDExtension descriptor. |
| `bridge/godot_extension/` | C++ GDExtension classes exposed to Godot, including the host node/player binding. |
| `bridge/engine_api/` | Stable C ABI between the Godot host and the engine core. Keep this narrow and versionable. |
| `cpp/core/` | KiriKiri runtime core: script VM, storage, events, window/layer system, rendering, audio, movie, and plugin infrastructure. |
| `cpp/plugins/` | Built-in plugin implementations and compatibility adapters registered as KiriKiri plugins. |
| `build/` | Platform build entry scripts and validation scripts. |
| `tests/` | Unit tests, fixtures, and generic probe profiles. Committed test profiles must not contain local absolute game paths. |
| `tools/` | Developer tools such as XP3 helpers and plugin audit utilities. |
| `vcpkg/` | Local vcpkg ports and triplets used by this project. |
| `.devtools/vcpkg/` | Optional local vcpkg checkout. This directory is ignored and can be recreated. |
| `docs/` | GitHub Pages site files, not engineering documentation. |
| `doc/` | Engineering documentation. Start here for development notes. |

## Important Files

| File | Purpose |
| --- | --- |
| `build.sh` | Unified build entry point. Dispatches to platform scripts in `build/`. |
| `build/build_macos.sh` | Builds C++ core/GDExtension for macOS, stages dylibs, exports the Godot macOS app. |
| `build/build_ios.sh` | Builds iOS device or simulator static libraries, exports and patches the Xcode project. |
| `build/build_android.sh` | Builds Android native libraries and exports APKs through Godot. |
| `CMakeLists.txt` | Top-level native build. Adds engine API, GDExtension, core, plugins, tests, and tools. |
| `CMakePresets.json` | Named CMake presets for macOS, iOS, Android, and related build directories. |
| `vcpkg.json` | Native dependency manifest. Godot Native must not depend on ANGLE. |
| `apps/godot_app/project.godot` | Godot project settings and input/export defaults. Do not commit local game paths here. |
| `apps/godot_app/export_presets.cfg` | Godot export presets for macOS, iOS, and Android. Signing-sensitive values may need local overrides. |
| `apps/godot_app/aether_kiri.gdextension` | GDExtension library map for debug/release and target platforms. |
| `apps/godot_app/scenes/main.tscn` | Main scene. Most UI is currently built by script. |
| `apps/godot_app/scripts/main.gd` | Main app shell: home UI, settings, game detail page, loading console, renderer selection, input forwarding, performance overlay. |
| `apps/godot_app/scripts/probe_config.gd` | Shared probe/test config loader. Reads `AETHERKIRI_TEST_CONFIG` and environment overrides. |
| `apps/godot_app/scripts/smoke_test.gd` | Basic engine startup smoke probe. |
| `apps/godot_app/scripts/step_render_probe.gd` | Click/step render probe that saves screenshots after configured actions. |
| `apps/godot_app/scripts/perf_input_probe.gd` | Input latency/performance probe around click/text behavior. |
| `apps/godot_app/scripts/sequence_perf_probe.gd` | Longer sequence FPS probe. |
| `apps/godot_app/scripts/gui_render_probe.gd` | GUI render screenshot probe. |
| `apps/godot_app/scripts/gpu_blend_self_test.gd` | Godot GPU blend self-test harness. |
| `bridge/godot_extension/src/aether_kiri_godot.cpp` | Godot-visible `AetherKiriPlayer` implementation and Godot method bindings. |
| `bridge/engine_api/include/engine_api.h` | C ABI exported by the engine bridge. |
| `bridge/engine_api/include/engine_options.h` | Engine option keys/values shared with host code. |
| `bridge/engine_api/src/engine_api.cpp` | C ABI implementation that creates, opens, ticks, renders, and receives input. |
| `cpp/core/environ/EngineLoop.*` | Main runtime lifecycle, tick loop, and host input conversion into TVP events. |
| `cpp/core/visual/LayerManager.*` | Layer hit testing, focus/capture, mouse/touch/key dispatch, and compatibility input behavior. |
| `cpp/core/visual/impl/DrawDevice.*` | Draw device bridge between window/layer updates and render managers. |
| `cpp/core/visual/impl/LayerBitmapImpl.*` | Bitmap/layer pixel operations and text drawing. |
| `cpp/core/visual/godot/` | Godot Native render manager and texture/backend code, when present in the branch. |
| `cpp/core/plugin/PluginImpl.cpp` | Plugin registration/loading path for internal KiriKiri plugin modules. |
| `cpp/plugins/CMakeLists.txt` | Plugin build wiring. Use this when adding or enabling plugin modules. |
| `tests/profiles/kr37s.json` | Generic probe profile. It intentionally has an empty `game_path`; use environment variables for local paths. |
| `doc/krkr2_plugins.md` | Reference list of known KiriKiri2 plugin names and source locations. |

## Build Prerequisites

Required for normal development:

- macOS with Xcode command line tools for macOS/iOS builds.
- CMake 3.28+ and Ninja.
- Godot 4.6.3 stable at `/Applications/Godot.app`, or set `GODOT_BIN`.
- Godot export templates for the platforms you build.
- vcpkg in `.devtools/vcpkg` or `VCPKG_ROOT`.

Android also needs:

- Android SDK and NDK.
- `ANDROID_HOME` or `ANDROID_SDK_ROOT`, otherwise the scripts try
  `$HOME/Library/Android/sdk`.

Useful environment variables:

| Variable | Purpose |
| --- | --- |
| `GODOT_BIN` | Override Godot executable path. |
| `GODOT_EXPORT_TEMPLATE` | Override a platform export template zip. |
| `VCPKG_ROOT` | Override vcpkg checkout. |
| `JOBS` | Parallel native build jobs. |
| `IOS_SIMULATOR_ARCH` | `arm64` or `x86_64` simulator build selection. |
| `AETHERKIRI_TEST_CONFIG` | JSON probe profile path. |
| `AETHERKIRI_SMOKE_GAME` | Local game path for probes; do not commit this into profiles. |
| `AETHERKIRI_RENDER_BACKEND` | Probe backend override. |
| `AETHERKIRI_INPUT_TRACE` | Enables engine/layer input traces. |
| `AETHERKIRI_PERF_LOG_INTERVAL` | Performance log interval in seconds. |
| `AETHERKIRI_FRAME_SPIKE_MS` | Logs frames slower than this threshold. |
| `AETHERKIRI_VERBOSE_RENDER_LOG` | Enables verbose render logging in the Godot shell. |

## Build Commands

Use `./build.sh <platform> <debug|release>`.

```bash
./build.sh macos debug
./build.sh macos release
./build.sh ios debug --simulator
./build.sh ios debug --simulator --simulator-arch=arm64
./build.sh ios release
./build.sh android debug --abi=arm64-v8a
./build.sh android release --abi=arm64-v8a
```

Clean a platform build:

```bash
./build.sh --clean macos release
```

## Platform Build And Launch

macOS debug:

```bash
./build.sh macos debug
out/godot/macos/debug/AetherKiri.app/Contents/MacOS/AetherKiri
```

macOS release:

```bash
./build.sh macos release
open -n out/godot/macos/release/AetherKiri.app
```

iOS Simulator:

```bash
./build.sh ios debug --simulator --simulator-arch=arm64
xcodebuild \
  -project out/godot/ios-simulator/debug/AetherKiri.xcodeproj \
  -scheme AetherKiri \
  -configuration Debug \
  -destination 'platform=iOS Simulator,name=iPad Pro 11-inch (M4)' \
  build
```

iOS device:

```bash
./build.sh ios release
xcodebuild \
  -project out/godot/ios/release/AetherKiri.xcodeproj \
  -scheme AetherKiri \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  -allowProvisioningUpdates \
  CODE_SIGN_IDENTITY='Apple Development' \
  build
xcrun devicectl device install app \
  --device <device-identifier> \
  /path/to/AetherKiri.app
```

Android debug:

```bash
./build.sh android debug --abi=arm64-v8a
adb install -r out/godot/android/debug/AetherKiri-debug.apk
adb shell monkey -p org.github.krkr2.aetherkiri \
  -c android.intent.category.LAUNCHER 1
```

Android release:

```bash
./build.sh android release --abi=arm64-v8a
```

The release APK is unsigned unless a release keystore is configured.

On iOS/iPadOS, copy games through the Files app into:

```text
On My iPad/iPhone -> AetherKiri -> Games
```

Return to AetherKiri and tap refresh.

## Runtime UI Flow

The Godot shell mirrors the old mobile app flow:

1. Home page scans known games.
2. On iOS/iPadOS, users copy games through Files and tap refresh.
3. On platforms with file-system access, the primary action can import/select a
   directory or package.
4. Game detail page shows path, play time, scrape/cover actions, rename/remove,
   and launch.
5. Launch switches to a full-page loading console. New log lines should
   auto-scroll to the bottom.
6. After startup succeeds, the shell hides and the game viewport fills the app.
7. Settings persist renderer, performance overlay, frame limit, orientation,
   and developer toggles.

## Input Pipeline

Godot events are handled in `apps/godot_app/scripts/main.gd` and forwarded to
`AetherKiriPlayer`, then through `EngineLoop` into TVP input events.

Important rules:

- Desktop mouse events may trigger a small forced tick to keep click/text reveal
  responsive.
- Mobile touch events must not also forward Godot's emulated mouse events.
  Otherwise one tap can become two KiriKiri clicks.
- Mobile touch should rely on the normal frame loop instead of extra forced
  ticks unless there is a measured reason.
- Save/load/gallery compatibility Enter fallbacks belong in
  `LayerManager.cpp`, and should only target visible, enabled selection layers.

## Test Profiles And Probes

Committed profiles live in `tests/profiles/`. They must be portable and must
not contain machine-local absolute paths. Use environment variables for local
games:

```bash
AETHERKIRI_TEST_CONFIG="$PWD/tests/profiles/kr37s.json" \
AETHERKIRI_SMOKE_GAME="/path/to/game" \
/Applications/Godot.app/Contents/MacOS/Godot \
  --path apps/godot_app \
  --script res://scripts/smoke_test.gd
```

Step render probe:

```bash
AETHERKIRI_TEST_CONFIG="$PWD/tests/profiles/kr37s.json" \
AETHERKIRI_SMOKE_GAME="/path/to/game" \
/Applications/Godot.app/Contents/MacOS/Godot \
  --path apps/godot_app \
  --script res://scripts/step_render_probe.gd
```

Input/performance probe:

```bash
AETHERKIRI_TEST_CONFIG="$PWD/tests/profiles/kr37s.json" \
AETHERKIRI_SMOKE_GAME="/path/to/game" \
out/godot/macos/debug/AetherKiri.app/Contents/MacOS/AetherKiri \
  --script res://scripts/perf_input_probe.gd
```

Common profile fields:

| Field | Purpose |
| --- | --- |
| `game_path` | Optional portable game path. Prefer empty in committed profiles. |
| `backend` | Renderer name, usually `Godot Native`. |
| `surface_size` | Engine render size, for example `[1280, 720]`. |
| `window_size` | Probe window size. |
| `coord_size` | Coordinate space used by clicks. |
| `startup_timeout_frames` | Startup wait limit. |
| `warmup_frames` | Frames to wait after startup before measuring. |
| `after_click_frames` | Default wait after each configured click. |
| `measure_frames` | FPS measurement window. |
| `clicks` | Ordered click steps with `x`, `y`, optional `name`, and optional `after_frames`. |
| `perf_input` | Extra click/frame settings for input latency probes. |

## Validation Checklist

Before pushing engine/render/input work, run the narrowest checks that cover the
change:

```bash
/Applications/Godot.app/Contents/MacOS/Godot \
  --headless \
  --path apps/godot_app \
  --check-only \
  --quit
```

Build checks:

```bash
./build.sh macos debug
./build.sh macos release
./build.sh ios debug --simulator
./build.sh ios release
./build.sh android debug --abi=arm64-v8a
```

Renderer migration checks:

```bash
rg "F[l]utter|f[l]utter|A[N]GLE|Platform[ ]Graphics" \
  README.md README.zh-CN.md apps bridge build CMakeLists.txt
rg "u[n]official-angle|l[i]bEGL|l[i]bGLESv2" \
  CMakeLists.txt bridge cpp build vcpkg.json
build/validate_godot_native.sh
build/validate_gpu_bridge.sh
```

Manual game smoke:

- Open a game.
- Reach the title page.
- Start or load a save.
- Verify mouse/touch input.
- Verify text reveal and click-to-complete text behavior.
- Open menu.
- Verify audio.
- Save once.
- Exit back to shell.
- Check FPS and frame spike logs.

## Debugging Notes

Useful traces:

```bash
AETHERKIRI_INPUT_TRACE=1 ...
AETHERKIRI_FRAME_SPIKE_MS=20 ...
AETHERKIRI_VERBOSE_RENDER_LOG=1 ...
```

On iOS device:

```bash
xcrun devicectl device process launch \
  --device <device-identifier> \
  --terminate-existing \
  --console \
  --environment-variables '{"AETHERKIRI_INPUT_TRACE":"1"}' \
  com.liuyu.aetherkiri.kr37s
```

Use `--console` only when actively collecting logs, because it waits for the
process and can keep a terminal session occupied.

## Code Change Guidelines

- Keep the Godot app as the product shell.
- Keep local test paths out of committed project settings and profiles.
- Prefer backend-specific fixes over broad behavior changes in the engine core.
- Do not silently downgrade performance paths to Debug CPU.
- Treat `bridge/engine_api` as a stable boundary.
- Add plugin compatibility only when behavior is real or intentionally
  documented; avoid pretending unsupported APIs succeeded.
- Preserve user work in the working tree. Do not reset or revert unrelated
  files.
