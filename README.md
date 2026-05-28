# AetherKiri

AetherKiri is a Godot-hosted KiriKiri2 runtime. The project is migrating to a
C++ engine core loaded by a Godot 4.6 GDExtension, with Godot-owned rendering as
the default product path.

## Architecture

```
Godot App Shell
  -> GDExtension Host
    -> C++ Engine Core
      -> KiriKiri Runtime / Plugins
```

The default renderer is **Godot Native**. It is intended to render through
Godot `RenderingDevice` resources owned by the Godot app. **GPU Bridge** remains
as an explicit compatibility/performance backend for external native GPU render
targets imported by Godot. **Debug CPU** is a visible fallback path only and is
not accepted as a performance target.

## Repository Layout

- `apps/godot_app/` - Godot project, scenes, settings UI, performance/log panel,
  and export presets.
- `bridge/godot_extension/` - Godot native host library entry points.
- `bridge/engine_api/` - C ABI used by the host layer to drive the C++ engine.
- `cpp/core/` - KiriKiri2 runtime, visual system, audio, storage, VM, and plugin
  support.
- `cpp/plugins/` - bundled native plugin implementations and compatibility
  stubs.

## Render Backends

| Backend | Purpose |
| --- | --- |
| Godot Native | Default Godot-owned GPU rendering path. |
| GPU Bridge | Explicit external GPU render-target bridge for comparison and compatibility. |
| Debug CPU | RGBA readback/upload fallback for debugging only. |

The Godot settings UI persists the selected backend and warns when changing it
while a game session is active, because render resources must be recreated.

## Building

Prerequisites:

- CMake 3.28+
- Ninja
- vcpkg, either in `.devtools/vcpkg` or via `VCPKG_ROOT`
- Godot at `/Applications/Godot.app` or `GODOT_BIN=/path/to/Godot`
- Xcode for macOS/iOS exports

```bash
./build.sh macos debug
./build.sh ios debug --simulator
```

The scripts build the native engine and Godot host library, stage them under
`apps/godot_app/bin/`, then run the matching Godot export preset when Godot is
available.

## Validation

Useful migration checks:

```bash
rg "F[l]utter|f[l]utter|A[N]GLE|Platform[ ]Graphics" README.md apps bridge build CMakeLists.txt
rg "u[n]official-angle|l[i]bEGL|l[i]bGLESv2" CMakeLists.txt bridge cpp build vcpkg.json
./build.sh macos debug
./build.sh ios debug --simulator
build/validate_godot_native.sh
build/validate_gpu_bridge.sh
```

Game smoke target:

```text
/Users/liuyu/gal/奶牛5 KR3.7S
```

Acceptance requires startup, rendering, input, menu operations, audio, save
paths, clean exit, and performance parity from Godot Native or GPU Bridge. Debug
CPU is only a diagnostic fallback.
