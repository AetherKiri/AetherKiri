# Compatibility stubs

[English](krkrz_plugin_stubs.md) | [简体中文](krkrz_plugin_stubs.zh-CN.md)

`cpp/plugins/stubs/dummy_plugin_stubs.cpp` now contains only the genuinely
host-owned compatibility surfaces that have no portable implementation (the
GPU bridge fallback, Flash and the legacy effect anchor). Empty module
registrations were removed once a real Aether adapter existed; this prevents a
second callback from masking the adapter selected by the plugin registry.

The authoritative per-module status is
[`runtime/kirikiri/manifests/plugins.toml`](../runtime/kirikiri/manifests/plugins.toml).
A stub may provide name/shape compatibility while still being reported as
`status = "stub"`; callers must not treat it as feature-complete native
support. `resourceRW` and `krkrsteam` are no longer stubs: their portable
sidecar/local-cloud adapters retain data and return failure for SDK-only
operations. `sigcheck` is now a hybrid portable SHA-256/RSA-PSS verifier: it
uses the process OpenSSL provider when available and reports an explicit
failure when no provider exists. `win32ole` remains an explicit fail-closed
COM/ActiveX boundary because silently accepting calls would be unsafe.
