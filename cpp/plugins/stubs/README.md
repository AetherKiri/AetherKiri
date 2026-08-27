# Compatibility stubs

`dummy_plugin_stubs.cpp` contains deliberately small registrations for legacy
KiriKiri modules whose native implementation is not part of the portable
Aether runtime.  Keeping these registrations in a separate directory makes
it explicit that they are compatibility boundaries, rather than upstream
plugin implementations.

The authoritative per-module status is
`runtime/kirikiri/manifests/plugins.toml`.  A stub may provide name/shape
compatibility while still being reported as `status = "stub"`; callers must
not treat it as feature-complete native support.
