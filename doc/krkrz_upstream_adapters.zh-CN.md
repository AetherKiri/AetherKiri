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
`layerExSave` 使用的 namespace 隔离 LodePNG/TLG5 方法；它的 Aether wrapper 还在
不引入 upstream Layer/Storage ABI 的前提下补齐 `oozeColor`、fingerprint、
shrink-vector、octet-vector 和 province palette 接口。Hybrid 复用还包括
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

Core adapter 也遵循同一条“单 owner”规则：固定 submodule 提供实现字节，Aether 提供
公开 ABI 和生命周期：

| 区域 | Adapter | Aether 继续负责 |
| --- | --- | --- |
| Sound DSP/loop/FFT | `cpp/core/sound/upstream_bridge`、`cpp/core/utils/upstream_bridge` | sound host、allocator、`DesiredFormat`、scalar fallback 与分发策略 |
| Visual SIMD 叶子 | `cpp/core/visual/upstream_bridge/VisualSIMDLeavesDispatch.cpp`、`UnivTransSSE2.cpp` | Highway 负责 blend/adjust/color-fill；Aether 负责 alpha 语义、HDA/additive-alpha ColorMap、box-blur、Bitmap/线程 ABI 与 scalar fallback |
| TLG/Resample | `cpp/core/visual/upstream_bridge/TLGSIMD.cpp`、`ResampleImage{SSE2,AVX2}.cpp` | `LoadTLG` 格式路由、虚拟流、metadata，以及 nearest/不支持滤镜/无 scanline fallback |
| Resample | `cpp/core/visual/upstream_bridge/ResampleImage{SSE2,AVX2}.cpp` + `ResampleImageDispatch.cpp` | Bitmap/线程 ABI、CPU/OS 选择、nearest/不支持滤镜/无 scanline/universal fallback |
| 可变字体 | `cpp/core/visual/upstream_bridge/FontVariations.cpp` + `FontStream.cpp` | FreeType face、fallback 顺序、有界缓存和 FontSystem registry |
| DAP | `cpp/core/tjs2/upstream_bridge`、`cpp/core/utils/upstream_bridge/DAPServer.cpp` | VM hook 安装、Aether 线程 ABI 和主事件循环 |
| 文件 REPL | `cpp/core/utils/ReplFileChannel.cpp` | RAII stream ABI 与主线程求值；upstream console/socket 仅作参考 |
| CLIP | `cpp/plugins/upstream_bridge/clipfile_*.cpp` + `clipfile_compat.hpp` | `clip://` Storage、Layer/TJS wrapper、SQLite owner 与平台生命周期 |
| Richtext/Minikin | `cpp/plugins/upstream_bridge/krkrz_richtext_*.cpp` | Aether FontService/FreeType owner、模块注册、stream 生命周期和 renderer 上传边界 |

文件 REPL 通过 `-replfile=<目录>` 启用，使用 UTF-8 `cmd`/`resp` JSON 文件交互。
它是开发/诊断通道，不是面向游戏的兼容开关。

`clipfile` 已是默认 hybrid adapter。三个桥接翻译单元只包含 pinned submodule 的
`clipclass.cpp`、`clipwriter.cpp` 和 `main.cpp`；`clipparse` 的五个 C++ 源文件作为
独立静态目标编译，并与旧 sqlite3/XP3 VFS 共用唯一的现代 SQLite owner（CI 使用
vcpkg 固定包，本机优先使用具备 deserialize 的系统 provider，独立 checkout 使用带哈希校验的
3.45.1 fallback）。`clipfile_compat.hpp`
先预加载 Aether 的 `tp_stub`/事件接口，再在局部宏域中把 `iTJSBinaryStream` 映射为
带 `Destruct()` 的 wrapper，并把 `TVPCreateStream` 的所有权转回 RAII；同时补齐
非 Windows 的 `S_OK`、BMP header 和反向字符串查找兼容名。这样 CLIP 的懒加载、区域
读取、写回和 `clip://` media 可以复用，但不会引入 upstream 的第二套 registry。
web/emscripten 因 Storage host 不同暂不编译。`krkr_richtext` 现在已经是桌面和
Android 的 hybrid adapter：pinned submodule 中的 richtext/Minikin 源码共用 Aether
的 FontService、FreeType、HarfBuzz 和 ICU owner；传统文本 API 继续使用现有
`TextRenderBase`，因此不会改变旧游戏路径。iOS 和 web 保留源码契约，但在配置阶段跳过
依赖较重的目标。Effekseer 与 threepp 仍只做 optional 源码校验，因为它们的
OpenGL/VRM 宿主生命周期尚未与 Aether renderer 对齐。
