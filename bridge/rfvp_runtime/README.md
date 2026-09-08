# Experimental embedded rfvp provider

This is an opt-in native provider for FVP HCB games, using the pinned
`packages/rfvp` submodule. It implements `engine_runtime_provider_v1_t`; Godot
presents the native-resolution frame and owns the window and input coordinates.
The default `auto` renderer sends RFVP's scene traversal through AetherKiri's
Godot GPU Bridge when RenderingDevice is available, with a software fallback.
It does not launch the Windows executable or the standalone rfvp application.

## Build

Initialize `packages/rfvp` and install Rust/Cargo with a target matching the C++
build. Enable `AETHERKIRI_ENABLE_RFVP=ON` on the normal application CMake preset.
The default remains OFF. Android, Web and universal macOS builds are rejected;
iOS and macOS must build a single architecture at a time. Native audio needs
the platform audio development libraries (including ALSA on Linux).

For the independent provider tests, without the other application packages:

```sh
cmake -S bridge/rfvp_runtime -B out/rfvp-check -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/rfvp-check
ctest --test-dir out/rfvp-check --output-on-failure
```

`RFVP_CARGO`, `RFVP_RUSTC` and `AETHERKIRI_RFVP_RUST_TARGET` are configurable.
Cargo uses the committed bridge lockfile with `--locked`. This lockfile covers
the small generated workspace, not all upstream console and tool packages.

## Resources and behavior

- Import an extracted game directory containing a valid `.hcb` and its original
  resource packs/subdirectories. The library selects the HCB launch entry.
  Direct HCB selection also works; explicitly select one if several exist.
- Original Japanese scripts default to Shift-JIS. The game's detail page offers
  a persistent script-encoding choice (Shift-JIS, GBK or UTF-8). Choose the
  encoding of the actual HCB, not the desired display language. The developer
  override `AETHERKIRI_RFVP_ENCODING` remains available. Metadata refresh preserves
  manually selected launch files, including the empty auto-detect choice.
  Translation patches must be prepared as a separate raw HCB first; renaming a
  compressed `.pck` to `.hcb` does not convert it. Windows patch DLLs are not run.
- One rfvp session may be open per process, because upstream owns global state.
  Saves and extracted movie caches live beneath the host writable directory,
  isolated by canonical game/script path and encoding, not in the game folder.
  Moving the game or changing its entry/encoding changes that save identity.
- Pointer buttons, cursor, scroll, navigation/function keys and gesture cancel
  are mapped to the upstream input manager. The provider does not advertise
  text-entry/IME support. Pointer positions and deltas use the native frame
  size, independently of the shell's requested presentation surface.
- The selected backends include audio and native WMV/MPEG movies, not MP4.
  Pause freezes host ticks and pauses audio; resume offsets the movie clock.
  Actual audio/video synchronization still requires game-flow testing.
- Script-driven save/load uses VM/state snapshots and CPU thumbnails. Legacy
  standalone native save/load dialogs are not implemented and return an
  explicit error. Exit requests return control to the application host.
- `rfvp_renderer=auto|gpu|cpu` is a pre-open engine option. `auto` is the
  default, `gpu` requires the Godot GPU Bridge, and `cpu` is useful for visual
  comparison. The app-level developer override is
  `AETHERKIRI_RFVP_RENDERER`. Normal GPU presentation stays on-device; a
  readback occurs only when the host explicitly requests RGBA pixels, such as
  for save thumbnails or screenshots.
- New RFVS payloads use a version-2 envelope to preserve active motions, sprite
  and snow animations, and text-reveal coroutine linkage. Version-1 snapshots
  remain readable, but missing playback state in old files cannot be recovered
  retroactively. Create a new save to benefit from complete playback capture.

## Source and license boundary

`cmake/PrepareRfvpSources.cmake` creates a build-tree copy and applies the host
overlay without modifying the pinned submodule. The overlay redirects game,
save and cache roots, resets per-session globals, adds pause/cancel hooks, and
replaces the upstream embedded Microsoft fonts with AetherKiri's OFL runtime
font. Large sound-effect slot buffers are heap-backed so initialization also
fits the host's smaller asynchronous-startup thread stack. The compiled rfvp
crate is a Rust static library behind a private C ABI;
no Rust layout crosses the engine-provider ABI.

The rfvp crate uses full optimization even in the development profile, retaining
debug assertions. Release builds use ThinLTO and one code-generation unit. CPU
rendering skips zero-opacity quads and rasterizes
axis-aligned quads once, avoiding redundant triangle scans and a double-blended
translucent diagonal. Rotated/sheared geometry retains the triangle path.

The rfvp sources and Rust modifications are MPL-2.0. Distributors must retain
the source notices and make the covered sources and these modifications
available under MPL-2.0. The font license is
`apps/godot_app/assets/fonts/OFL.txt`. No commercial game assets belong here.

## Verification scope

The provider test renders upstream's open-source painter, then runs an original
minimal HCB fixture for click/erase, keyboard-driven save/load, cancellation,
pause/resume, single-session isolation and reopening. Fixture bytecode is
generated by the test; there are no commercial assets in the test suite.
The save regression also commits a second slot from the prepared gameplay
snapshot, then loads it: completing a write must release its request without
discarding the pre-menu payload. Another fixture saves during a fade and checks
that the animation reaches its destination after load. Rust regressions cover
V1/V2 round trips, malformed payloads, fixed-buffer lengths, text waiters,
opacity culling, translucent diagonals, sampling/clipping/flipping parity, and
the lazy GPU texture, batching, readback and cleanup contract.
Godot's input mapping regression covers rfvp
at a mismatched presentation size while preserving KiriKiri's surface mapping.
Games open on a native worker thread, matching the application's asynchronous
startup, and then tick, handle input and close on the host thread.
The macOS arm64 Debug app has also been smoke-tested through the normal library
import/launch flow, presenting the painter's 1024x640 frame via Godot/Metal.
A local smoke test with the original Shift-JIS script of
`アストラエアの白き永遠 ver1.1` reached the title and opening story, advanced text,
opened the save/load menus, wrote a thumbnail-bearing slot and resumed gameplay
after loading it. A subsequent normal-library run with its prepared GBK script
verified Chinese dialogue, current-line restoration after load, multiple manual
slots, app restart, automatic text advancement and the following two-character
scene. Original resources and the original script were preserved.
This is still limited gameplay coverage, not a full playthrough or a claim about
audio synchronization, all movies, gallery modes or long-session stability.

The standalone host tests do not require commercial assets. The pinned upstream
crate also contains eight legacy tests whose external parser/save/texture samples
are absent from the checkout; those cannot be counted as passing. The remaining
self-contained upstream/host tests can run independently.

The upstream painter's canvas drawing is not used as a passing input test:
at the pinned revision it observes the click and cursor coordinates but does
not issue the expected tile update. Its title/frame alone does not prove its
application logic works.

These tests are not a full AetherKiri application or commercial-game
compatibility certification. Do not add a game to the verified-games list
until the normal application import/launch and the claimed flows are exercised
and confirmed by the user.
