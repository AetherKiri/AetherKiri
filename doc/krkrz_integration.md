# AetherKiri × krkrz_dev integration

[English](krkrz_integration.md) | [简体中文](krkrz_integration.zh-CN.md)

This document is the implementation contract for reusing
[`wamsoft/krkrz_dev`](https://github.com/wamsoft/krkrz_dev) in AetherKiri. The
goal is source-level reuse where the upstream business logic is compatible,
while AetherKiri remains the owner of the runtime ABI, plugin registration,
platform lifecycle, and renderer/audio integration.

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
```

The repository URL intentionally remains the SSH URL used by maintainers. A
public CI job can override it for this command only:

```bash
git -c submodule.third_party/krkrz_dev.url=https://github.com/wamsoft/krkrz_dev.git \
  submodule update --init --recursive --depth 1 third_party/krkrz_dev
```

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
| `layerExSave.dll` | hybrid | Aether Layer/Storage/threading wrapper with namespaced upstream LodePNG and TLG5 codec methods; BMP and TJS/octet boundaries remain Aether-owned |
| `extNagano.dll` | hybrid | Aether provider registry wrapping ten selected upstream transition algorithms; incompatible algorithms/options automatically use the Aether fallback |
| `KAGParserEx.dll` | hybrid | Aether's single parser implementation with upstream semantics as reference |
| `AlphaMovie.dll` | hybrid | Aether FFmpeg/queue/Godot pipeline; upstream codec code is reference input |

The remaining portable modules (`scriptsEx`, `json`, `csvParser`, `lineParser`,
`saveStruct`, SQLite/VFS, `xp3filter`, `motionplayer`, and the Aether GPU/Live2D
bridges) stay Aether-owned. The authoritative machine-readable status for all
modules, including stubs and optional modules, is the manifest.

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
  `cpp/core/visual/simd` remains linked. Upstream SSE2/AVX2/NEON files are
  compiled by the isolated parity target and serve as method-level reference;
  importing them wholesale would duplicate `tvpgl` symbols and CPU dispatch
  state. A future individual kernel can be adopted only after namespacing and
  an engine-level image test.
* **Sound DSP:** Aether's `cpp/core/sound` and `cpp/core/utils` own the public
  sound ABI and default implementation. Upstream `MathAlgorithms`, `RealFFT`,
  and phase-vocoder SIMD code are currently parity/reference inputs. Individual
  methods may be adopted later behind renamed or namespaced symbols and the
  upstream relative/absolute-tolerance parity test; the complete upstream
  sound core is not linked as a second implementation.
* **DAP debugger:** Upstream `tjsDebuggerCore`, hook/symbol files, and
  `DAPServer` are recorded as `optional`. They require an adapter for Aether's
  VM hooks, thread lifecycle, socket ownership, and host event loop before they
  can be linked. The upstream `dap_smoke.py` remains a future acceptance test;
  it is not evidence that Aether currently exposes DAP.

This distinction is deliberate: “reusable algorithm” does not mean “safe to
link as a second core.”

## Optional plugins and stubs

`AETHER_USE_KRKRZ_OPTIONAL_PLUGINS=ON` validates that the pinned source for
`layerExVector`, `krkr_richtext`, `krkreffekseer`, and `krkrthreepp` is present.
`layerExVector.dll` is now integrated through
[`cpp/plugins/krkrzLayerExVectorCompat.cpp`](../cpp/plugins/krkrzLayerExVectorCompat.cpp):
it loads Aether's single `layerExDraw` renderer and adapts `GdiPlus.loadFont`,
font aliases (including desktop native font paths), the
`fontFamily`/`fontSize`/`italic`/`letterSpacing`/`lineSpacing` properties, and
`drawStringArea` without linking ThorVG or a second global class registry.
`lineSpacing` follows krkrz's writable non-negative scale semantics in the
vector compatibility path; the Aether renderer's native pixel metric remains
available through a private channel for layout calculations. The other three
SDK-heavy plugins remain source-validated only until each has its own adapter,
dependency policy, and runtime test.

Compatibility-only registrations live under
[`cpp/plugins/stubs`](../cpp/plugins/stubs); their policy is documented in
[`krkrz_plugin_stubs.md`](krkrz_plugin_stubs.md). A stub preserves a module
name or script shape but is not feature-complete native support. `sigcheck` is
kept separate from the upstream implementation until its platform/security
contract is reviewed.

## Build and verification

The GitHub Actions `Build` workflow uses the complete compatibility profile by
default. It forwards `AETHERKIRI_ENABLE_ONSCRIPTER=ON`,
`AETHER_USE_KRKRZ_OPTIONAL_PLUGINS=ON`, and
`AETHER_BUILD_KRKRZ_CORE_PARITY=ON` to every platform configure. The optional
plugin setting validates all selected upstream source trees; the parity setting
adds the isolated visual/sound tests to the native CI test configure. It does
not link an upstream registry or replace an Aether implementation. Trusted
pushes and release builds also initialize `AetherInternal`; fork and Dependabot
pull requests cannot receive the private SSH key and therefore remain public
fallback builds.

Before any build, CI resolves `wamsoft/krkrz_dev` `master` over HTTPS and checks
that the parent gitlink and `runtime/kirikiri/manifests/plugins.toml` still
refer to that exact latest reviewed commit. A stale pin fails with an explicit
update instruction instead of silently compiling an older or unreviewed tree.

The complete product build consumes the seven leaf adapters, the layerExSave
codec bridge, and the selected extNagano algorithms, then extends the same
targets with AetherInternal compatibility providers. For a macOS Debug build:

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

The seven leaf adapters are mandatory source-level integrations: configure
validates the initialized pinned submodule and always compiles the bridge
translation units. The old `AETHER_USE_KRKRZ_LEAF_PLUGINS` source-selection
option and its historical local implementations have been removed, so there is
only one implementation path and no stale cache value can select a second copy.
This is not a runtime/game switch: the product has one plugin registry, and
hybrid providers select their Aether fallback automatically when an upstream
operation cannot be used. The layerExSave codec bridge remains available in the
normal integration target. Both public fallback and private AetherInternal
configurations are supported; the latter extends targets instead of replacing
them.

The minimum pre-submit checks are:

```bash
python3 tools/plugin_manifest_report.py --strict
python3 tools/plugin_gap_audit.py
cmake --build <build-dir> --target krkr2plugin --parallel
ctest --test-dir <build-dir> --output-on-failure
```

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

The submodule's source and license notices remain in the submodule. Do not copy
its source into `cpp/plugins` or publish private AetherInternal sources as part
of this integration.
