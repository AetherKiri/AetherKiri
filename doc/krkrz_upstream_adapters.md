# krkrz source adapters

[English](krkrz_upstream_adapters.md) | [简体中文](krkrz_upstream_adapters.zh-CN.md)

The translation units in `cpp/plugins/upstream_bridge` are the only place
where selected `krkrz_dev` plugin sources enter the Aether build. Each adapter
establishes the Aether `ncbind`/TJS ABI, names the module explicitly, and then
includes the business translation unit from the pinned
`third_party/krkrz_dev` checkout. A few portable upstream implementation files
(the layerExSave codecs and selected extNagano algorithms) are also compiled
directly, but they are always reached through an Aether-owned wrapper or
detached provider; the upstream registry and lifecycle are never imported.
The seven direct leaf plugins have no local fallback copy in the parent
repository; the pinned submodule plus these adapters is their single source
path.

Keep these rules when adding an adapter:

* Do not include upstream `tp_stub.cpp`, `ncbind.cpp`, `v2link.cpp`, plugin
  registries, or upstream CMake files. Aether owns those runtime facilities.
* Resolve an ABI difference in the small adapter, with a comment and a test;
  do not patch the submodule working tree.
* Preserve Aether's ownership and threading rules. In particular,
  `tTJSBinaryStream` is RAII-managed and must not receive an upstream
  `iTJSBinaryStream::Destruct()` call.
* Add the source and adapter to
  `runtime/kirikiri/manifests/plugins.toml` and keep the manifest revision
  equal to the parent gitlink.

The current direct leaf adapters are the low-risk plugins:
`layerExAreaAverage`, `layerExRaster`, `layerExLongExposure`, `getSample`,
`layerExBTOA`, `layerExImage`, and `shrinkCopy`. Hybrid source reuse additionally
includes the namespaced LodePNG/TLG5 methods used by `layerExSave`. Its Aether
wrapper also exposes the upstream `oozeColor`, fingerprint, shrink-vector,
octet-vector, and province-palette methods without importing the upstream
Layer/Storage ABI. Hybrid reuse additionally includes the
`blurfade`, `book`, `flutter`, `honeyturn`, `morphing`, `multiripple`, `rgbfade`,
`scanline`, `spin`, and `zoomfade` transition algorithms. The extNagano
`3duniversal` and `imagewipe` providers remain Aether fallbacks because their
texture-provider ABI does not match; option conversion failures also fall back
automatically. More invasive plugins remain hybrid or Aether-owned until their
runtime contract is proven.

`layerExVector.dll` is a hybrid adapter that does not copy or link ThorVG:
`cpp/plugins/krkrzLayerExVectorCompat.cpp` loads Aether's `layerExDraw` first and
adds `GdiPlus.loadFont` (including desktop native font paths), font aliases, the
`fontFamily`/`fontSize`/`italic`/`letterSpacing`/`lineSpacing` properties, and
`drawStringArea` to the same TJS classes. The vector-facing `lineSpacing` setter
uses krkrz's writable non-negative scale; the adapter reads Aether's native
pixel metric through a private sibling property before applying that scale.
Vector games and existing LayerExDraw games therefore share one renderer and
one face-aware font stream registry; `unloadFont` keeps registered streams alive
for the process lifetime so existing Font objects cannot dangle.

This is an implementation-level compatibility rule, not a runtime switch:
there is one Aether plugin registry, and a game sees the same module/provider
names regardless of whether an upstream operation accepts its inputs.

Core adapters follow the same single-owner rule. The pinned checkout supplies
the implementation bytes, while Aether supplies the public ABI and lifecycle:

| Area | Adapter | What remains Aether-owned |
| --- | --- | --- |
| Sound DSP/loop/FFT | `cpp/core/sound/upstream_bridge` and `cpp/core/utils/upstream_bridge` | sound host, allocator, `DesiredFormat`, scalar fallback and dispatch policy |
| Visual SIMD leaves | `cpp/core/visual/upstream_bridge/VisualSIMDLeavesDispatch.cpp` and `UnivTransSSE2.cpp` | Highway blend/adjust/color-fill owner; Aether alpha semantics, HDA/additive-alpha ColorMap variants, box-blur, bitmap/thread ABI and scalar fallback |
| TLG/resampling | `cpp/core/visual/upstream_bridge/TLGSIMD.cpp` and `ResampleImage{SSE2,AVX2}.cpp` | `LoadTLG` format router, virtual streams, metadata and nearest/unsupported-filter/no-scanline fallback |
| Resampling | `cpp/core/visual/upstream_bridge/ResampleImage{SSE2,AVX2}.cpp` plus `ResampleImageDispatch.cpp` | bitmap/thread ABI, CPU/OS selection, and nearest/unsupported-filter/no-scanline/universal fallback |
| Variable fonts | `cpp/core/visual/upstream_bridge/FontVariations.cpp` plus `FontStream.cpp` | FreeType faces, fallback order, bounded cache and FontSystem registry |
| DAP | `cpp/core/tjs2/upstream_bridge`, `cpp/core/utils/upstream_bridge/DAPServer.cpp` | VM hook installation, Aether thread ABI and main event loop |
| File REPL | `cpp/core/utils/ReplFileChannel.cpp` | RAII stream ABI and main-thread evaluation; upstream console/socket code is reference-only |
| CLIP | `cpp/plugins/upstream_bridge/clipfile_*.cpp` plus `clipfile_compat.hpp` | `clip://` Storage, Layer/TJS wrapper, the SQLite owner, and platform lifecycle |
| Richtext/Minikin | `cpp/plugins/upstream_bridge/krkrz_richtext_*.cpp` | Aether FontService/FreeType owner, module registration, stream lifetime, and renderer upload boundary |

The file REPL accepts `-replfile=<directory>` and exchanges UTF-8 `cmd` and
`resp` JSON files. It is intentionally a development/diagnostic channel, not
a game-facing compatibility switch.

`clipfile` is now a default hybrid adapter. Three bridge translation units
include only the pinned submodule's `clipclass.cpp`, `clipwriter.cpp`, and
`main.cpp`; the five portable `clipparse` C++ files are compiled as an
independent static target, and one modern SQLite owner is shared process-wide
(the vcpkg-pinned package in CI, a deserialize-capable system provider on a
developer host, or the hash-verified 3.45.1 fallback in a standalone
checkout). `clipfile_compat.hpp` primes Aether's `tp_stub` and event
interfaces before introducing a local macro scope that maps
`iTJSBinaryStream` to a `Destruct()`-capable wrapper and routes
`TVPCreateStream` ownership back to RAII. It also supplies the non-Windows
`S_OK`, BMP-header, and reverse-string compatibility names. CLIP lazy loading,
region reads, writing, and `clip://` media can therefore be reused without a
second upstream registry. Web/emscripten omits the adapter because its Storage
host differs. `krkr_richtext` is now a hybrid adapter on desktop and Android:
its pinned richtext/Minikin sources share Aether's FontService, FreeType,
HarfBuzz, and ICU owners, while the legacy `TextRenderBase` remains available
for games that use the classic text API. iOS and web keep the source contract
but skip the dependency-heavy target. Effekseer and threepp remain
source-validated optional projects because their OpenGL/VRM host lifecycles do
not match Aether's renderer yet.
