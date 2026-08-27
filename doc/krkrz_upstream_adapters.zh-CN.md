# krkrz 源码适配器

[English](krkrz_upstream_adapters.md) | [简体中文](krkrz_upstream_adapters.zh-CN.md)

`cpp/plugins/upstream_bridge` 中的翻译单元是选定 `krkrz_dev` 插件源码进入
Aether 构建的唯一入口。每个适配器先建立 Aether 的 `ncbind`/TJS ABI，明确
模块名称，然后再包含固定版本 `third_party/krkrz_dev` checkout 中的业务翻译
单元。少量可移植 upstream 实现文件（layerExSave codec 和选定的 extNagano
算法）也会直接编译，但它们始终通过 Aether 自有 wrapper 或 detached provider
使用；不会导入 upstream registry 或生命周期。
七个直接叶子插件在父仓库中不再保留本地 fallback 副本；固定版本 submodule 加上这些
适配器就是它们唯一的源码路径。

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

当前直接叶子适配器是低风险插件：
`layerExAreaAverage`、`layerExRaster`、`layerExLongExposure`、`getSample`、
`layerExBTOA`、`layerExImage` 和 `shrinkCopy`。Hybrid 源码复用还包括
`layerExSave` 使用的 namespace 隔离 LodePNG/TLG5 方法，以及
`blurfade`、`book`、`flutter`、`honeyturn`、`morphing`、`multiripple`、`rgbfade`、
`scanline`、`spin`、`zoomfade` 十个转场算法。由于 texture-provider ABI 不匹配，
extNagano 的 `3duniversal` 和 `imagewipe` 保留 Aether fallback；选项转换失败时
也会自动 fallback。更复杂的插件在运行时契约得到验证前，继续采用 hybrid 或
Aether-owned 方案。

`layerExVector.dll` 是一个不复制 ThorVG 的 hybrid 适配：
`cpp/plugins/krkrzLayerExVectorCompat.cpp` 先加载 Aether 的 `layerExDraw`，再在
同一个 TJS 类上补齐 `GdiPlus.loadFont`（包括桌面原生字体路径）、字体别名、
`fontFamily`/`fontSize`/`italic`/`letterSpacing`/`lineSpacing` 和
`drawStringArea`。vector 侧的 `lineSpacing` setter 遵循 krkrz 可写的非负比例；
适配层通过私有的 sibling 属性读取 Aether 原生像素行高，再应用该比例。因此
vector 游戏与原有 LayerExDraw 游戏共享一套 renderer 和支持 face index 的字体
注册表；`unloadFont` 按进程生命周期保留已注册字体，避免仍在使用的 Font 对象悬空。

这是实现层的兼容规则，不是运行时开关：Aether 只有一个插件 registry；无论 upstream
操作是否接受输入，游戏看到的模块/provider 名称都保持不变。
