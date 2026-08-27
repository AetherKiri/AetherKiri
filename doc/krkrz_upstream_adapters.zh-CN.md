# krkrz 源码适配器

[English](krkrz_upstream_adapters.md) | [简体中文](krkrz_upstream_adapters.zh-CN.md)

`cpp/plugins/upstream_bridge` 中的翻译单元是选定 `krkrz_dev` 插件源码进入
Aether 构建的唯一入口。每个适配器先建立 Aether 的 `ncbind`/TJS ABI，明确
模块名称，然后再包含固定版本 `third_party/krkrz_dev` checkout 中的业务翻译
单元。

新增适配器时请遵守以下规则：

* 不要包含 upstream 的 `tp_stub.cpp`、`ncbind.cpp`、`v2link.cpp`、插件注册表或
  upstream CMake 文件；这些运行时设施由 Aether 唯一维护。
* ABI 差异应在这个小型适配器中解决，并配有注释和测试；不要修改 submodule
  工作树。
* 保留 Aether 的所有权和线程规则。特别是 `tTJSBinaryStream` 使用 RAII 管理，
  不能调用 upstream 的 `iTJSBinaryStream::Destruct()`。
* 将源码和适配器登记到
  `runtime/kirikiri/manifests/plugins.toml`，并保证 manifest revision 与父仓 gitlink
  一致。

当前适配器有意限制在低风险的叶子插件：
`layerExAreaAverage`、`layerExRaster`、`layerExLongExposure`、`getSample`、
`layerExBTOA`、`layerExImage` 和 `shrinkCopy`。更复杂的插件在运行时契约得到
验证前，继续采用 hybrid 或 Aether-owned 方案。
