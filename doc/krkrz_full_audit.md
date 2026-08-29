# AetherKiri × krkrz_dev full audit matrix

This document records the source, build, and ABI audit of
`third_party/krkrz_dev` in AetherKiri.  It is the English counterpart of
[`krkrz_full_audit.zh-CN.md`](krkrz_full_audit.zh-CN.md).  The shorter
[`krkrz_integration.md`](krkrz_integration.md) describes the integration
contract that is already enabled; this document also covers code that remains
reference-only or host-specific.

## Scope and conclusion

| Scope | Count | Treatment |
| --- | ---: | --- |
| `src/plugins` | 56 directories | Direct, hybrid, Aether-owned, host-compat, optional, or ABI infrastructure |
| `src/core` tree | 2,490 files | common, generic, sdl3, win32, external, tests, assets, and build metadata |
| `src/core/common` | 487 files | base, environ, extension, glad, msg, sound, tjs2, utils, visual |
| core platform layers | 213 files | generic 50, sdl3 41, win32 122 |
| core external | 1,579 files | five nested groups: elements, glyphware, movie-player, pl_mpeg, sound-codecs |
| core data/tests/metadata | 211 files | data 99, resource 14, tests 9, docs/licenses/build files |
| script submodules | 5 | KAG3, KAG3_Ham, Krkr2Compat, Sample, test |
| tool submodules | 2 | tjs2doc and the Win32 debugger |

The conclusion is not to link the entire upstream core.  AetherKiri remains
the sole owner of the runtime ABI, plugin registry, Storage, Layer/renderer,
sound host, and TJS VM.  Upstream code is consumed through the existing pinned
submodule only where an algorithm or protocol can cross that boundary safely;
no upstream source is copied into this repository.

## Plugin classification

### Compiled directly from the submodule (7)

Small translation units in `cpp/plugins/upstream_bridge` establish the Aether
ABI and include the upstream business unit.  There is no local business copy:

`layerExAreaAverage`, `layerExRaster`, `layerExLongExposure`, `getSample`,
`layerExBTOA`, `layerExImage`, and `shrinkCopy`.

### Hybrid reuse (8)

| Plugin | Aether-owned boundary | Reused upstream part |
| --- | --- | --- |
| `AlphaMovie` | FFmpeg, queues, texture/audio routing, Godot presentation | codec behavior reference |
| `KAGParserEx` | the single parser, tag metadata, compiled-scene path | syntax/compatibility reference |
| `layerExSave` | Layer/Storage/TJS/octet/thread boundary | namespaced LodePNG and TLG5 codec, plus parity utilities |
| `clipfile` | `clip://` Storage, Layer/TJS wrapper, one SQLite owner | pinned CLIP parser/writer and tiled decode |
| `krkr_richtext` | FontService/FreeType, ICU/HarfBuzz ownership, lifetime | Minikin layout, glyph rendering, and TJS surface |
| `extNagano` | provider registry and fallback | ten self-contained transition algorithms |
| `layerExVector` | LayerExDraw, font registration and lifetime | vector API semantics |
| `psdfile` | TJS/Layer/Storage wrapper | nested `psdparse` parser |

Incompatible `extNagano` algorithms/options automatically use the Aether
provider.  This is an implementation fallback, not a user-facing runtime
switch.

### Aether remains the sole implementation (16)

`addFont`, `binaryStream`, `csvParser`, `expat`, `extrans`, `fstat`, `json`,
`layerEx`, `layerExDraw`, `lineParser`, `memfile`, `menu`, `minizip`,
`saveStruct`, `scriptsEx`, and `varfile` remain Aether-owned.  Upstream is used
only for API, edge-case, and test reference.  A second `layerExDraw`, minizip,
KAG parser, or scriptsEx registration would create duplicate global owners.

### Host and compatibility layer (20)

`fpslimit`, `gamepad`, `httprequest`, `httpserv`, `krkrgles`, `krkrlive2d`,
`messenger`, `msgreceiver`, `process`, `resourceRW`, `shellExecute`, `sigcheck`,
`stdio`, `steam`, `systemEx`, `tftSave`, `win32dialog`, `win32ole`, `windowEx`,
and `windowExProgress` depend on Win32, SDL, OpenGL, Steamworks, Cubism, or
Aether host lifetime.  They use an Aether bridge, private provider, portable
compatibility surface, or an explicit stub.  `fpslimit` is a compatibility
no-op; `tftSave` keeps its script contract; the real Live2D provider is in
the private AetherInternal package.

### Optional product features (2)

`krkreffekseer` and `krkrthreepp` remain source-validated optional features.
They require separate Effekseer/OpenGL and threepp/VRM host contracts.  They
are not linked until those dependencies and a runtime test are available.

### ABI infrastructure (3)

`ncbind`, `simplebinder`, and `tp_stub` are reference infrastructure only.
Do not import their binder, `v2link`, `tp_stub.cpp`, independent registry, or
standalone CMake: those define a second calling convention or registration
table.  The six classifications total `7 + 8 + 16 + 20 + 2 + 3 = 56`, and
the strict manifest check verifies exact directory coverage.

## Core audit

| Common area | Policy |
| --- | --- |
| `base` (49 files) | Aether owns Storage/Stream/Archive; BinaryStream, StorageCache, and XP3 checks are absorbed method-by-method |
| `environ` (7) | Host window/input/lifetime differs; reference and adapter only |
| `extension` (2) | Interface ideas are reference; Aether owns the ABI |
| `glad` (6) | Upstream OpenGL loader is not used by the Godot/SDL2 host |
| `msg` (11) | Adapted to Aether message lifetime |
| `sound` (37) | MathAlgorithms, RealFFT, WaveSegmentQueue, WaveLoopManager and x86/NEON leaves use source bridges; host/allocator/lifetime stay Aether-owned |
| `tjs2` (101) | VM/parser/Variant/Dictionary/ABI are not replaced; debugger core/hook/symbols are bridged |
| `utils` (62) | Random, clipboard, utility, md5, RealFFT and DAP transport use bridges; REPL file channel is an Aether adapter |
| `visual` (212) | Aether owns bitmap/loader/renderer/font; TLG, Resample, ColorMap/24-bit conversion and transition leaves are adapted |

The following similar files were deliberately not bridged as whole files:

| Upstream file | Reason |
| --- | --- |
| `common/environ/TouchPoint.cpp` | only constants and not part of Aether's active platform CMake |
| `common/utils/VelocityTracker.{h,cpp}` | public tick width differs (`uint32` vs `uint64`) |
| `common/utils/TickCount.cpp` | overflow thread and timing lifetime differ |
| `common/base/CharacterSet.cpp` | surrogate/codepoint behavior is intentionally different |
| `common/base/BinaryStream.cpp` | upstream `Destruct()` contract conflicts with Aether RAII stream |
| `common/visual/ComplexRect.cpp`, `LayerBitmapIntf.cpp` | bitmap hierarchy and renderer state differ materially |
| `common/tjs2/tjsInterface.cpp` | empty translation unit |

All generic, SDL3, and Win32 platform files remain platform reference.  GDI,
D3D, DirectInput, PE resources, and COM cannot be linked into macOS,
Android, or Web targets.

External `elements`/ThorVG, glyphware, movie-player/pl_mpeg, and sound-codecs
are reference inputs or allocator headers, not a second renderer/audio host.
KAG scripts and the debugger/tjs2doc tools remain recursive fixtures/tools and
are not copied into the runtime.

## Resource and media entry points

* **Images:** Aether `GraphicsLoaderIntf` remains the single route for BMP,
  PNG, JPEG, TLG, BPG, WEBP, JXR, PVRv3 and AMV, including virtual Storage,
  cache, and asynchronous lifetime.  Upstream `SimpleImageLoad` is only a
  small PNG/JPEG helper.
* **TLG:** Aether keeps TLGmux/TLGref/TLGqoi, QHDR/metadata, LZ4 bands,
  arena/mmap, virtual streams, and diagnostics.  Upstream decompression,
  color-composition, and SIMD leaves are reused; the complete upstream
  `LoadTLG.cpp` is not substituted.
* **Storage:** XP3, Cx, ZIP/7z/TAR, libarchive, unrar, zstd, minizip, mmap,
  prefetch, and media registration remain one Aether registry.  Upstream
  BinaryStream/StorageCache logic is absorbed only where the lifecycle fits.
* **Sound:** FFmpeg/Opus/RIFF/Vorbis decoding and host audio remain Aether;
  scalar/SIMD math, FFT, queue, loop, and parity-tested DSP leaves come from
  the pinned source bridges.
* **Fonts:** FontStream, variable axes, TTC face index, color glyphs,
  surrogate/codepoint, fallback, and optional HarfBuzz/FriBidi shaping share
  one Aether FontService and bounded cache.
* **Debugger/REPL:** DAP VM hooks, socket transport, and event loop are
  bridged on desktop.  `-replfile=<directory>` is the safe main-thread file
  adapter; upstream console/icline/socket front ends remain reference-only.

`layerExSave` now also exposes the upstream-compatible `oozeColor`,
`getFingerPrintValue`, `getShrinkVectorOctet`, `octetVectorSum`, and
`saveProvinceImage` methods.  The codec and fixed 256-entry province palette
are encoded through the pinned LodePNG source while Layer/Storage ownership
stays in Aether.

`resourceRW` provides bounded ICO/CUR/group-icon/version-resource objects and
an `AKRRES01` sidecar on non-Win32 hosts.  `windowEx` supplies logical cursor
clipping, window enumeration, class values, cursor loading, virtual-key
mapping, and DPI-context forwarding, with the native Win32 path retained.
Gamepad constants and SDL-backed devices are installed only when the matching
global is absent, so a real game/native implementation remains the owner.

## Ownership and ABI rule

Aether's `tTJSBinaryStream` is RAII-managed while upstream code expects an
`iTJSBinaryStream::Destruct()` protocol.  Bitmap, Storage media, locks, and
plugin registration have the same kind of boundary.  Every upstream leaf
therefore passes through a bridge, namespace, or explicit wrapper.  There is
exactly one owner for each global class, module, and registry; only algorithms
cross the boundary.

## Current status and remaining validation

Implemented in this checkout:

1. Seven direct submodule adapters, CLIP and richtext hybrid adapters, and 30
   core source/leaf bridges (sound, FFT/loop, TLG, Resample, visual leaves,
   fonts, DAP, TJS/utilities);
2. Eight hybrid plugin boundaries plus portable HTTP, registry, resource,
   gamepad, window, pre-rendered-font, and layer-effect compatibility paths;
3. Isolated visual/sound parity targets and contract tests for fonts, TLG,
   debugger, REPL, CLIP, resource objects, and gamepad;
4. Recursive submodule/pin checks (75 nested submodules) and exact manifest
   coverage of all 56 plugin directories;
5. 84 KiriKiri2 compatibility reference items with zero missing modules.

Remaining items are validation or genuinely host-owned contracts, not hidden
duplicate implementations:

1. The macOS arm64 Debug product and the isolated compatibility test build link
   successfully.  The isolated CTest run is 264/264, and the direct Catch2
   plugin run is 227/227 (3,124 assertions).  Visual parity is 294/294 and
   sound parity is 28/28 (fail=0).  Coverage includes malformed TLG5/TLG6
   preflight and bounds, a compressed krkrz TLG5 round trip, the BGRA↔RGBA
   adapter and x86 SSE2 composition wrapper, plus resourceRW, FontService,
   DAP/REPL, CLIP, gamepad, and layer-effect contracts.  The all-on macOS
   Debug product was built and codesigned on 2026-08-30.  Continue real
   host/game regression samples for resourceRW, Steam, TLG/DSP,
   WaveLoopManager, FontStream, DAP, `-replfile`, and CLIP; these are
   interactive coverage gaps, not unresolved build or symbol gaps.
2. Keep `win32ole`, DirectShow/AVI, SWF, and Steamworks
   screenshot/DLC/live/account calls fail-closed until their platform
   contracts are available. `sigcheck` now has a portable OpenSSL-backed
   verifier and only fails when no crypto provider is present. Expose
   capability/error state rather than claiming success for the remaining
   host-only calls.
3. Evaluate Effekseer and threepp only with their real SDK and host lifetime.
4. Repeat the ABI, symbol, resource-entry, and parity audit for every upstream
   pin update.

## Reproducible gates

```bash
python3 tools/plugin_manifest_report.py --strict
python3 tools/krkrz_core_audit.py
python3 tools/plugin_gap_audit.py
cmake --build <build-dir> --target krkr2plugin --parallel
```

With `ENABLE_TESTS=ON`, the first two audits are also registered as
`plugin_manifest_contract` and `krkrz_core_contract`.  A dirty, uninitialized,
drifted, or conflicting nested submodule fails the gate instead of silently
using an unreviewed source tree.
