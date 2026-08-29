# AetherKiri × krkrz_dev 全量审核矩阵

[English](krkrz_full_audit.md) | [简体中文](krkrz_full_audit.zh-CN.md)

本文记录 `third_party/krkrz_dev` 在 AetherKiri 中的完整源码、构建和 ABI 审核结果。
它和 [`krkrz_integration.zh-CN.md`](krkrz_integration.zh-CN.md) 的区别是：后者记录
已经落地的集成契约，本文则覆盖 checkout 中的全部插件、core、external、脚本和工具。

## 审核范围与结论

本次审核覆盖：

| 范围 | 数量 | 说明 |
| --- | ---: | --- |
| `src/plugins` | 56 个目录 | 每个目录都归入直接复用、混合复用、Aether 所有、宿主兼容、optional 或 ABI 基础设施 |
| `src/core` 源码树 | 2,490 个文件 | 包含 common、generic、sdl3、win32、external、tests 以及资源/文档/构建元数据；机器审计不会漏掉顶层目录 |
| `src/core/common` | 487 个文件 | 覆盖 base、environ、extension、glad、msg、sound、tjs2、utils、visual |
| core 平台层 | 213 个文件 | generic 50、sdl3 41、win32 122 |
| core external | 1,579 个文件 | 5 组 submodule：elements、glyphware、movie-player、pl_mpeg、sound-codecs |
| core 资源/测试/元数据 | 211 个文件 | data 99、resource 14、tests 9、doc/licenses/cmake/tp_stub 及顶层清单 |
| 脚本 submodule | 5 个 | KAG3、KAG3_Ham、Krkr2Compat、Sample、test |
| 工具 submodule | 2 个 | tjs2doc、Win32 debugger |

结论不是“把整个 krkrz core 链进 Aether”，而是：Aether 保持唯一运行时 ABI、插件注册表、
Storage、Layer/Renderer、Sound host 和 TJS VM；upstream 代码按算法和协议边界进入。
所有复用都通过现有 submodule，禁止把源码复制到 Aether 仓库。

## 56 个插件逐项归类

### 直接从 submodule 编译（7）

这些目录的业务翻译单元由 `cpp/plugins/upstream_bridge` 建立 Aether ABI 后直接包含，
没有本地业务副本：

`layerExAreaAverage`、`layerExRaster`、`layerExLongExposure`、`getSample`、
`layerExBTOA`、`layerExImage`、`shrinkCopy`。

### 混合复用（8）

| 插件 | Aether 保留部分 | upstream 使用部分 |
| --- | --- | --- |
| `AlphaMovie` | FFmpeg、工作队列、纹理上传、音频路由、Godot presentation | 编解码行为参考 |
| `KAGParserEx` | 唯一 KAG parser、tag metadata、编译场景解析 | 语义和兼容边界参考 |
| `layerExSave` | Layer/Storage/TJS/octet/线程边界 | namespace 隔离的 LodePNG、TLG5 slide，以及经边界适配的图层工具方法 |
| `clipfile` | `clip://` Storage、Layer/TJS wrapper、单一 SQLite owner | pinned submodule 的 CLIP parser/writer 与 tiled decode |
| `krkr_richtext` | Aether FontService/FreeType、ICU/HarfBuzz owner、模块/stream 生命周期 | pinned submodule 的 richtext/Minikin 布局、glyph 渲染和 TJS API |
| `extNagano` | provider registry 和 fallback | 10 个自包含转场算法 |
| `layerExVector` | LayerExDraw、字体注册和渲染生命周期 | vector API 语义 |
| `psdfile` | TJS/Layer/Storage wrapper | nested `psdparse` parser |

`extNagano` 的 `3duniversal`、`imagewipe` 以及不兼容的选项形状自动使用 Aether
provider；这不是面向用户的运行时开关。

### Aether 保持唯一实现（16）

`addFont`、`binaryStream`、`csvParser`、`expat`、`extrans`、`fstat`、`json`、`layerEx`、
`layerExDraw`、`lineParser`、`memfile`、`menu`、`minizip`、`saveStruct`、`scriptsEx`、
`varfile`。

这些模块的 upstream 版本只用于 API、边界条件和测试参考。尤其不能并列链接
`layerExDraw`、`minizip`、`KAGParser` 或 `scriptsEx` 的第二份实现。

### 宿主、平台和兼容层（20）

`fpslimit`、`gamepad`、`httprequest`、`httpserv`、`krkrgles`、`krkrlive2d`、`messenger`、
`msgreceiver`、`process`、`resourceRW`、`shellExecute`、`sigcheck`、`stdio`、`steam`、
`systemEx`、`tftSave`、`win32dialog`、`win32ole`、`windowEx`、`windowExProgress`。

它们依赖 Win32、SDL3、OpenGL、Steam SDK、Cubism 或 Aether 宿主生命周期，当前采取
Aether bridge、私包实现、兼容 surface 或 stub。`fpslimit` 是兼容 no-op；`tftSave` 保留
脚本接口；`krkrlive2d` 的真实实现位于私有 AetherInternal 包。

### 新功能，暂不接入产品（2）

`krkreffekseer`、`krkrthreepp`。

Effekseer 和 threepp 分别需要 Effekseer OpenGL、threepp/VRM 3D 宿主合同，当前只做
optional 源码存在性校验。`krkr_richtext` 的 Minikin/richtext 已通过 Aether
FontService、ICU/HarfBuzz 和 RAII stream 适配器接入桌面和 Android；iOS/web 因宿主或
依赖差异在配置阶段跳过。CLIP 的可移植 parser/writer 也已通过 Aether stream/storage
适配器接入桌面和 Android，web/emscripten 仍因 Storage host 差异跳过。

### ABI 基础设施（3）

`ncbind`、`simplebinder`、`tp_stub`。

禁止导入 upstream 的 binder、`v2link`、`tp_stub.cpp`、独立注册表或 standalone CMake；
这些会产生第二套调用约定、模块注册和全局符号。

上述六组的总数为 `7 + 8 + 16 + 20 + 2 + 3 = 56`，与 checkout 实际目录数一致。
同一份分类已写入 manifest 的 `[plugin_catalog]`，并由
`tools/plugin_manifest_report.py --strict` 对 checkout 的 56 个目录做精确覆盖检查。

## Core 审核矩阵

### common

| 目录 | 文件数 | 处理策略 |
| --- | ---: | --- |
| `base` | 49 | Aether 保留 Storage/Stream/Archive owner；可吸收 BinaryStream、StorageCache、XP3 边界修复 |
| `environ` | 7 | 宿主窗口、输入和生命周期不同，参考为主 |
| `extension` | 2 | 接口思想可参考，所有权在 Aether |
| `glad` | 6 | upstream OpenGL loader，不用于 Aether Godot/SDL2 host |
| `msg` | 11 | 消息语言系统按 Aether 生命周期适配 |
| `sound` | 37 | scalar/SIMD MathAlgorithms、RealFFT、WaveSegmentQueue 和 WaveLoopManager 已通过 source bridge 使用 submodule 实现；PhaseVocoder 只分发经过 parity 的叶子，host/allocator/lifecycle 仍在 Aether |
| `tjs2` | 101 | VM、parser、Variant、Dictionary 和 ABI 不替换；debugger hook/symbol/core 已通过 ABI bridge 接入 |
| `utils` | 62 | Random.cpp、ClipboardIntf.cpp、MiscUtility.cpp、md5.c、RealFFT 和 DAP transport 已通过 source bridge 复用；REPL file channel 由 Aether adapter 提供 |
| `visual` | 212 | Aether 保留 Bitmap/Loader/Renderer/Font owner；TLG、Resample SSE2/AVX2、ColorMap/24-bit 转换及 universal-transition 叶子已适配；box-blur、HDA/additive-alpha 变体仍由 Aether 保持精确语义 |

### 相似但明确不整文件桥接的实现

下面这些文件与 Aether 版本存在较高的文本相似度，但逐项检查后仍保留 Aether
实现，避免把表面相同误当成 ABI 或语义相同：

| upstream 文件 | 审核结论 |
| --- | --- |
| `common/environ/TouchPoint.cpp` | 只有两个常量定义，而且 Aether 当前平台 CMake 未编译这一路径；保留本地平台文件，不扩大无效桥接范围 |
| `common/utils/VelocityTracker.{h,cpp}` | 算法主体接近，但 `VelocityTrackers::update` 的 tick 类型为 `uint32`/`uint64` 两种公开约定；保留 Aether 头和实现，按方法做 parity |
| `common/utils/TickCount.cpp` | 溢出线程和计时生命周期语义不同；不能用 upstream 整文件替换 |
| `common/base/CharacterSet.cpp` | upstream 增加 surrogate/codepoint 与长度处理；Aether 的兼容行为有意不同 |
| `common/base/BinaryStream.cpp` | upstream 使用 `iTJSBinaryStream::Destruct()`，Aether 使用 RAII 的具体 `tTJSBinaryStream`；只能做 adapter/reference |
| `common/visual/ComplexRect.cpp`、`LayerBitmapIntf.cpp` | 矩形裁剪、Bitmap hierarchy 和 renderer 状态有实质差异；保留 Aether owner |
| `common/tjs2/tjsInterface.cpp` | 空实现翻译单元，没有可复用业务逻辑 |

因此“未出现在当前 source bridge 清单中”本身也是审核结论，而不是遗漏。后续 upstream
版本变化时，只有先通过同样的 ABI、语义和目标编译检查，才可把这些条目改成桥接。

### 平台层

`generic`（50 个文件）、`sdl3`（41 个文件）和 `win32`（122 个文件）全部审核为平台
实现或参考输入，不进入 Aether 的 Godot/SDL2 渲染、音频和事件主链。Win32 的 GDI、D3D、
DirectInput、PE resource 和 COM 代码尤其不能直接链接到 macOS/Android/Web 构建。

### external、脚本和工具

| 路径 | 结论 |
| --- | --- |
| `src/core/external/elements`、ThorVG | UI/矢量控件，不能和 Aether LayerExDraw 并存为第二 renderer |
| `src/core/external/glyphware` | 字体 metadata、emoji、fallback、shaping 的参考输入；Aether FontService 已提供对应的可移植 FreeType/HarfBuzz/FriBidi 适配 |
| `src/core/external/movie-player`、`pl_mpeg` | 独立播放器或 MPEG1 fallback，Aether 继续使用 FFmpeg |
| `src/core/external/sound-codecs` | allocator hook/header，不是完整音频管线 |
| `script/KAG3`、`KAG3_Ham`、`Krkr2Compat`、`Sample`、`test` | 直接作为 fixture/reference，不复制到 runtime |
| `src/tools/dotNet/tjs2doc`、`src/tools/win32/debugger` | 开发工具，不随产品链接 |

## 资源入口审核

### 图像

Aether 的 [`GraphicsLoaderIntf.cpp`](../cpp/core/visual/GraphicsLoaderIntf.cpp) 统一路由
BMP、PNG、JPEG、TLG、BPG、WEBP、JXR、PVRv3 和 AMV，并负责扩展名注册、异步加载、缓存
和虚拟 Storage。upstream `SimpleImageLoad` 只是 PNG/JPEG 的简单 RGBA 辅助函数，不能
替换该入口。

### TLG

Aether 的 [`LoadTLG.cpp`](../cpp/core/visual/LoadTLG.cpp) 除传统 TLG5/TLG6 外，还包含
`TLGmux`、`TLGref`、`TLGqoi`、QHDR/metadata、LZ4 分带、arena/mmap 和诊断路径。
upstream 的 [`LoadTLG.cpp`](../third_party/krkrz_dev/src/core/common/visual/LoadTLG.cpp)
主要覆盖原始 TLG5/TLG6 和 TLG0.0 SDS。

因此只复用 upstream 的解压、颜色合成和 SSE2 内核；文件头路由、特殊格式、metadata、
虚拟流和错误处理必须留在 Aether。不能用 upstream 整个 `LoadTLG.cpp` 替换 Aether 版本。
TLGref adapter 同时接受两种 QREF 写法：`nameBytes` 包含 UTF-16 结尾 NUL，或像
DRACU-RIOT 资源那样只计名字 code unit、把 NUL 紧跟在声明范围之后。2026-08-30 的
macOS arm64 Debug 回放探针已经通过 QREF → TLGqoi/QHDR 解码 `ev114ab.tlg`，美羽回想
画面不再是占位图。

### 存储和归档

Aether 的 [`cpp/core/base/CMakeLists.txt`](../cpp/core/base/CMakeLists.txt) 已负责 XP3、
Cx decoder、ZIP、7z、TAR、libarchive、unrar、zstd、minizip、mmap、预取和虚拟 media。
upstream `BinaryStream`、`StorageCache`、XP3 parser 检查可以逐方法吸收，但不能替换
Aether Storage registry 和 archive lifecycle。

### 音频

Aether 的 [`WaveIntf.cpp`](../cpp/core/sound/WaveIntf.cpp) 已注册 FFmpeg、Opus、RIFF/WAV
和 Vorbis decoder。`MathAlgorithms.cpp`、`RealFFT.cpp`、`WaveSegmentQueue.cpp` 和
`WaveLoopManager.cpp` 的实现已经通过 bridge 直接使用固定 submodule 源码；SSE/NEON、
phase-vocoder 只分发经过 parity 的叶子。upstream 的线程、allocator 和完整 AudioStream
不能直接替换 Aether 音频管线；loop manager 只使用适配器提供的短生命周期分配函数。

### 脚本、字体和调试

- TJS2/KAG parser 保持 Aether owner；KAG3 等 nested script 只作为测试和参考。
- `FontStream`、`FontVariations`、颜色 glyph、TTC face index、surrogate/codepoint、
  glyphware metadata/emoji/fallback 已接到 Aether 的共享 FontService；桌面检测到
  HarfBuzz+FriBidi 时还启用复杂脚本 shaping，但不能引入第二套字体 registry 或 renderer。
- DAP 已完成桌面 VM hook、线程、socket 和事件循环适配；REPL 的 `-replfile` 安全子集
  已由 Aether 主线程 adapter 提供。upstream console/icline/socket 前端和 alloc-stats
  overlay 仍只作参考，不能误报为产品 UI 能力。

### 插件能力补全

- `layerExSave` 已补齐 upstream 可见的 `oozeColor`、`getFingerPrintValue`、
  `getShrinkVectorOctet`、`octetVectorSum` 和 `saveProvinceImage`。固定 256 色 province
  palette 与 PNG 编码直接使用 pinned submodule 中 namespace 隔离的 LodePNG，Layer、
  Storage 和 TJS 生命周期仍由 Aether 管理；`getShrinkVectorOctet` 还修正了 upstream
  对高度参数为零会除零的问题。
- `resourceRW` 已提供有界 ICO/CUR、组图标和 `VS_VERSION_INFO` 对象，并保留 Win32
  原生 PE resource 路径；非 Win32 使用确定性 sidecar。资源类型常量仅在全局缺失时
  安装，卸载时也只删除自己仍拥有的值。
- `windowEx` 已补齐 cursor clip、顶层窗口查找、class long、cursor load、virtual-key
  mapping 和 DPI context 接口；Win32 使用原生 API，其他宿主提供可观察的逻辑语义或
  明确返回不可用。`systemEx` 同时补齐 OS/known-folder/环境变量/消息处理接口。
- `gamepad` 使用 SDL 映射控制器/通用 joystick、边沿计数和 rumble；upstream 常量及
  `GamepadPort`/`Gamepad` 别名只在缺失时安装，不覆盖游戏或 native plugin 的实现。

## 重复实现与 ABI 边界

Aether 的 [`tTJSBinaryStream`](../cpp/core/tjs2/tjs.h) 是带 RAII 析构的具体类；upstream
使用带 `Destruct()` 的 `iTJSBinaryStream`。Bitmap hierarchy、Storage media、线程锁和
插件注册接口也存在同类差异。所有 upstream leaf 都必须经过 bridge、namespace 或显式
wrapper；不能依靠“代码看起来相似”直接链接。

当前产品遵循一条规则：每个全局类、模块和 registry 只有一个 owner。算法可以来自
upstream，但公共符号和生命周期只能由 Aether 暴露。

## 当前状态与后续顺序

当前已经落地：

1. 7 个插件直接 submodule adapter、CLIP 与 richtext 两个 hybrid adapter，以及 30 个 core source bridge/leaf adapter（sound SIMD/FFT/loop、TLG、Resample、视觉叶子、字体、DAP、TJS/utility）；
2. 8 个 hybrid plugin；
3. Visual/Sound 隔离 parity target 与字体、TLG、debugger、REPL、CLIP 契约测试；
4. 递归 submodule 和 manifest pin 校验（当前 krkrz_dev 共 75 个 nested submodule）；
5. 84 项 KiriKiri2 兼容参考项，缺失 0。

剩余工作按风险排序：

1. macOS arm64 Debug 产品和隔离兼容测试构建都已经完成链接；隔离 CTest 为
   266/266，直接 Catch2 插件测试为 229/229 个用例且所有实际执行的断言均通过；Visual parity 为
   294/294，Sound parity 为 28/28（fail=0）。覆盖了损坏 TLG5/TLG6 的预检与边界、
   krkrz 压缩 TLG5 回环、BGRA↔RGBA 适配和 x86 SSE2 合成包装，以及 resourceRW、
   FontService、DAP/REPL、CLIP、gamepad、layer-effect 契约。2026-08-30 已完成
   全开 macOS Debug 产品构建并通过签名校验；同时用真实 DRACU-RIOT 资源完成了
   鉴赏模式 → 剧情回想 → 美羽回想流程，`ev114ab.tlg` 已解码为真实画面而非占位图。
   目前剩余的真实宿主样例仅是 `resourceRW`、`krkrsteam`、Sound DSP、
   WaveLoopManager、FontStream、DAP、`-replfile` 和 CLIP；这些是交互覆盖缺口，
   不是未解决的构建或符号缺口；
2. 对仍明确失败关闭的宿主专属接口（`win32ole`、DirectShow/AVI、SWF、Steamworks 截图/DLC/直播）补充调用方可见的错误/能力查询，并在有真实宿主 SDK 时分别验证；`sigcheck` 已有 OpenSSL-backed 可移植校验器，仅在没有密码 provider 时失败；
3. 为 Effekseer、threepp 评估各自的 OpenGL/VRM SDK 与宿主生命周期，未验证前不链接；
4. 对 upstream 每次 pin 更新重复 ABI、符号和资源入口审核。

本文件是静态审核矩阵，不把 optional/reference 误报为已接入功能。源码版本变更时，
必须重新执行递归 submodule 校验、manifest 严格检查、plugin gap audit 和对应 parity
测试。`plugin_manifest_report.py --strict` 与 `krkrz_core_audit.py` 都会拒绝未初始化、
漂移、冲突或本地有改动的 nested submodule/worktree。

## 可重复执行的全量门禁

以下命令不需要构建完整产品即可审核 checkout、manifest、插件目录覆盖和桥接关系：

```bash
python3 tools/plugin_manifest_report.py --strict
python3 tools/krkrz_core_audit.py
python3 tools/plugin_gap_audit.py
```

在启用 `ENABLE_TESTS=ON` 的 CMake 构建中，前两项分别注册为
`plugin_manifest_contract` 和 `krkrz_core_contract`；因此 upstream gitlink、目录数量、
manifest 路径或 source bridge 发生漂移时，CTest 会直接失败。
