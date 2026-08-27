# krkrz source adapters

The translation units in this directory are the only place where selected
`krkrz_dev` plugin sources enter the Aether build. Each adapter establishes the
Aether `ncbind`/TJS ABI, names the module explicitly, and then includes the
business translation unit from the pinned `third_party/krkrz_dev` checkout.

Keep these rules when adding an adapter:

* Do not include upstream `tp_stub.cpp`, `ncbind.cpp`, `v2link.cpp`, plugin
  registries, or upstream CMake files. Aether owns those runtime facilities.
* Resolve an ABI difference in this small adapter, with a comment and a test;
  do not patch the submodule working tree.
* Preserve Aether's ownership and threading rules. In particular,
  `tTJSBinaryStream` is RAII-managed and must not receive an upstream
  `iTJSBinaryStream::Destruct()` call.
* Add the source and adapter to
  `runtime/kirikiri/manifests/plugins.toml` and keep the manifest revision
  equal to the parent gitlink.

The current adapters are intentionally limited to the low-risk leaf plugins:
`layerExAreaAverage`, `layerExRaster`, `layerExLongExposure`, `getSample`,
`layerExBTOA`, `layerExImage`, and `shrinkCopy`. More invasive plugins remain
hybrid or Aether-owned until their runtime contract is proven.
