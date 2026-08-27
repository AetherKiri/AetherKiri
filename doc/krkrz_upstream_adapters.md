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
includes the namespaced LodePNG/TLG5 methods used by `layerExSave`, plus the
`blurfade`, `book`, `flutter`, `honeyturn`, `morphing`, `multiripple`, `rgbfade`,
`scanline`, `spin`, and `zoomfade` transition algorithms. The extNagano
`3duniversal` and `imagewipe` providers remain Aether fallbacks because their
texture-provider ABI does not match; option conversion failures also fall back
automatically. More invasive plugins remain hybrid or Aether-owned until their
runtime contract is proven.

This is an implementation-level compatibility rule, not a runtime switch:
there is one Aether plugin registry, and a game sees the same module/provider
names regardless of whether an upstream operation accepts its inputs.
