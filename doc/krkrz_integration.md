# AetherKiri × krkrz_dev integration

[English](krkrz_integration.md) | [简体中文](krkrz_integration.zh-CN.md)

This document is the implementation contract for reusing
[`wamsoft/krkrz_dev`](https://github.com/wamsoft/krkrz_dev) in AetherKiri. The
goal is source-level reuse where the upstream business logic is compatible,
while AetherKiri remains the owner of the runtime ABI, plugin registration,
platform lifecycle, and renderer/audio integration.

The exhaustive 56-plugin/core/external/script/tool audit is in
[`krkrz_full_audit.md`](krkrz_full_audit.md). This document intentionally
describes only the enabled integration contract and its verification steps.

## Pinned source and checkout

The parent repository carries one public gitlink at
`third_party/krkrz_dev`. At this revision the gitlink and the manifest both
point to:

```text
repository: wamsoft/krkrz_dev
revision:   83cc5cc4528bf431d16b5d4949cb11966331e392
```

After a fresh clone:

```bash
git submodule update --init --recursive third_party/krkrz_dev
python3 tools/plugin_manifest_report.py --strict
python3 tools/krkrz_core_audit.py
```

The repository URL intentionally remains the SSH URL used by maintainers. A
public CI job can override it for this command only:

```bash
git -c submodule.third_party/krkrz_dev.url=https://github.com/wamsoft/krkrz_dev.git \
  submodule update --init --recursive --depth 1 third_party/krkrz_dev
```

`plugin_manifest_report.py --strict` also checks all 75 nested submodules below
the checkout. An uninitialized, drifted, conflicted, or locally dirty child
worktree fails the check.

Updating the source is a two-part change: move the parent gitlink, then update
`upstream_revision` in
[`runtime/kirikiri/manifests/plugins.toml`](../runtime/kirikiri/manifests/plugins.toml).
The strict manifest check prevents a stale checkout from being mistaken for
the reviewed revision.

## Integration layers

The upstream standalone CMake files are not included. Aether's
[`cpp/plugins/CMakeLists.txt`](../cpp/plugins/CMakeLists.txt) selects source
files, and the small translation units in
[`cpp/plugins/upstream_bridge`](../cpp/plugins/upstream_bridge) establish the
Aether ABI before including an upstream business translation unit. The adapter
rules are kept in [`krkrz_upstream_adapters.md`](krkrz_upstream_adapters.md),
alongside the rest of the integration documentation.

The adapters currently enabled by default are:

| Module | Reuse mode | Boundary |
| --- | --- | --- |
| `layerExAreaAverage.dll` | upstream-adapted | upstream implementation plus Aether integer/pointer conversion |
| `layerExRaster.dll` | upstream-adapted | upstream raster implementation and shared layer base |
| `layerExLongExposure.dll` | upstream-adapted | upstream implementation under Aether TJS ABI |
| `getSample.dll` | upstream-adapted | upstream sampler plus Aether `enableGetSample` compatibility callback |
| `layerExBTOA.dll` | upstream-adapted | upstream implementation |
| `layerExImage.dll` | upstream-adapted | upstream image implementation plus portable `RGBQUAD` definitions |
| `shrinkCopy.dll` | upstream-adapted | upstream implementation |
| `psdfile.dll` | hybrid | Aether TJS/Layer/Storage wrapper, upstream `psdparse` only |
| `layerExSave.dll` | hybrid | Aether Layer/Storage/threading wrapper with namespaced upstream LodePNG/TLG5 codecs and adapted utility methods; BMP and TJS/octet boundaries remain Aether-owned |
| `clipfile.dll` | hybrid | Pinned-submodule CLIP parser/writer with an Aether `clip://` Storage and Layer/TJS ABI bridge; shares one process-wide SQLite owner (vcpkg-pinned in CI, modern system SQLite for local hosts, or the hash-verified 3.45.1 fallback) |
| `krkr_richtext.dll` | hybrid | Pinned richtext/Minikin layout and rendering sources share Aether's FontService/FreeType, ICU and HarfBuzz owners; the classic `TextRenderBase` path remains available |
| `extNagano.dll` | hybrid | Aether provider registry wrapping ten selected upstream transition algorithms; incompatible algorithms/options automatically use the Aether fallback |
| `KAGParserEx.dll` | hybrid | Aether's single parser implementation with upstream semantics as reference |
| `AlphaMovie.dll` | hybrid | Aether FFmpeg/queue/Godot pipeline; upstream codec code is reference input |
| `resourceRW.dll` | hybrid | Upstream Reader/Writer TJS contract with a bounded `AKRRES01` sidecar on non-Win32 hosts |
| `krkrsteam.dll` | hybrid | Upstream Steam API shape with persistent achievements and `steam://` local cloud storage; SDK-only calls fail closed |

The remaining portable modules (`scriptsEx`, `json`, `csvParser`, `lineParser`,
`saveStruct`, SQLite/VFS, `xp3filter`, `motionplayer`, and the Aether GPU/Live2D
bridges) stay Aether-owned. A single modern SQLite owner (vcpkg-pinned in CI,
a deserialize-capable system provider on developer hosts, or the hash-verified
3.45.1 fallback) serves
the legacy sqlite3 TJS API, XP3 VFS, and CLIP `serialize`/`deserialize` calls;
there is no second database owner. The authoritative machine-readable status
for all modules, including stubs and optional modules, is the manifest.

## KAG/TJS script boundary

KAG3, KAG3_Ham, Krkr2Compat, Sample, and the upstream TJS2 test corpus are
already recursive submodules below `third_party/krkrz_dev/script`. Their
paths, nested gitlink revisions, and entrypoints are recorded as
`[[script_components]]` in the manifest and checked by
`plugin_manifest_report.py --strict`. This keeps one source of truth for
upstream scripts without vendoring a second copy into the runtime.

The product demo remains a deliberate fixture: it contains translated text
and Aether-specific polyfills, so it is not silently replaced by the upstream
KAG3 tree. `Krkr2Compat` and `KAG3_Ham` are reference/fixture inputs rather
than default runtime injection. The TJS2 corpus, issue-226 regressions, and
the KAG entrypoint chain are now executed by Aether's CTest harness directly
from the submodule; this validates script compatibility without copying or
injecting the upstream KAG runtime into the product.

## ABI and ownership rules

1. Do not link upstream `tp_stub.cpp`, `ncbind.cpp`, `v2link.cpp`, standalone
   plugin registries, or upstream `krkrz.cmake`. They define a second runtime
   ABI and can create duplicate module/class symbols.
2. Every adapted translation unit defines its `NCB_MODULE_NAME` explicitly and
   includes `krkrz_aether_compat.hpp` first. ABI differences are isolated in
   that adapter and never patched into the submodule checkout.
3. Aether's `tTJSBinaryStream` is RAII-managed. Upstream code that expects
   `iTJSBinaryStream::Destruct()` must be adapted; calling `Destruct()` on an
   Aether stream is invalid.
4. Keep one owner for each global class or module. In particular, do not link
   upstream `KAGParser.cpp` beside Aether's `cpp/core/base/KAGParser.cpp`, and
   do not import the complete upstream plugin registry.
5. Private AetherInternal hooks are guarded by
   `AETHERKIRI_INTERNAL_KRKR2_PLUGIN`; public builds must remain linkable with
   the compatibility stubs and portable data-pack loaders.

## Core: visual SIMD, sound DSP, and DAP

The upstream core is useful, but it is not a drop-in replacement for Aether's
core. The core contract in the manifest records the exact upstream files and
parity tests.

* **Visual SIMD:** Aether's Highway/function-pointer dispatch in
  `cpp/core/visual/simd` remains the renderer owner for blend, adjust-color and
  color-fill. The parity-safe ColorMap/ColorMap65 base variants and 24-bit
  conversion are consumed through
  `cpp/core/visual/upstream_bridge/VisualSIMDLeavesDispatch.cpp`; the
  universal-transition leaves use the same bridge with an Aether alpha
  normalization wrapper. HDA/additive-alpha color variants and box-blur stay
  on Aether because upstream arithmetic differs at the byte/LSB level. The
  compatible TLG5/TLG6 leaf kernels are consumed through
  `cpp/core/visual/upstream_bridge/TLGSIMD.cpp`, and SSE2/AVX2 resampling leaves
  are selected after scalar/Highway initialization by the CPU/OS probe.
  Nearest-neighbour, unsupported filters, bitmap owners without CPU scanlines,
  arm64 and universal builds retain Aether's scalar/render fallback, so no
  second `tvpgl` symbol table or dispatch registry is introduced.
* **Sound DSP:** Aether's `cpp/core/sound` and `cpp/core/utils` own the public
  sound ABI and lifecycle. The scalar MathAlgorithms/RealFFT leaves,
  WaveSegmentQueue, WaveLoopManager, and x86/NEON MathAlgorithms/xmmlib leaves
  are consumed through source bridges from the pinned checkout. `PhaseVocoderDSP`
  dispatches only the compatible window/FFT methods and always retains a scalar
  fallback; host audio, allocator state, and the Aether-only `DesiredFormat`
  lifecycle remain Aether-owned.
* **Portable core leaves:** `visual/gl/WeightFunctor.cpp`,
  `utils/Random.cpp`, `utils/ClipboardIntf.cpp`, `utils/MiscUtility.cpp`,
  `utils/md5.c`, `base/PluginIntf.cpp`, and `tjs2/tjsException.cpp` are also
  consumed through small Aether-header bridges. They contain no host lifecycle
  state; platform storage, message registration, and public ABI remain owned by
  Aether.
* **Fonts:** `visual/FontVariations.cpp` is consumed through a source bridge.
  Aether's shared FontService/FreeType owner handles clamped variable axes, TTC
  face indices, bitmap/color glyphs, UTF-16 surrogate/codepoint decoding,
  VS15/VS16 presentation hints, and the fallback chain. It uses a bounded,
  immutable FontStream cache keyed by storage path and face index. Desktop
  builds enable HarfBuzz+FriBidi shaping when detected and otherwise retain the
  compatible scalar path.
* **DAP debugger:** Upstream `tjsDebuggerCore`, hook/symbol files, and
  `DAPServer` are linked on desktop through Aether ABI bridges. VM hooks,
  thread startup, and event-loop ownership remain Aether's. The server is inert
  until `-dap=<port>` is supplied; `src/core/tests/dap_smoke.py` is the protocol
  acceptance test and still requires a real host session.
* **REPL:** Aether exposes the safe subset of krkrz's file REPL as
  `-replfile=<directory>`. A main-thread adapter evaluates TJS expressions or
  statements and atomically exchanges UTF-8 `cmd`/`resp` JSON files. The
  upstream console/icline/socket frontends are reference-only because their
  stream and thread ABI does not match Aether.

This distinction is deliberate: “reusable algorithm” does not mean “safe to
link as a second core.”
The full file-by-file decisions for similar-but-incompatible units such as
`VelocityTracker`, `TickCount`, `CharacterSet`, `BinaryStream`, and the bitmap
geometry code are recorded in the [Chinese full audit matrix](krkrz_full_audit.zh-CN.md).

## Optional plugins and stubs

`clipfile` is now a default hybrid integration on desktop and Android. Its
portable `clipparse` C++ sources are compiled directly from the pinned
submodule and `cpp/plugins/upstream_bridge/clipfile_compat.hpp` maps the
upstream `iTJSBinaryStream::Destruct()` contract to Aether's RAII stream. The
`CLIP` class exposes `.clip` metadata, tiled/region reads, compositing, preview,
and `clip://` virtual Storage; `CLIPWriter` exposes attributes, pixels, layer
editing, resize, thumbnail invalidation, and validation. Web/emscripten keeps
the adapter disabled because its Storage host is not portable, not because of
a runtime switch.

`AETHER_USE_KRKRZ_OPTIONAL_PLUGINS=ON` validates the remaining SDK-heavy
`krkreffekseer` and `krkrthreepp` source trees. `krkr_richtext` is no longer
optional on supported native hosts: its pinned Minikin/richtext sources are
compiled into a hybrid target when FreeType, HarfBuzz and ICU are available.
The adapter is skipped on iOS/web where the dependency and host contracts are
different; the existing Aether text renderer remains the compatibility path.
`layerExVector.dll` is now integrated through
[`cpp/plugins/krkrzLayerExVectorCompat.cpp`](../cpp/plugins/krkrzLayerExVectorCompat.cpp):
it loads Aether's single `layerExDraw` renderer and adapts `GdiPlus.loadFont`,
font aliases (including desktop native font paths), the
`fontFamily`/`fontSize`/`italic`/`letterSpacing`/`lineSpacing` properties, and
`drawStringArea` without linking ThorVG or a second global class registry.
`lineSpacing` follows krkrz's writable non-negative scale semantics in the
vector compatibility path; the Aether renderer's native pixel metric remains
available through a private channel for layout calculations. The two remaining
SDK-heavy plugins remain source-validated only until each has its own adapter,
dependency policy, and runtime test.

`layerExSave.dll` also exposes the complete upstream utility surface:
`oozeColor`, `getFingerPrintValue`, `getShrinkVectorOctet`,
`Math.octetVectorSum`, and `saveProvinceImage`. Province PNGs use the pinned
LodePNG implementation and upstream 256-entry palette; Aether continues to own
Layer buffers, virtual Storage, and TJS objects. Zero-sized shrink-vector
requests are rejected at the adapter boundary instead of reaching upstream's
division-by-zero path.

`resourceRW.dll` keeps the upstream resource type/name/language behavior. When
the host cannot edit a PE image, `ResourceWriter` writes `<target>.aetherres`
using the deterministic, size-bounded container in
`cpp/plugins/portableResourceBundle.cpp`; `ResourceReader` enumerates and reads
the same sidecar, including UTF-8/UTF-16 text and octets. The original target is
never overwritten. ICO/CUR, grouped icons, and `VS_VERSION_INFO` have bounded
portable parser/serializer objects; resource constants are installed only when
the game/native host has not already defined them. `windowEx` retains native
Win32 cursor/DPI/class APIs while providing logical cursor clipping, top-level
window lookup, and keyboard mapping on portable hosts. SDL-backed gamepad
objects and upstream constants never replace an existing game/native
implementation. `krkrsteam.dll` uses the same Storage boundary for a local
state file and registers `steam://<filename>` as a writable cloud namespace.
Achievements, cloud metadata and language work offline; screenshots,
broadcast, DLC and account ownership return `false` because no Steamworks
identity is available.

`sigcheck.dll` is a hybrid adapter rather than a name-only stub. It parses the
upstream embedded/sidecar signature format, streams the signed bytes with
bounded memory, and verifies SHA-256/RSA-PSS through the process OpenSSL
provider when available. A build without a crypto provider returns an explicit
error (fail-closed); it never reports a false success. `win32ole` remains a
host-owned COM/ActiveX boundary and is still fail-closed.

Compatibility-only registrations live under
[`cpp/plugins/stubs`](../cpp/plugins/stubs); their policy is documented in
[`krkrz_plugin_stubs.md`](krkrz_plugin_stubs.md). A stub preserves a module
name or script shape but is not feature-complete native support. Empty module
callbacks are not retained when an Aether adapter owns the module. Only
`win32ole` remains an explicit unimplemented host boundary; SDK-heavy modules
continue to expose capability/error state instead of claiming success.

## Build and verification

The GitHub Actions `Build` workflow uses the complete compatibility profile by
default. It forwards `AETHERKIRI_ENABLE_ONSCRIPTER=ON`, desktop
`AETHERKIRI_ENABLE_DAP=ON`, `AETHERKIRI_ENABLE_FONT_SHAPING=ON`,
`AETHERKIRI_ENABLE_CLIPFILE=ON`, `AETHER_USE_KRKRZ_OPTIONAL_PLUGINS=ON`, and
`AETHER_BUILD_KRKRZ_CORE_PARITY=ON` to every platform configure. The optional
plugin setting validates only the remaining SDK-heavy source trees; CLIP is
compiled by its own default-on option, and parity adds isolated visual/sound
tests to native CI. None of these settings link an upstream registry or
replace an Aether implementation. Trusted pushes and release builds also
initialize `AetherInternal`; fork and Dependabot pull requests cannot receive
the private SSH key and therefore remain public fallback builds.

Before any build, CI resolves `wamsoft/krkrz_dev` `master` over HTTPS and checks
that the parent gitlink and `runtime/kirikiri/manifests/plugins.toml` still
refer to that exact latest reviewed commit. A stale pin fails with an explicit
update instruction instead of silently compiling an older or unreviewed tree.

The complete product build consumes the seven leaf adapters, the layerExSave
codec bridge, the CLIP parser/writer, and the selected extNagano algorithms,
then extends the same targets with AetherInternal compatibility providers. For
a macOS Debug build:

```bash
git submodule update --init --recursive third_party/krkrz_dev
git submodule update --init --recursive packages/AetherInternal
AETHERKIRI_ENABLE_INTERNAL=ON ./build.sh macos debug --jobs=2
```

The configure output must contain
`AetherInternal E-mote/Live2D package: enabled`. `build.sh` configures with a
fresh CMake cache, so a previously cached `AETHERKIRI_ENABLE_INTERNAL=OFF`
cannot silently turn the next product build into a public-only artifact. A
build that reports the package as explicitly disabled or unavailable is valid
for public fallback testing, but must not be handed off as a full game
compatibility build.

To validate the public fallback and the krkrz adapters in isolation, use a
separate build directory:

```bash
cmake -S . -B out/krkrz-public-debug \
  -DAETHERKIRI_ENABLE_INTERNAL=OFF \
  -DENABLE_TESTS=ON
cmake --build out/krkrz-public-debug --target krkr2plugin --parallel
ctest --test-dir out/krkrz-public-debug --output-on-failure
```

The seven leaf adapters and the CLIP adapter are mandatory source-level integrations: configure
validates the initialized pinned submodule and always compiles the bridge
translation units. The old `AETHER_USE_KRKRZ_LEAF_PLUGINS` source-selection
option and its historical local implementations have been removed, so there is
only one implementation path and no stale cache value can select a second copy.
This is not a runtime/game switch: the product has one plugin registry, and
hybrid providers select their Aether fallback automatically when an upstream
operation cannot be used. The normal integration target also carries the
layerExSave codec bridge, the shared SQLite owner, and CLIP `clip://` Storage.
Both public fallback and private AetherInternal
configurations are supported; the latter extends targets instead of replacing
them.

The minimum pre-submit checks are:

```bash
python3 tools/plugin_manifest_report.py --strict
python3 tools/krkrz_core_audit.py
python3 tools/plugin_gap_audit.py
cmake --build <build-dir> --target krkr2plugin --parallel
ctest --test-dir <build-dir> --output-on-failure
```

The current pinned checkout has passed the complete gate: 264/264 CTest
cases, 227/227 direct Catch2 plugin cases (3,124 assertions), visual parity
294/294, and sound parity 28/28 with zero failures.  The all-on macOS arm64
Debug product was rebuilt and deep-code-signature verified on 2026-08-30.

For a focused review of the upstream core algorithms, enable the isolated
parity executables. They compile only the upstream visual/sound test sources
and never link `krkr2core`:

```bash
cmake -S . -B out/krkrz-parity \
  -DAETHER_BUILD_KRKRZ_CORE_PARITY=ON \
  -DAETHERKIRI_ENABLE_INTERNAL=OFF \
  -DENABLE_TESTS=ON
cmake --build out/krkrz-parity \
  --target aether_krkrz_visual_parity aether_krkrz_sound_parity --parallel
ctest --test-dir out/krkrz-parity \
  -R 'aether_krkrz_(visual|sound)_parity' --output-on-failure
```

These parity targets are the safe first consumer of upstream core code. They
make it possible to reuse or port one well-understood method without claiming
that the whole upstream core can replace Aether. A future production adapter
must keep the same symbol isolation and add an engine-level behavior test before
changing a core component's status.

For agent-driven debugging, create a private directory and start the product
with `-replfile=/absolute/path/to/repl`. Write UTF-8 TJS into `cmd.tmp`, rename
it to `cmd`, wait for `resp`, then delete `resp` before sending the next
command. Responses contain `protocol`, `ok`, `kind`, `result`, and `error`;
commands larger than 2 MiB and invalid UTF-8 are rejected without evaluating
partial input.

The submodule's source and license notices remain in the submodule. Do not copy
its source into `cpp/plugins` or publish private AetherInternal sources as part
of this integration.
