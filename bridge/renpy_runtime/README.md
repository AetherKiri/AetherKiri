# Ren'Py SDK provider

This is an explicitly opt-in bootstrap milestone. Configure
`AETHERKIRI_ENABLE_RENPY=ON` and point `AETHERKIRI_RENPY_SDK_ROOT` at an
official desktop Ren'Py SDK checkout. The provider validates a `game/script.rpy`
project and launches the SDK executable directly, without a shell.

The provider stages an opt-in Python overlay into `game/libs` while a game is
open. The overlay exports real renderer pixels through the AKRF1 transport and
forwards input through JSONL; staging is removed when the provider closes. A
read-only game root returns `ENGINE_RESULT_NOT_SUPPORTED` with a diagnostic.
Native GPU texture import remains unsupported, so hosts use the RGBA frame
path while the SDK-owned window stays available for debugging.

The Ren'Py game keeps its own logical canvas size from `game/options.rpy` (for
example 640x360). AetherKiri presents that frame in the same aspect-preserving
host surface used by the AR/KrKr runtimes, so a 16:9 Ren'Py game is enlarged to
the available display without stretching. Pointer coordinates are mapped back
from the enlarged display to the logical Ren'Py canvas before input is sent.

The process is started with an argv array and an exec-error pipe, polled with
`waitpid`/`WaitForSingleObject`, and terminated during provider destruction.
Paths are validated before launch and are never passed through a shell.

## Experimental Python overlay

`python/aether_renpy_overlay.py` is an opt-in Ren'Py-side prototype. Set
`AETHERKIRI_RENPY_OVERLAY=1` and import `install()` from the game's
`game/options.rpy` to hook `renpy.display.core.Interface.draw_screen`.

The hook uses the active SDK renderer (`renpy.display.draw`) after the real
draw. On Ren'Py 8.5.x, the compatibility `pygame.image` module does not
provide `tostring`; the overlay copies the live `Surface._pixels_address`
using its pitch and channel masks, then emits tightly packed RGBA bytes. The
frame file is atomically replaced on each draw and has this little-endian
layout:

```
AKRF1\0\0\0 | u32 width | u32 height | u64 serial | u32 payload_len | RGBA...
```

Set `AETHERKIRI_RENPY_FRAME` to select the frame path. Input is consumed from
`AETHERKIRI_RENPY_INPUT` as JSON lines during Ren'Py periodic callbacks:
`{"type": 768, "attributes": {"key": 13, "mod": 0, "unicode": "\r", "scancode": 40, "repeat": false}}`
is a `KEYDOWN` event; `mod`, `unicode`, `scancode`, and `repeat` default to
zero, empty, zero, and false when omitted. Mouse events use the usual `pos`,
`button`, and `rel` attributes. Set `AETHERKIRI_RENPY_ERROR` to an optional
append-only diagnostics path; frame and input failures are reported there
without terminating the SDK process.

The reproducible desktop probe is `tools/run_renpy_overlay_probe.sh`; run it
with `RENPY_SDK=/path/to/renpy-8.5.3-sdk`. It launches the official SDK with
SDL's dummy drivers, verifies a nonblank 640x360 frame, injects Down+Return,
and checks that the Ren'Py menu selection reaches script code. It uses the
SDK's `gl2` renderer by default; set `RENPY_RENDERER=sw` to exercise the
software renderer instead.

The overlay is desktop-only. On Android and iOS, `AETHERKIRI_ENABLE_RENPY`
builds a provider registration stub that reports an explicit
`ENGINE_RESULT_NOT_SUPPORTED` from game open and all runtime operations; this
prevents a staged archive from being mistaken for playable mobile support.
The provider's desktop integration remains guarded by
`AETHERKIRI_ENABLE_RENPY` and requires a writable `game/libs` directory to
stage the hook.

## Mobile dependency staging

The official mobile support packages are staged separately with
`tools/install_renpy_mobile_support.sh --platform android|ios|both`. The
script verifies the Ren'Py 8.5.3 RAPT and Renios archives before extracting
them and records the verified checksums beside each staged package. These
inputs are not a mobile runtime: Android still needs the
`PythonSDLActivity`/JNI bootstrap, iOS still needs an in-process Xcode
adapter, and both platforms still need lifecycle, input, and Godot rendering
integration.

When `AETHERKIRI_ENABLE_RENPY=ON` and
`AETHERKIRI_RENPY_MOBILE_ROOT` points at that staged root, `scripts/build_ios.sh`
folds the selected Renios `prebuilt/{debug,release}` static-library closure
into the Godot iOS extension archive and bundles the official Renios resources
plus `MetalANGLE.xcframework` under `Aether/renios` and `Aether/Frameworks`.
If a generated Renios game `base/` directory is available, set
`AETHERKIRI_RENPY_RENIOS_BASE` to bundle it alongside those resources.
The prototype's entrypoint source and `libSDL2main.a` are intentionally omitted;
Godot remains the sole UIKit/SDL application host. The generated
`renios/renios-manifest.txt` records the exact closure. This is a link/bundle
smoke only: the provider continues to return `ENGINE_RESULT_NOT_SUPPORTED`
until the host-owned lifecycle, surface rendering, and input adapter are
implemented and validated on a device or simulator.

### iOS in-process adapter boundary

The staged Renios prototype is an application template whose `main.c` calls
`SDL_UIKitRunApp(..., launcher_main)`. AetherKiri already owns the Godot
application and run loop, so the mobile provider does not copy that entrypoint
or call `UIApplicationMain`. `renpy_runtime_ios_adapter.h` defines the small
host-owned adapter registration boundary that a future iOS integration can
install after linking the complete Renios static-library closure. The same
header exposes `renpy_get_ios_launcher_contract()`: the build probe confirms
that the shipped `launcher_main` reaches blocking `Py_RunMain`, so it cannot be
called from a Godot frame callback. Run
`tools/test_renios_ios_launcher.sh <mobile-root> <debug|release>` to repeat
that archive check. Until a split init/tick/shutdown launcher and its
lifecycle, surface, and input bridge are linked, `runtime=renpy` continues to
return `ENGINE_RESULT_NOT_SUPPORTED` with an explicit diagnostic.

### Android in-process adapter boundary

The staged RAPT prototype is a `PythonSDLActivity` that loads
`librenpython.so`, prepares Android storage/assets, and owns an SDL surface.
`scripts/build_android.sh` now stages the arm64 library, private/assets
directory, and the official Java/resource templates under
`assets/renpy_mobile/rapt` in the existing Godot export. The RAPT manifest and
`PythonSDLActivity` remain assets and are never merged into the host manifest,
so the export still has one Activity. A small host-owned
`org.github.krkr2.aetherkiri.RenPyMobileBridge` Java shim binds an already
running Godot Activity to the engine JNI bridge; it does not launch RAPT or
load its SDL loop. Set `AETHERKIRI_RENPY_ANDROID_PRIVATE_ASSETS` when a built
Ren'Py game's private payload is available.

The export also compiles two host-owned, non-Activity signature shims at
`org.libsdl.app.SDLActivity` and `org.renpy.android.PythonSDLActivity`. They
only bind the existing Activity and declare the callbacks that preflight
checks; the complete RAPT Activity remains under `assets/renpy_mobile/rapt`.
If a Godot template already owns either class, staging fails closed instead of
overwriting a possible SDL singleton.

The SDL shim's `getNativeSurface()` and `getContext()` callbacks return the
same Java `Surface` and Application Context already held by `EngineBridge`; no
new View or Surface is allocated. This is safe for inspection and preflight,
but the official `librenpython.so` still has no host-tick or
`SDL_AndroidSetActivity`/`SDL_AndroidSetSurface` API. Enabling gameplay next
requires rebuilding the RAPT native payload around an explicit
`init(context, surface)`, `tick`, `pause/resume`, input, and `shutdown`
interface rather than calling its blocking `SDL_main` entrypoint.

The Android provider also has a compile-tested `BootstrapAdapter` boundary that
accepts an opaque pointer to the existing host Activity through the host
extension slot `reserved_ptr[1]` or the JNI shim. It never creates a second
Activity. Because RAPT embeds its own SDL/Python runtime and lifecycle,
asset/JNI staging alone is not a playable integration: until SDL surface,
lifecycle, input, and Godot rendering handoff are complete, `Start` and
provider open return `ENGINE_RESULT_NOT_SUPPORTED` with an explicit diagnostic.
Before attempting that handoff, the Android `BootstrapAdapter::Preflight`
loader checks the bound host Activity, required `SDLActivity` and
`PythonSDLActivity` JNI methods, a live host `ANativeWindow`, and the exported
`librenpython.so` entrypoints (`SDL_main`, SDL Android accessors, and RAPT JNI
callbacks). Missing pieces produce a stable diagnostic and never call
`SDL_main` or create another Activity.
