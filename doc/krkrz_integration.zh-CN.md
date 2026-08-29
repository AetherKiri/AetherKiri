# AetherKiri × krkrz_dev 集成说明

[English](krkrz_integration.md) | [简体中文](krkrz_integration.zh-CN.md)

本文是 AetherKiri 复用
[`wamsoft/krkrz_dev`](https://github.com/wamsoft/krkrz_dev) 的实现契约。目标是
在 upstream 业务逻辑兼容时复用其源码，同时由 AetherKiri 继续负责运行时 ABI、
插件注册、平台生命周期以及渲染/音频集成。

完整的 56 个插件、core、external、脚本、工具和资源入口审核见
[`krkrz_full_audit.zh-CN.md`](krkrz_full_audit.zh-CN.md)。本文只保留已经落地的集成
契约和构建验证方法，避免把“已审核”误解为“全部已接入产品”。

## 固定来源与 checkout

父仓库通过一个公开 gitlink 引入 `third_party/krkrz_dev`。当前 gitlink 与
manifest 都固定到：

```text
repository: wamsoft/krkrz_dev
revision:   83cc5cc4528bf431d16b5d4949cb11966331e392
```

全新 clone 后执行：

```bash
git submodule update --init --recursive third_party/krkrz_dev
python3 tools/plugin_manifest_report.py --strict
python3 tools/krkrz_core_audit.py
```

仓库 URL 有意保留维护者使用的 SSH 地址。公开 CI 只在这条命令中临时覆盖为
HTTPS：

```bash
git -c submodule.third_party/krkrz_dev.url=https://github.com/wamsoft/krkrz_dev.git \
  submodule update --init --recursive --depth 1 third_party/krkrz_dev
```

`plugin_manifest_report.py --strict` 还会检查 `krkrz_dev` 下的全部 nested
submodule；当前 checkout 共 75 个，任何未初始化、漂移、冲突或本地脏工作区状态都会直接失败。

更新 upstream 源码需要同时修改两处：移动父仓 gitlink，并更新
[`runtime/kirikiri/manifests/plugins.toml`](../runtime/kirikiri/manifests/plugins.toml)
中的 `upstream_revision`。严格 manifest 检查会阻止把过期 checkout 当成已审核
版本。

## 集成层次

不引入 upstream 的独立 CMake 文件。Aether 的
[`cpp/plugins/CMakeLists.txt`](../cpp/plugins/CMakeLists.txt) 选择源码，
[`cpp/plugins/upstream_bridge`](../cpp/plugins/upstream_bridge) 中的小型翻译单元
先建立 Aether ABI，再包含 upstream 业务翻译单元。适配器规则见
[`krkrz_upstream_adapters.zh-CN.md`](krkrz_upstream_adapters.zh-CN.md)。

当前默认启用的适配器如下：

| 模块 | 复用方式 | 边界 |
| --- | --- | --- |
| `layerExAreaAverage.dll` | upstream-adapted | upstream 实现，加 Aether 整数/指针转换 |
| `layerExRaster.dll` | upstream-adapted | upstream 光栅实现和共享 layer base |
| `layerExLongExposure.dll` | upstream-adapted | 在 Aether TJS ABI 下编译 upstream 实现 |
| `getSample.dll` | upstream-adapted | upstream 采样器，加 Aether `enableGetSample` 兼容回调 |
| `layerExBTOA.dll` | upstream-adapted | upstream 实现 |
| `layerExImage.dll` | upstream-adapted | upstream 图像实现，加可移植 `RGBQUAD` 定义 |
| `shrinkCopy.dll` | upstream-adapted | upstream 实现 |
| `psdfile.dll` | hybrid | Aether TJS/Layer/Storage wrapper，仅复用 upstream `psdparse` |
| `layerExSave.dll` | hybrid | Aether Layer/Storage/线程 wrapper，直接编译 namespace 隔离的 upstream LodePNG/TLG5 codec 并适配图层工具方法；BMP 和 TJS/octet 边界仍由 Aether 负责 |
| `clipfile.dll` | hybrid | 直接编译 pinned submodule 的 CLIP parser/writer；Aether 负责 `clip://` Storage、Layer/TJS ABI，并共用唯一进程级 SQLite owner（CI 为 vcpkg 固定包，本机优先使用具备 deserialize 的现代系统 SQLite，最后使用带哈希校验的 3.45.1 fallback） |
| `krkr_richtext.dll` | hybrid | pinned submodule 的 richtext/Minikin 布局与渲染源码共用 Aether 的 FontService/FreeType、ICU 和 HarfBuzz owner；传统 `TextRenderBase` 路径继续保留 |
| `extNagano.dll` | hybrid | Aether provider registry 包装十个选定的 upstream 转场算法；算法或选项不兼容时自动使用 Aether fallback |
| `KAGParserEx.dll` | hybrid | Aether 单一 parser 实现，upstream 语义作为参考 |
| `AlphaMovie.dll` | hybrid | Aether FFmpeg/队列/Godot pipeline；upstream codec 作为参考输入 |
| `resourceRW.dll` | hybrid | 保留 upstream Reader/Writer TJS 契约，在非 Win32 主机使用有界 `AKRRES01` sidecar |
| `krkrsteam.dll` | hybrid | 保留 upstream Steam API 形状，提供持久化成就和 `steam://` 本地 cloud；SDK 专属调用 fail-closed |

其余可移植模块（`scriptsEx`、`json`、`csvParser`、`lineParser`、`saveStruct`、
SQLite/VFS、`xp3filter`、`motionplayer` 以及 Aether GPU/Live2D bridge）继续由
Aether 维护。SQLite 使用一个现代的唯一 owner（CI 使用 vcpkg 固定包，本机优先使用具备
deserialize 的系统 provider，独立 checkout 再使用带哈希校验的 3.45.1 fallback），同时服务旧的 sqlite3
TJS API、XP3 VFS 和 CLIP 的 `serialize/deserialize`，不会再链接第二份数据库实现。
包括 stub 和 optional 模块在内的权威机器可读状态都在 manifest 中。

## KAG/TJS 脚本边界

KAG3、KAG3_Ham、Krkr2Compat、Sample 以及 upstream TJS2 测试集已经作为递归
submodule 放在 `third_party/krkrz_dev/script` 下。它们的路径、nested gitlink
revision 和入口点记录在 manifest 的 `[[script_components]]` 中，并由
`plugin_manifest_report.py --strict` 校验。这样可以保持 upstream 脚本的单一来源，
不把第二份脚本 vendoring 到 runtime。

产品 Demo 是有意保留的独立 fixture：它包含翻译文本和 Aether 专用 polyfill，
因此不会被 upstream KAG3 树静默替换。`Krkr2Compat` 和 `KAG3_Ham` 是参考/测试
输入，不会默认注入运行时。TJS2 测试集、issue-226 回归以及 KAG 入口链现在由
Aether 的 CTest harness 直接从 submodule 执行；这验证了脚本兼容性，但不会复制或
把 upstream KAG runtime 注入产品。

## ABI 与所有权规则

1. 不要链接 upstream 的 `tp_stub.cpp`、`ncbind.cpp`、`v2link.cpp`、独立插件注册表
   或 `krkrz.cmake`。它们会定义第二套运行时 ABI，并可能产生重复的模块/类符号。
2. 每个适配翻译单元都必须显式定义 `NCB_MODULE_NAME`，并首先包含
   `krkrz_aether_compat.hpp`。ABI 差异只放在适配器中解决，不能修改 submodule checkout。
3. Aether 的 `tTJSBinaryStream` 由 RAII 管理。凡是期待
   `iTJSBinaryStream::Destruct()` 的 upstream 代码都必须适配；对 Aether stream 调用
   `Destruct()` 是无效操作。
4. 每个全局类或模块只保留一个 owner。特别是不要在 Aether 的
   `cpp/core/base/KAGParser.cpp` 旁边再链接 upstream `KAGParser.cpp`，也不要导入
   完整 upstream 插件注册表。
5. 私有 AetherInternal hook 由 `AETHERKIRI_INTERNAL_KRKR2_PLUGIN` 保护；公开构建
   必须能仅依靠兼容 stub 和可移植 data-pack loader 完成链接。

## Core：Visual SIMD、Sound DSP 与 DAP

upstream core 有复用价值，但不是 Aether core 的直接替代品。manifest 的 core
契约记录了精确的 upstream 文件和 parity 测试。

* **Visual SIMD：** `cpp/core/visual/simd` 中 Aether 的 Highway/函数指针分发继续负责
  blend、adjust-color 和 color-fill。通过
  `cpp/core/visual/upstream_bridge/VisualSIMDLeavesDispatch.cpp` 接入经过 parity 的
  ColorMap/ColorMap65 基础变体和 24-bit 转换；universal-transition 叶子使用同一
  bridge，并由 Aether wrapper 修正 alpha 语义。由于字节级/LSB 算术不同，HDA、
  additive-alpha 的 ColorMap 变体及 box-blur 继续由 Aether 保持精确实现。兼容的
  TLG5/TLG6 叶子内核通过 `TLGSIMD.cpp` 接入，Resample 的 SSE2/AVX2 叶子则在
  scalar/Highway 初始化后按 CPU/OS 能力选择。nearest、不支持的滤镜、无法提供 CPU
  scanline 的 bitmap、arm64 和 universal 构建仍自动回到 Aether scalar/render owner，
  避免重复 `tvpgl` 符号和 dispatch 状态。
* **Sound DSP：** `cpp/core/sound` 与 `cpp/core/utils` 负责公开 sound ABI 和生命
  周期。scalar MathAlgorithms/RealFFT、WaveSegmentQueue、WaveLoopManager 以及 x86/NEON
  MathAlgorithms/xmmlib 已通过 bridge 使用固定 submodule 源码；`PhaseVocoderDSP`
  只分发兼容的 window/FFT 方法并始终保留 scalar fallback。主机音频、allocator 状态和
  `DesiredFormat` 生命周期仍由 Aether 唯一维护。
* **无宿主状态叶子：** `visual/gl/WeightFunctor.cpp`、`utils/Random.cpp`、
  `utils/ClipboardIntf.cpp`、`utils/MiscUtility.cpp`、`utils/md5.c`、`base/PluginIntf.cpp` 和 `tjs2/tjsException.cpp` 已用
  source bridge 直接消费 pinned submodule 的实现。桥接文件先包含 Aether 头文件，
  因此仍只有一套 TJS/message/clipboard ABI；平台存储和生命周期不会从 upstream 引入。
* **字体：** `visual/FontVariations.cpp` 通过 source bridge 使用。Aether 的共享
  FontService/FreeType owner 负责可变字体轴的范围裁剪、TTC face index、位图/颜色
  glyph、Unicode surrogate/codepoint、VS15/VS16 和 fallback 链，并使用按 storage
  path+face index 分键的有界不可变 FontStream cache；桌面检测到 HarfBuzz+FriBidi 时
  还启用复杂脚本 shaping，不具备依赖时自动回到兼容的标量路径。
* **DAP debugger：** upstream `tjsDebuggerCore`、hook/symbol 文件及 `DAPServer`
  已在桌面通过 Aether ABI bridge 链接。VM hook、线程启动和事件循环所有权仍由 Aether
  负责；只有传入 `-dap=<port>` 才启动，`src/core/tests/dap_smoke.py` 仍需真实宿主会话。
* **REPL：** Aether 提供 krkrz 文件 REPL 的安全子集：`-replfile=<目录>`。主线程
  adapter 解析并执行 TJS 表达式/语句，通过 UTF-8 `cmd`/`resp` JSON 原子交换结果。
  upstream console/icline/socket 前端的 stream/thread ABI 不兼容，因此只保留为参考，
  不链接第二套实现。

这里的区别是有意的：“算法可复用”不等于“可以作为第二套 core 安全链接”。
相似但被否决整文件桥接的 `VelocityTracker`、`TickCount`、`CharacterSet`、
`BinaryStream`、`ComplexRect` 和 `LayerBitmapIntf` 的逐项原因，见
[`krkrz_full_audit.zh-CN.md`](krkrz_full_audit.zh-CN.md) 的“相似但明确不整文件桥接”表。

## Optional 插件与 Stub

`clipfile` 已从 optional 提升为默认 hybrid：桌面和 Android 构建直接编译 pinned
submodule 中的 `clipparse` C++ 源码，并通过
`cpp/plugins/upstream_bridge/clipfile_compat.hpp` 将 upstream 的
`iTJSBinaryStream::Destruct()` 契约映射到 Aether 的 RAII stream。`CLIP` 提供
`.clip` 元数据、分块/区域读取、合成、预览和 `clip://` 虚拟 Storage；`CLIPWriter`
提供属性、像素、图层、缩放、缩略图和校验写回。现代 SQLite 在 Aether 中只有一个
owner，避免旧 sqlite amalgamation 与 CLIP 的全局符号冲突。web/emscripten 因没有
可移植 Storage host 而保持禁用，其他平台不需要运行时开关。

开启 `AETHER_USE_KRKRZ_OPTIONAL_PLUGINS=ON` 会校验仍需要 SDK 的
`krkreffekseer` 和 `krkrthreepp` 固定源码存在性。`krkr_richtext` 在支持的原生
平台已经不是 optional：检测到 FreeType、HarfBuzz 和 ICU 后，会把 pinned
Minikin/richtext 源码编译为 hybrid target；iOS/web 因依赖和宿主契约不同而跳过。
现有 Aether 文本 renderer 仍是旧游戏的兼容路径。
`layerExVector.dll` 已通过
[`cpp/plugins/krkrzLayerExVectorCompat.cpp`](../cpp/plugins/krkrzLayerExVectorCompat.cpp)
接入：它加载 Aether 唯一的 `layerExDraw` renderer，并适配 `GdiPlus.loadFont`、
字体别名（包括桌面原生字体路径）、`fontFamily`/`fontSize`/`italic`/`letterSpacing`
属性、`lineSpacing` 和 `drawStringArea`，不会再链接 ThorVG 或第二套全局类。
其中 `lineSpacing` 遵循 krkrz 可写的非负比例，布局时通过私有属性保留并读取
Aether 原生像素行高后再应用比例。其余两个依赖 SDK 的插件仍只做源码校验，需要
各自的 adapter、依赖策略和运行时测试后再接入。

`layerExSave.dll` 也已经补齐 upstream 的工具接口：`oozeColor`、
`getFingerPrintValue`、`getShrinkVectorOctet`、`Math.octetVectorSum` 和
`saveProvinceImage`。province PNG 直接使用 pinned LodePNG 和 upstream 固定的
256 色 palette；Layer buffer、虚拟 Storage 和 TJS 对象仍由 Aether 负责。缩略向量的
零尺寸参数在 adapter 边界直接拒绝，避免进入 upstream 的除零路径。

`resourceRW.dll` 保留 upstream 的资源类型/名称/语言行为。主机不能编辑 PE
镜像时，`ResourceWriter` 会通过
`cpp/plugins/portableResourceBundle.cpp` 写入 `<target>.aetherres` 确定性有界
容器，`ResourceReader` 可以重新枚举并读取 sidecar，支持 UTF-8/UTF-16 文本和
octet；原始 exe/dll 不会被覆盖。ICO/CUR、组图标和 `VS_VERSION_INFO` 已有有界的
可移植解析/序列化对象，资源常量只在游戏或 native host 没有定义时安装。
`windowEx` 保留 Win32 原生 cursor/DPI/class API，并在可移植宿主提供逻辑 cursor clip、
顶层窗口查找和键盘映射；SDL gamepad 对象和 upstream 常量也不会覆盖已有实现。
`krkrsteam.dll` 通过同一个 Storage 边界保存本地
状态，并注册可写的 `steam://<filename>` cloud namespace。成就、cloud 元数据和
语言在离线副本中可用；截图、直播、DLC 和账号归属因没有 Steamworks 身份而返回
`false`，不会伪造在线成功。

`sigcheck.dll` 已经不是只保留名字的 stub，而是 hybrid adapter：解析 upstream
的内嵌/sidecar 签名格式，在有 OpenSSL provider 时以有界内存流式执行
SHA-256/RSA-PSS 校验；没有密码 provider 时明确报错（fail-closed），绝不报告
虚假的成功。`win32ole` 仍属于宿主负责的 COM/ActiveX 边界并保持 fail-closed。

兼容性注册位于 [`cpp/plugins/stubs`](../cpp/plugins/stubs)，规则见
[`krkrz_plugin_stubs.zh-CN.md`](krkrz_plugin_stubs.zh-CN.md)。Stub 可以保留模块名或脚本形状，
但不是功能完整的 native 支持。已有 Aether adapter 的模块不会再保留空
callback；目前只有 `win32ole` 仍是未实现的宿主边界，其他 SDK 专属接口都应
暴露能力/错误状态而不是伪造成功。

## 构建与验证

GitHub Actions 的 `Build` workflow 默认使用完整兼容配置：所有平台的全新
CMake configure 都会收到 `AETHERKIRI_ENABLE_ONSCRIPTER=ON`、桌面
`AETHERKIRI_ENABLE_DAP=ON`、`AETHERKIRI_ENABLE_FONT_SHAPING=ON`、
`AETHERKIRI_ENABLE_CLIPFILE=ON`、`AETHER_USE_KRKRZ_OPTIONAL_PLUGINS=ON` 和
`AETHER_BUILD_KRKRZ_CORE_PARITY=ON`。optional plugin 选项只校验仍未接入的
SDK 源码，CLIP adapter 则由独立选项默认编译；parity 选项会在原生 CI 测试配置中
加入隔离的图像/声音测试。它们不会链接 upstream registry，也不会替换 Aether 的
实现。可信 push 和 release 构建还会初始化 `AetherInternal`；fork 或 Dependabot
PR 无法取得私有 SSH key，因此仍然使用公开 fallback，这是凭据边界而不是功能开关。

每次构建前，CI 会通过 HTTPS 解析 `wamsoft/krkrz_dev` 的 `master`，并检查父仓库
gitlink 与 `runtime/kirikiri/manifests/plugins.toml` 是否仍指向同一个“最新且已审核”
提交。如果 pin 过期，CI 会给出明确的同步提示并停止，不会静默编译旧源码或未经审核的
upstream 树。

完整产品构建会使用七个叶子适配器、layerExSave codec bridge、CLIP parser/writer
以及选定的 extNagano 算法，并在相同 target 上扩展 AetherInternal 兼容 provider。macOS Debug
构建方式如下：

```bash
git submodule update --init --recursive third_party/krkrz_dev
git submodule update --init --recursive packages/AetherInternal
AETHERKIRI_ENABLE_INTERNAL=ON ./build.sh macos debug --jobs=2
```

配置输出必须包含 `AetherInternal E-mote/Live2D package: enabled`。`build.sh`
会使用全新的 CMake cache 配置，因此之前缓存的
`AETHERKIRI_ENABLE_INTERNAL=OFF` 不会把下一次产品构建静默变成仅公开实现的产物。
如果构建提示 package 被显式关闭或不可用，该产物可以用于公开 fallback 测试，
但不能作为完整游戏兼容包交付。

如需隔离验证公开 fallback 与 krkrz adapter，请使用独立构建目录：

```bash
cmake -S . -B out/krkrz-public-debug \
  -DAETHERKIRI_ENABLE_INTERNAL=OFF \
  -DENABLE_TESTS=ON
cmake --build out/krkrz-public-debug --target krkr2plugin --parallel
ctest --test-dir out/krkrz-public-debug --output-on-failure
```

七个叶子适配器和 CLIP adapter 现在是强制的源码级集成：配置阶段会校验已初始化的固定版本
submodule，并始终编译 `upstream_bridge` 翻译单元。旧的
`AETHER_USE_KRKRZ_LEAF_PLUGINS` 源码选择项及其历史本地实现已经删除，因此只剩一条
实现路径，过期的 cache 值也不能再选中第二份代码。这不是运行时或面向游戏的开关：
产品只有一个插件 registry，hybrid provider 在 upstream 操作不能使用时会自动选择
Aether fallback。正常集成目标还提供 layerExSave codec bridge、共享 SQLite owner 和
CLIP 的 `clip://` storage。公开 fallback 与私有
AetherInternal 配置都受支持；后者是扩展目标，不会替换其余实现。

提交前至少执行：

```bash
python3 tools/plugin_manifest_report.py --strict
python3 tools/krkrz_core_audit.py
python3 tools/plugin_gap_audit.py
cmake --build <build-dir> --target krkr2plugin --parallel
ctest --test-dir <build-dir> --output-on-failure
```

当前 pinned checkout 已通过完整门禁：CTest 264/264，直接 Catch2 插件测试
227/227（3,124 条断言），Visual parity 294/294，Sound parity 28/28 且失败数为 0。
2026-08-30 已重新构建全开 macOS arm64 Debug 产品，并通过 deep code signature
校验。

如需聚焦检查 upstream core 算法，可启用隔离的 parity executable。它们只编译
upstream visual/sound 测试源码，绝不链接 `krkr2core`：

```bash
cmake -S . -B out/krkrz-parity \
  -DAETHER_BUILD_KRKRZ_CORE_PARITY=ON \
  -DAETHERKIRI_ENABLE_INTERNAL=OFF \
  -DENABLE_TESTS=ON
cmake --build out/krkrz-parity \
  --target aether_krkrz_visual_parity aether_krkrz_sound_parity --parallel
ctest --test-dir out/krkrz-parity \
  -R 'aether_krkrz_(visual|sound)_parity' --output-on-failure
```

这些 parity target 是消费 upstream core 的安全第一步：它们支持复用或移植一个已经
理解清楚的方法，但不意味着整套 upstream core 可以替换 Aether。未来的生产 adapter
必须保持同样的符号隔离，并增加 engine-level 行为测试，之后才能改变 core component
状态。

如需 agent 驱动调试，可创建一个私有目录并以 `-replfile=/绝对路径/repl` 启动。将
UTF-8 TJS 写入 `cmd.tmp` 后 rename 为 `cmd`，等待 `resp` 出现并删除后再发送下一条。
响应包含 `protocol`、`ok`、`kind`、`result`、`error`；超过 2 MiB 或非法 UTF-8 的命令
会被拒绝，不会执行不完整脚本。

submodule 的源码和 license notice 继续保留在 submodule 内。不要把源码复制到
`cpp/plugins`，也不要在这次集成中发布私有 AetherInternal 源码。
