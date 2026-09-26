<p align="center">
  <img src="apps/godot_app/assets/icon.png" width="112" alt="AetherKiri app icon">
</p>

<h1 align="center">AetherKiri</h1>

<p align="center">
  A Godot-hosted, extensible multi-runtime platform for visual novels.<br>
  <a href="README.md">English</a> · <a href="README.zh-CN.md">简体中文</a>
</p>

<p align="center">
  <a href="https://github.com/AetherKiri/AetherKiri/actions/workflows/build.yml"><img alt="macOS Build" src="https://img.shields.io/github/actions/workflow/status/AetherKiri/AetherKiri/build.yml?branch=main&amp;job=Build%20macOS%20App&amp;label=macOS"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/actions/workflows/build.yml"><img alt="iOS Build" src="https://img.shields.io/github/actions/workflow/status/AetherKiri/AetherKiri/build.yml?branch=main&amp;job=Build%20iOS%20App&amp;label=iOS"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/actions/workflows/build.yml"><img alt="Android Build" src="https://img.shields.io/github/actions/workflow/status/AetherKiri/AetherKiri/build.yml?branch=main&amp;job=Build%20Android%20App&amp;label=Android"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/blob/main/LICENSE"><img alt="License" src="https://img.shields.io/github/license/AetherKiri/AetherKiri?logo=gnu&label=license"></a>
</p>

## Overview

AetherKiri runs multiple visual-novel engines inside one Godot 4.7 app. A
single `AetherRuntimePlayer` hosts the native runtimes behind a versioned
provider ABI, while Godot owns the product surface: UI, final-frame
presentation, input, settings, export presets, and platform packaging. The
runtime dispatcher selects the engine from each game's markers and
capabilities.

```text
Godot App Shell
  └─ AetherRuntimePlayer
       └─ Runtime Dispatcher
            ├─ KiriRuntime ─── KiriKiri2 core / plugins
            ├─ OnsRuntime ─── OnscripterYuri
            ├─ SiglusRuntime ─ siglus_rs
            ├─ A Runtime ───── Artemis (private package)
            └─ C Runtime ───── CatSystem2 (private package)
```

The default product renderer is **Godot Native**: engine frames are rendered
through Godot-owned `RenderingDevice` resources. **GPU Bridge** is an optional
compatibility and comparison backend for external native GPU render targets,
and **Debug CPU** is a diagnostic fallback only.

## Highlights

- One stable `AetherRuntimePlayer` with a versioned provider ABI for current
  and future runtimes.
- C++17 KiriKiri2 engine core (visual, audio, storage, VM, plugins) as the
  [`AetherKrkr`](https://github.com/AetherKiri/AetherKrkr) submodule.
- OnscripterYuri integration with Godot pointer/touch/keyboard input mapped
  back to ONS events.
- Export presets for macOS, iOS/iPadOS, Android, Web, and Linux.
- Runtime-selectable render backend with persisted settings.
- Bundled multilingual KAG3 demo, playable from the library and deletable by
  the player.
- Smoke, render, interaction, performance, and manual-repro probe scripts.
- Manual compatibility notes for tested titles in
  [`doc/verified_games.md`](doc/verified_games.md).

In distributed builds, OnsRuntime, A Runtime, and C Runtime are beta features
that require an active 30-day coffee entitlement. Debug builds keep these
runtimes unrestricted for compatibility development and testing.

## Platform Support

| Platform | Minimum | Product package |
| --- | --- | --- |
| macOS | macOS 13.0 (Ventura) | App Store (Apple Silicon) |
| iOS / iPadOS | iOS / iPadOS 16.0, `arm64` | App Store; simulator builds for development |
| Android | Android 8.0 (API 26), `arm64-v8a` | GitHub Release APK |
| Web | Browser with wasm SIMD + threads, `SharedArrayBuffer`, COOP/COEP | Self-hosted static export |
| Linux | Build from source | `x86_64` export via `./build.sh linux release` |
| Windows | Build from source | Native targets compile locally |

## Repository Layout

| Area | Path | Purpose |
| --- | --- | --- |
| Product | `apps/godot_app/` | Godot project: scenes, settings UI, library, export presets. |
| ABI | `abi/` | Stable C ABI the host uses to drive the C++ engines. |
| Glue | `bridge/godot_extension/` | GDExtension host entry points. |
| | `bridge/krkr2_runtime/` | KiriKiri2 integration glue (legacy engine implementation, host hooks). |
| | `bridge/onscripter_runtime/` | Headless OnscripterYuri host, frame capture, input bridge. |
| | `bridge/siglus_runtime/` | Siglus provider plus the build-time overlay applied to pristine `siglus_rs`. |
| Engines | `packages/AetherKrkr/` | KiriKiri2 engine runtime submodule. |
| | `packages/OnscripterYuri/` | Public OnscripterYuri submodule. |
| | `packages/AetherSiglus/` | Public siglus_rs submodule (kept pristine). |
| | `packages/AetherInternal/` | Optional private E-mote package; public builds work without it. |
| Support | `demos/aetherkiri-kag3/` | Source of the built-in KAG3 demo. |
| | `tests/` | Unit tests, fixtures, and portable probe profiles. |
| | `tools/` | Developer and compatibility tools (xp3 archive tools). |
| Docs | `doc/` | Development guide, diagnostics, plugin notes, verified games, release guide. |

## Getting Started

### Prerequisites

| Tool | Needed for | Notes |
| --- | --- | --- |
| CMake 3.28+, Ninja | All builds | |
| NASM | Native FFmpeg | |
| vcpkg | All builds | `.devtools/vcpkg` or `VCPKG_ROOT` |
| Godot 4.7 | Exports | `/Applications/Godot.app` or `GODOT_BIN=/path/to/Godot` |
| Rust (rustup) | Siglus runtime | `rustup target add aarch64-linux-android`; builds degrade to "Siglus disabled" without it |
| Xcode | macOS / iOS | Tagged iOS releases need the iOS 26 SDK |
| Android SDK/NDK | Android | NDK `28.1.13356709`; `ANDROID_HOME` or `$HOME/Library/Android/sdk` |
| Emscripten (emsdk) | Web | `emcc`/`em++`/`emar` on PATH; Godot dlink templates (`web_dlink_*.zip`) |
| Node.js + npm | Web dev server | TypeScript/Vite |
| E-mote SDK | Private E-mote builds | `packages/AetherInternal/tools/install_emote_sdk.sh` |

### Build

```bash
git submodule update --init \
  packages/AetherKrkr packages/OnscripterYuri packages/psdfile \
  packages/AetherSiglus packages/AetherMinori
./build.sh macos debug        # or: macos release / ios debug --simulator /
                               # ios release / android debug --abi=arm64-v8a /
                               / web debug / linux release
```

These five public runtime submodules are the minimum for a default build
(the engine repository is a hard configure requirement; Siglus and Minori
degrade gracefully without a Rust toolchain, but the checkouts must be
present). `AetherInternal`, `tjs2Decompiler`, and `rfvp` are optional — see
[Optional Components](#optional-components). `AetherKrkr`, `psdfile`, and
`AetherMinori` are recorded with SSH URLs in `.gitmodules`; without SSH
credentials, map them to HTTPS once, the same rewrite GitHub-hosted runners
apply automatically:

```bash
git config --global url."https://github.com/".insteadOf git@github.com:
```

The scripts build the native engine and Godot host library, stage them under
`apps/godot_app/bin/`, and run the matching Godot export preset when Godot is
available. Linux exports bundle the required vcpkg shared libraries beside the
executable.

### Optional Components

- **Private package** — maintainers with access to the complete E-mote and
  native Live2D implementations can enable them before building:

  ```bash
  git submodule update --init packages/AetherInternal
  packages/AetherInternal/tools/install_emote_sdk.sh
  ```

  CMake enables the package automatically when present; a package/API
  mismatch stops configuration instead of building an incompatible
  combination. Trusted runs of the `Build` workflow use the `AETHERSECRET`
  read-only SSH key; fork and Dependabot runs use the public fallback. Use
  `-DAETHERKIRI_ENABLE_INTERNAL=OFF` to test the public fallback explicitly.

- **Runtime checkout overrides** — each submodule accepts a local checkout
  path for engine-side hot iteration without committing a gitlink bump:
  `AETHERKIRI_KRKR_DIR`, `AETHERKIRI_ONSCRIPTERYURI_DIR`,
  `AETHERKIRI_SIGLUS_DIR`, `AETHERKIRI_MINORI_DIR`, `AETHERKIRI_RFVP_DIR`,
  and `AETHERKIRI_INTERNAL_DIR`.

- **TJS2 analysis helper** — `git submodule update --init
  packages/tjs2Decompiler` (see
  [development.md](doc/development.md#compiled-tjs2-analysis)).

- **Linux bootstrap** — the Linux environment is project-local and keeps all
  tool downloads in `.aetherkiri-cache/` (override with
  `AETHERKIRI_CACHE_DIR`):

  ```bash
  ./tools/setup_linux.sh
  ```

## Run

### macOS

```bash
./build.sh macos release
open out/godot/macos/release/Aether.app
```

Run the debug build from a terminal to inspect logs, or pass a local test game
for the current run only:

```bash
AETHERKIRI_GAME_PATH="/path/to/game" \
  out/godot/macos/debug/Aether.app/Contents/MacOS/Aether
```

### iOS / iPadOS

```bash
./build.sh ios debug --simulator     # simulator
./build.sh ios release               # device (Xcode project in out/godot/ios/release/)
```

Install the device build with Xcode, or from the command line:

```bash
xcodebuild -project out/godot/ios/release/Aether.xcodeproj \
  -scheme Aether -configuration Release \
  -destination 'generic/platform=iOS' -allowProvisioningUpdates build
xcrun devicectl device install app --device <id> <path>/Aether.app
```

Copy games through the Files app into
`On My iPhone/iPad → Aether → Games`, then tap refresh in the app.

### Android

```bash
./build.sh android debug --abi=arm64-v8a
adb install -r out/godot/android/debug/Aether-debug.apk
adb shell monkey -p org.github.krkr2.aetherkiri -c android.intent.category.LAUNCHER 1
```

The release APK (`out/godot/android/release/Aether-release.apk`) is unsigned
until a release keystore is configured — sign it with `apksigner` before
distributing.

### Web

```bash
source /path/to/emsdk/emsdk_env.sh
./build.sh web debug
npm install && npm run web:dev:debug   # serves with the required COOP/COEP headers
```

The export lands in `out/godot/web/<debug|release>/index.html`. Cloud
deployments import games through the browser's file/directory picker and mount
the authorized files with on-demand Range reads; saves persist to the site's
IndexedDB-backed `/userfs`. For local development only, Vite can expose a
read-only test game root via `AETHERKIRI_GAME_ROOT` (see
[development.md](doc/development.md)).

## Render Backends

| Backend | Role |
| --- | --- |
| Godot Native | Default product path — Godot-owned GPU rendering. |
| GPU Bridge | Optional backend for external GPU render targets (compatibility, comparison). |
| Debug CPU | RGBA readback/upload fallback, diagnostics only. |

The settings UI persists the selection and warns when switching during a game
session, because render resources must be recreated.

## Runtime Notes

- **ONScripter games** are detected by their markers (`0.txt`, `00.txt`,
  `nscript.dat`, `nscr_sec.dat`, `nscript.___`, `onscript.nt2`,
  `onscript.nt3`). Saves are written to the game's `savedata/` directory so
  they survive app updates; the default script encoding matches upstream (GBK)
  and can be overridden with `AETHERKIRI_ONS_ENCODING=gbk|sjis|utf8`. ONS
  `mpegplay`/`avi`/`movie` commands share AetherKiri's FFmpeg media pipeline.
  Integration permission and licensing are recorded in
  [upstream issue #75](https://github.com/YuriSizuku/OnscripterYuri/issues/75).
- **Built-in demo** — the multilingual AetherKiri KAG3 demo ships inside the
  app package and is staged into the library on first launch, using the same
  flow as an imported game. Deleting it removes the staged copy and its saves.
  Source: [`demos/aetherkiri-kag3/`](demos/aetherkiri-kag3/).

## Testing & Probes

```bash
AETHERKIRI_SMOKE_GAME="/path/to/game" \
  /Applications/Godot.app/Contents/MacOS/Godot --headless \
  --path apps/godot_app --script res://scripts/smoke_test.gd
```

Probe configuration (`AETHERKIRI_TEST_CONFIG`), render/interaction/performance
probes, profile fields, and the validation checklist are documented in
[development.md](doc/development.md#test-profiles-and-probes). The repository
also ships a minimal ONS fixture (`tests/fixtures/onscripter_smoke`) and an
ONScripter movie-command fixture
(`tests/fixtures/onscripter_movie_smoke/video.avi`).

## Releases

Tagged releases (SemVer) drive the `Release` workflow: App Store signing,
upload to App Store Connect, and the GitHub Release. See
[doc/release.md](doc/release.md) for the required repository secrets and the
store packaging flow.

## Documentation

| Document | Contents |
| --- | --- |
| [`doc/development.md`](doc/development.md) | Developer guide: architecture, file roles, build, testing, probes, debugging. |
| [`doc/diagnostics.md`](doc/diagnostics.md) | In-app debugger, one-command collection, evidence-first investigation. |
| [`doc/krkr2_plugins.md`](doc/krkr2_plugins.md) | KiriKiri2 plugin notes. |
| [`doc/verified_games.md`](doc/verified_games.md) | Manually smoke-tested games. |
| [`doc/release.md`](doc/release.md) | Tagged release and App Store signing guide. |
| [`tools/README.md`](tools/README.md) | Tools. |

## License

AetherKiri is distributed under GPL-3.0-or-later (`LICENSE`); third-party
notices are preserved in `THIRD_PARTY_LICENSES.md`. For Apple App Store
distribution, `COPYING.iOS` records the limited additional permission granted
by approving copyright holders solely for the official Aether iOS or macOS
release, or a distributor they authorize in writing; third-party forks and
derivative apps may not rely on that permission, which neither grants rights
in upstream or third-party material on behalf of other copyright holders nor
revokes rights already granted by the GPL.
