# AetherKiri × krkrz_dev 集成说明

[English](krkrz_integration.md) | [简体中文](krkrz_integration.zh-CN.md)

本文是 AetherKiri 复用
[`wamsoft/krkrz_dev`](https://github.com/wamsoft/krkrz_dev) 的实现契约。目标是
在 upstream 业务逻辑兼容时复用其源码，同时由 AetherKiri 继续负责运行时 ABI、
插件注册、平台生命周期以及渲染/音频集成。

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
```

仓库 URL 有意保留维护者使用的 SSH 地址。公开 CI 只在这条命令中临时覆盖为
HTTPS：

```bash
git -c submodule.third_party/krkrz_dev.url=https://github.com/wamsoft/krkrz_dev.git \
  submodule update --init --recursive --depth 1 third_party/krkrz_dev
```

更新 upstream 源码需要同时修改两处：移动父仓 gitlink，并更新
[`runtime/kirikiri/manifests/plugins.toml`](../runtime/kirikiri/manifests/plugins.toml)
中的 `upstream_revision`。严格 manifest 检查会阻止把过期 checkout 当成已审核
版本。

## 集成层次

不引入 upstream 的独立 CMake 文件。Aether 的
[`cpp/plugins/CMakeLists.txt`](../cpp/plugins/CMakeLists.txt) 选择源码，
[`cpp/plugins/upstream_bridge`](../cpp/plugins/upstream_bridge) 中的小型翻译单元
先建立 Aether ABI，再包含 upstream 业务翻译单元。适配器规则见
[`krkrz_upstream_adapters.md`](krkrz_upstream_adapters.md)。

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
| `layerExSave.dll` | hybrid | Aether 存储/线程桥接；upstream encoder 作为参考输入 |
| `extNagano.dll` | hybrid | Aether provider 与确定性 fallback；不宣称 upstream 效果完全一致 |
| `KAGParserEx.dll` | hybrid | Aether 单一 parser 实现，upstream 语义作为参考 |
| `AlphaMovie.dll` | hybrid | Aether FFmpeg/队列/Godot pipeline；upstream codec 作为参考输入 |

其余可移植模块（`scriptsEx`、`json`、`csvParser`、`lineParser`、`saveStruct`、
SQLite/VFS、`xp3filter`、`motionplayer` 以及 Aether GPU/Live2D bridge）继续由
Aether 维护。包括 stub 和 optional 模块在内的权威机器可读状态都在 manifest 中。

## KAG/TJS 脚本边界

KAG3、KAG3_Ham、Krkr2Compat、Sample 以及 upstream TJS2 测试集已经作为递归
submodule 放在 `third_party/krkrz_dev/script` 下。它们的路径、nested gitlink
revision 和入口点记录在 manifest 的 `[[script_components]]` 中，并由
`plugin_manifest_report.py --strict` 校验。这样可以保持 upstream 脚本的单一来源，
不把第二份脚本 vendoring 到 runtime。

产品 Demo 是有意保留的独立 fixture：它包含翻译文本和 Aether 专用 polyfill，
因此不会被 upstream KAG3 树静默替换。`Krkr2Compat` 和 `KAG3_Ham` 是参考/测试
输入，不会默认注入运行时。未来如果生成 archive 和兼容性测试契约固定，可以让
脚本 stage target 直接消费 submodule。

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

* **Visual SIMD：** `cpp/core/visual/simd` 中 Aether 的 Highway/函数指针分发仍是
  生产实现。upstream SSE2/AVX2/NEON 文件只作为算法和 parity 输入；整包导入会
  重复 `tvpgl` 符号和 CPU dispatch 状态。
* **Sound DSP：** `cpp/core/sound` 与 `cpp/core/utils` 负责公开 sound ABI 和默认
  实现。upstream `MathAlgorithms`、`RealFFT` 与 phase-vocoder SIMD 代码只有在
  重命名或 namespace 隔离，并通过 upstream 相对/绝对误差 parity 测试后，才能
  进一步采用。
* **DAP debugger：** upstream `tjsDebuggerCore`、hook/symbol 文件及 `DAPServer`
  标记为 `optional`。它们需要先适配 Aether 的 VM hook、线程生命周期、socket
  所有权和 host event loop 才能链接。upstream `dap_smoke.py` 只是未来验收测试，
  不能证明 Aether 当前已经暴露 DAP。

这里的区别是有意的：“算法可复用”不等于“可以作为第二套 core 安全链接”。

## Optional 插件与 Stub

开启 `AETHER_USE_KRKRZ_OPTIONAL_PLUGINS=ON` 会验证固定源码中是否存在
`layerExVector`、`krkr_richtext`、`krkreffekseer` 和 `krkrthreepp`；不会链接这些
依赖 SDK 的插件。每个插件都需要专用 adapter、依赖策略和运行时测试，之后才能
把 manifest 状态从 `optional` 改掉。

兼容性注册位于 [`cpp/plugins/stubs`](../cpp/plugins/stubs)，规则见
[`krkrz_plugin_stubs.md`](krkrz_plugin_stubs.md)。Stub 可以保留模块名或脚本形状，
但不是功能完整的 native 支持。`sigcheck` 在平台/安全契约审核完成前，继续与
upstream 实现分离。

## 构建与验证

默认构建会使用七个叶子适配器，并要求 submodule 已初始化：

```bash
cmake -S . -B out/krkrz-debug \
  -DAETHER_USE_KRKRZ_LEAF_PLUGINS=ON \
  -DAETHERKIRI_ENABLE_INTERNAL=OFF \
  -DENABLE_TESTS=ON
cmake --build out/krkrz-debug --target krkr2plugin --parallel
ctest --test-dir out/krkrz-debug --output-on-failure
```

使用 `-DAETHER_USE_KRKRZ_LEAF_PLUGINS=OFF` 可以编译历史 Aether 实现，同时仍保留
submodule 和 manifest。公开 fallback 与私有 AetherInternal 配置都受支持；后者是
扩展目标，不会替换其余实现。

提交前至少执行：

```bash
python3 tools/plugin_manifest_report.py --strict
python3 tools/plugin_gap_audit.py
cmake --build <build-dir> --target krkr2plugin --parallel
ctest --test-dir <build-dir> --output-on-failure
```

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

这些 parity target 是消费 upstream core 的安全第一步。未来的生产 adapter 必须
保持同样的符号隔离，并增加 engine-level 行为测试，之后才能改变 core component
状态。

submodule 的源码和 license notice 继续保留在 submodule 内。不要把源码复制到
`cpp/plugins`，也不要在这次集成中发布私有 AetherInternal 源码。
