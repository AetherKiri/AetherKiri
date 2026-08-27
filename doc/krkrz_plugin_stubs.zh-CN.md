# 兼容性 Stub

[English](krkrz_plugin_stubs.md) | [简体中文](krkrz_plugin_stubs.zh-CN.md)

`cpp/plugins/stubs/dummy_plugin_stubs.cpp` 为尚未纳入可移植 Aether 运行时的
旧 KiriKiri 模块提供刻意精简的注册。将这些源码单独放在一个目录中，可以明确
它们是兼容边界，而不是 upstream 插件实现。

每个模块的权威状态记录在
[`runtime/kirikiri/manifests/plugins.toml`](../runtime/kirikiri/manifests/plugins.toml)。
Stub 可以只提供名称或形状兼容，同时在 manifest 中标记为 `status = "stub"`；
调用方不能把它当作功能完整的 native 支持。
