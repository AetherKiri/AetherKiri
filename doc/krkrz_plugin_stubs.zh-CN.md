# 兼容性 Stub

[English](krkrz_plugin_stubs.md) | [简体中文](krkrz_plugin_stubs.zh-CN.md)

`cpp/plugins/stubs/dummy_plugin_stubs.cpp` 现在只保留确实没有可移植实现、且
由宿主负责的兼容面（GPU bridge fallback、Flash 和旧效果锚点）。已经有真实
Aether adapter 的模块不再注册空回调，避免第二个 callback 遮蔽插件 registry
选择的适配器。

每个模块的权威状态记录在
[`runtime/kirikiri/manifests/plugins.toml`](../runtime/kirikiri/manifests/plugins.toml)。
Stub 可以只提供名称或形状兼容，同时在 manifest 中标记为 `status = "stub"`；
调用方不能把它当作功能完整的 native 支持。`resourceRW` 和 `krkrsteam` 已不再是
Stub：前者通过 sidecar 保留资源，后者通过本地 cloud 保留成就/文件，并对 SDK 专属
操作返回失败。`sigcheck` 已改为 hybrid 的可移植 SHA-256/RSA-PSS 校验器：有
OpenSSL provider 时使用进程级密码实现，没有 provider 时明确返回失败。`win32ole`
仍是明确的 fail-closed COM/ActiveX 边界，避免静默接受不安全调用。
