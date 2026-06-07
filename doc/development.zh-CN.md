# AetherKiri 开发文档

[English](development.md) | [简体中文](development.zh-CN.md)

本文是 AetherKiri 当前主要开发文档，覆盖 Godot-native 架构、目录结构、关键文件作用、构建、测试、自动化探针、平台注意事项和调试方法。

## 当前产品形态

AetherKiri 是一个由 Godot 托管的 KiriKiri2 运行时：

```text
Godot App Shell
  -> Godot 4.6 GDExtension Host
    -> C ABI Engine Bridge
      -> C++ Engine Core
        -> KiriKiri Runtime / Plugins
```

当前产品入口是 `apps/godot_app`。Flutter 和 ANGLE 已不属于当前产品架构。

## 渲染架构

Godot 设置页暴露三个渲染后端：

| 后端 | 作用 | 是否作为正式性能目标 |
| --- | --- | --- |
| Godot Native | 默认 Godot 原生 GPU 渲染路径。纹理和渲染资源由 Godot 持有，并直接显示在 Godot UI 中。 | 是 |
| GPU Bridge | 外部 native GPU render target 导入 Godot 的兼容/性能对照路径。 | 是，作为显式兼容后端 |
| Debug CPU | RGBA readback/upload 调试兜底，用于排查 GPU 路径正确性。 | 否 |

CPU 上传只能作为 debug fallback。性能优化应优先落到 Godot Native，其次是 GPU Bridge。

## 目录结构

| 路径 | 作用 |
| --- | --- |
| `apps/godot_app/` | Godot 项目、场景、脚本、资源、导出配置和 GDExtension 描述文件。 |
| `bridge/godot_extension/` | 暴露给 Godot 的 C++ GDExtension 类和 host/player 绑定。 |
| `bridge/engine_api/` | Godot host 与 C++ engine core 之间的稳定 C ABI。这个边界应保持窄且可版本化。 |
| `cpp/core/` | KiriKiri 运行时核心：脚本 VM、存储、事件、窗口/图层系统、渲染、音频、视频和插件基础设施。 |
| `cpp/plugins/` | 内置插件实现和兼容适配模块，以 KiriKiri 插件名注册。 |
| `build/` | 各平台构建脚本和验证脚本。 |
| `tests/` | 单元测试、fixture 和通用 probe profile。提交的 profile 不能包含本机绝对游戏路径。 |
| `tools/` | 开发工具，例如 XP3 工具和插件审计工具。 |
| `vcpkg/` | 项目使用的本地 vcpkg ports 和 triplets。 |
| `.devtools/vcpkg/` | 可选本地 vcpkg checkout，已被忽略，可重新生成。 |
| `docs/` | GitHub Pages 站点文件，不是工程开发文档目录。 |
| `doc/` | 工程开发文档。开发说明从这里开始读。 |

## 关键文件作用

| 文件 | 作用 |
| --- | --- |
| `build.sh` | 统一构建入口，根据平台分发到 `build/` 下的脚本。 |
| `build/build_macos.sh` | 构建 macOS C++ core/GDExtension，复制 dylib，并导出 Godot macOS app。 |
| `build/build_ios.sh` | 构建 iOS 真机或模拟器静态库，导出并 patch Xcode 工程。 |
| `build/build_android.sh` | 构建 Android native 库，并通过 Godot 导出 APK。 |
| `build/build_web.sh` | 构建 Emscripten Web GDExtension side module，并在 dlink 模板可用时导出 Godot Web app。 |
| `CMakeLists.txt` | 顶层 native build，组织 engine API、GDExtension、core、plugins、tests 和 tools。 |
| `CMakePresets.json` | macOS、iOS、Android、Web 等平台的 CMake preset 和输出目录。 |
| `vcpkg.json` | native 依赖清单。Godot Native 默认路径不能依赖 ANGLE。 |
| `apps/godot_app/project.godot` | Godot 项目设置和默认输入/导出配置。不要提交本地游戏路径。 |
| `apps/godot_app/export_presets.cfg` | Godot 导出 preset。签名相关配置可能需要本地覆盖。 |
| `apps/godot_app/aether_kiri.gdextension` | GDExtension 在各平台 debug/release 的 native library 映射。 |
| `apps/godot_app/scenes/main.tscn` | 主场景。当前大部分 UI 由脚本动态构建。 |
| `apps/godot_app/scripts/main.gd` | App shell 主逻辑：主页、设置、游戏详情、加载控制台、渲染后端选择、输入转发、性能面板。 |
| `apps/godot_app/scripts/probe_config.gd` | probe/test 配置加载器，读取 `AETHERKIRI_TEST_CONFIG` 和环境变量覆盖。 |
| `apps/godot_app/scripts/smoke_test.gd` | 基础引擎启动冒烟 probe。 |
| `apps/godot_app/scripts/step_render_probe.gd` | 按步骤点击并截图的渲染 probe。 |
| `apps/godot_app/scripts/perf_input_probe.gd` | 点击/文字行为的输入性能 probe。 |
| `apps/godot_app/scripts/sequence_perf_probe.gd` | 较长流程 FPS probe。 |
| `apps/godot_app/scripts/gui_render_probe.gd` | GUI 渲染截图 probe。 |
| `apps/godot_app/scripts/gpu_blend_self_test.gd` | Godot GPU blend 自测入口。 |
| `bridge/godot_extension/src/aether_kiri_godot.cpp` | Godot 可见的 `AetherKiriPlayer` 实现和方法绑定。 |
| `bridge/engine_api/include/engine_api.h` | engine bridge 导出的 C ABI。 |
| `bridge/engine_api/include/engine_options.h` | host 与 engine 共享的 option key/value。 |
| `bridge/engine_api/src/engine_api.cpp` | C ABI 实现，负责创建、打开、tick、render 和输入传递。 |
| `cpp/core/environ/EngineLoop.*` | 运行时生命周期、tick loop，以及 host input 到 TVP 事件的转换。 |
| `cpp/core/visual/LayerManager.*` | 图层命中测试、焦点/capture、鼠标/触摸/键盘派发和兼容输入逻辑。 |
| `cpp/core/visual/impl/DrawDevice.*` | Window/layer 更新与 render manager 之间的 draw device 桥。 |
| `cpp/core/visual/impl/LayerBitmapImpl.*` | Bitmap/layer 像素操作和文字绘制。 |
| `cpp/core/visual/godot/` | Godot Native render manager、texture 和后端代码。具体文件随分支演进。 |
| `cpp/core/plugin/PluginImpl.cpp` | 内部 KiriKiri 插件模块注册和加载路径。 |
| `cpp/plugins/CMakeLists.txt` | 插件构建接线。添加或启用插件时通常需要修改这里。 |
| `tests/profiles/kr37s.json` | 通用 probe profile，`game_path` 故意为空。本地路径通过环境变量传入。 |
| `doc/krkr2_plugins.md` | KiriKiri2 插件名和来源位置参考表。 |

## 构建前置条件

常规开发需要：

- macOS 和 Xcode command line tools，用于 macOS/iOS 构建。
- CMake 3.28+ 和 Ninja。
- Godot 4.6.3 stable，默认路径 `/Applications/Godot.app`，也可以设置 `GODOT_BIN`。
- 对应平台的 Godot export templates。
- `.devtools/vcpkg` 或 `VCPKG_ROOT`。

Android 还需要：

- Android SDK 和 NDK。
- `ANDROID_HOME` 或 `ANDROID_SDK_ROOT`。未设置时脚本会尝试 `$HOME/Library/Android/sdk`。

Web 还需要：

- Emscripten/emsdk，并确保 `emcc`、`em++`、`emar` 在 `PATH` 中。
- Godot Web GDExtension/dlink export templates，放在 Godot export template
  目录下并命名为 `web_dlink_debug.zip` 和
  `web_dlink_release.zip`。
- 本地 Web dev server 使用 TypeScript/Vite，需要 Node.js 和 npm。

常用环境变量：

| 变量 | 作用 |
| --- | --- |
| `GODOT_BIN` | 覆盖 Godot 可执行文件路径。 |
| `GODOT_EXPORT_TEMPLATE` | 覆盖平台 export template zip。 |
| `GODOT_TEMPLATE_DIR` | 覆盖 Godot export template 目录，Web dlink 模板常用。 |
| `VCPKG_ROOT` | 覆盖 vcpkg checkout。 |
| `EMSDK` / `EMSCRIPTEN_ROOT` | Web 构建使用的 Emscripten SDK/toolchain 位置。 |
| `AETHERKIRI_GAME_ROOT` / `AETHERKIRI_GAME_ROOTS` | 仅 Vite 本地 Web 调试使用，只读 RangeFS 游戏根目录。 |
| `AETHERKIRI_WEB_AUTO_START` | 仅 Vite 本地 Web 调试使用，启动后自动挂载并进入配置的游戏。 |
| `JOBS` | native 并行构建任务数。 |
| `IOS_SIMULATOR_ARCH` | iOS 模拟器架构，`arm64` 或 `x86_64`。 |
| `AETHERKIRI_TEST_CONFIG` | JSON probe profile 路径。 |
| `AETHERKIRI_SMOKE_GAME` | 本地游戏路径。不要写入提交的 profile。 |
| `AETHERKIRI_RENDER_BACKEND` | probe 渲染后端覆盖。 |
| `AETHERKIRI_INPUT_TRACE` | 开启 engine/layer 输入 trace。 |
| `AETHERKIRI_PERF_LOG_INTERVAL` | 性能日志输出间隔，单位秒。 |
| `AETHERKIRI_FRAME_SPIKE_MS` | 超过该阈值的慢帧会被记录。 |
| `AETHERKIRI_VERBOSE_RENDER_LOG` | 开启 Godot shell 侧 verbose render log。 |

## 构建命令

统一格式是 `./build.sh <platform> <debug|release>`。

```bash
./build.sh macos debug
./build.sh macos release
./build.sh ios debug --simulator
./build.sh ios debug --simulator --simulator-arch=arm64
./build.sh ios release
./build.sh android debug --abi=arm64-v8a
./build.sh android release --abi=arm64-v8a
./build.sh web debug
./build.sh web release
```

清理某个平台构建：

```bash
./build.sh --clean macos release
```

## 平台构建和启动

macOS Debug：

```bash
./build.sh macos debug
out/godot/macos/debug/AetherKiri.app/Contents/MacOS/AetherKiri
```

macOS Release：

```bash
./build.sh macos release
open -n out/godot/macos/release/AetherKiri.app
```

iOS 模拟器：

```bash
./build.sh ios debug --simulator --simulator-arch=arm64
xcodebuild \
  -project out/godot/ios-simulator/debug/AetherKiri.xcodeproj \
  -scheme AetherKiri \
  -configuration Debug \
  -destination 'platform=iOS Simulator,name=iPad Pro 11-inch (M4)' \
  build
```

iOS 真机：

```bash
./build.sh ios release
xcodebuild \
  -project out/godot/ios/release/AetherKiri.xcodeproj \
  -scheme AetherKiri \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  -allowProvisioningUpdates \
  CODE_SIGN_IDENTITY='Apple Development' \
  build
xcrun devicectl device install app \
  --device <device-identifier> \
  /path/to/AetherKiri.app
```

Android Debug：

```bash
./build.sh android debug --abi=arm64-v8a
adb install -r out/godot/android/debug/AetherKiri-debug.apk
adb shell monkey -p org.github.krkr2.aetherkiri \
  -c android.intent.category.LAUNCHER 1
```

Android Release：

```bash
./build.sh android release --abi=arm64-v8a
```

release APK 默认未配置正式签名。安装或分发前需要用 release keystore 签名。

Web Debug：

```bash
source /path/to/emsdk/emsdk_env.sh
./build.sh web debug
npm install
npm run web:dev:debug
```

Web Release：

```bash
source /path/to/emsdk/emsdk_env.sh
npm install
npm run web:build:release
npm run web:dev:release
```

原生 side module 输出到
`apps/godot_app/bin/web/<debug|release>/aether_kiri_godot.wasm`。完整的 Godot
Web 导出产物在 `out/godot/web/<debug|release>/`，浏览器入口是 `index.html`。
正式发布时把 `out/godot/web/release/` 里的内容上传到静态服务器或 CDN。

Web 导出按线程 + wasm SIMD 优先构建，浏览器部署时需要跨源隔离相关 HTTP 头；
本地测试默认由 TypeScript/Vite dev server 提供这些头。生产服务器需要给
HTML、JavaScript、wasm、pck 和资源文件返回：

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Resource-Policy: same-origin
```

同时需要配置 `.wasm` 的 MIME 为 `application/wasm`，`.pck` 为
`application/octet-stream`。Web 当前通过 `cpp/core/environ/web/Platform.cpp`
里的保守 platform shim 使用 Emscripten 虚拟文件系统。云端 Web 版不能依赖服务器
环境变量读取用户电脑上的游戏；正式导入路径是浏览器文件/目录选择器，用户授权后将
本地 `File`/`Blob` 对象以只读 Range FS 挂载到 `/webgames/<id>`。这样 2-3G 游戏包
不会进入 `copyToFS` 或 MEMFS。存档、游戏配置和其他运行时写入会持久化到当前站点
IndexedDB 支持的 `/userfs`。

本地自动化调试可用
`AETHERKIRI_GAME_ROOT=/absolute/path AETHERKIRI_WEB_AUTO_START=1 npm run web:dev:release`。
多个根目录可配合 `AETHERKIRI_WEB_AUTO_START_INDEX` 或
`AETHERKIRI_WEB_AUTO_START_NAME` 选择自动启动项。这些变量只作为 Vite 本地调试挂载，
不能作为云端部署方案。

iOS/iPadOS 上通过“文件”App 把游戏复制到：

```text
我的 iPad/iPhone -> AetherKiri -> Games
```

回到 AetherKiri 后点击刷新。

## 运行时 UI 流程

Godot shell 对齐旧移动端 app 的主流程：

1. 首页扫描已知游戏。
2. iOS/iPadOS 上用户通过“文件”App 复制游戏，然后点击刷新。
3. 支持文件系统的平台上，主操作可以导入/选择目录或包。
4. 游戏详情页显示路径、游玩时间、封面/刮削、重命名、移除和启动操作。
5. 启动后进入整页加载控制台。新日志打印时应自动滚动到底部。
6. 游戏启动完成后隐藏 shell，游戏 viewport 全屏显示。
7. 设置页持久化渲染后端、性能面板、帧率限制、横屏锁定和开发者开关。

## 输入链路

Godot 输入在 `apps/godot_app/scripts/main.gd` 中处理并转发给 `AetherKiriPlayer`，随后通过 `EngineLoop` 转换成 TVP input events。

重要规则：

- 桌面鼠标事件可以做少量 forced tick，让文字逐字显示时的点击响应更稳定。
- 移动端 touch 不能再同时转发 Godot 生成的兼容 mouse 事件，否则一次触摸会变成两次 KiriKiri 点击。
- 移动端 touch 默认依赖正常帧循环推进，不应无理由额外 forced tick。
- 存读档/鉴赏等兼容性 Enter fallback 放在 `LayerManager.cpp` 中，并且只能作用于可见、启用的选择层。

## 测试 Profile 和 Probe

提交到仓库的 profile 位于 `tests/profiles/`。它们必须可移植，不能包含本机绝对路径。本地游戏路径用环境变量传入：

```bash
AETHERKIRI_TEST_CONFIG="$PWD/tests/profiles/kr37s.json" \
AETHERKIRI_SMOKE_GAME="/path/to/game" \
/Applications/Godot.app/Contents/MacOS/Godot \
  --path apps/godot_app \
  --script res://scripts/smoke_test.gd
```

逐步截图 probe：

```bash
AETHERKIRI_TEST_CONFIG="$PWD/tests/profiles/kr37s.json" \
AETHERKIRI_SMOKE_GAME="/path/to/game" \
/Applications/Godot.app/Contents/MacOS/Godot \
  --path apps/godot_app \
  --script res://scripts/step_render_probe.gd
```

输入/性能 probe：

```bash
AETHERKIRI_TEST_CONFIG="$PWD/tests/profiles/kr37s.json" \
AETHERKIRI_SMOKE_GAME="/path/to/game" \
out/godot/macos/debug/AetherKiri.app/Contents/MacOS/AetherKiri \
  --script res://scripts/perf_input_probe.gd
```

常用 profile 字段：

| 字段 | 作用 |
| --- | --- |
| `game_path` | 可选可移植游戏路径。提交版本建议为空。 |
| `backend` | 渲染后端，通常为 `Godot Native`。 |
| `surface_size` | 引擎渲染尺寸，例如 `[1280, 720]`。 |
| `window_size` | probe 窗口尺寸。 |
| `coord_size` | 点击坐标使用的坐标空间。 |
| `startup_timeout_frames` | 启动等待帧数上限。 |
| `warmup_frames` | 启动成功后测量前等待帧数。 |
| `after_click_frames` | 每次点击后的默认等待帧数。 |
| `measure_frames` | FPS 测量窗口。 |
| `clicks` | 有序点击步骤，包含 `x`、`y`，可选 `name` 和 `after_frames`。 |
| `perf_input` | 输入延迟 probe 的额外点击/帧数配置。 |

## 验证清单

提交 engine/render/input 改动前，按影响范围运行必要检查：

```bash
/Applications/Godot.app/Contents/MacOS/Godot \
  --headless \
  --path apps/godot_app \
  --check-only \
  --quit
```

构建检查：

```bash
./build.sh macos debug
./build.sh macos release
./build.sh ios debug --simulator
./build.sh ios release
./build.sh android debug --abi=arm64-v8a
./build.sh web debug
```

渲染迁移检查：

```bash
rg "F[l]utter|f[l]utter|A[N]GLE|Platform[ ]Graphics" \
  README.md README.zh-CN.md apps bridge build CMakeLists.txt
rg "u[n]official-angle|l[i]bEGL|l[i]bGLESv2" \
  CMakeLists.txt bridge cpp build vcpkg.json
build/validate_godot_native.sh
build/validate_gpu_bridge.sh
```

手动游戏冒烟：

- 打开游戏。
- 进入标题页。
- 新游戏或读取存档。
- 验证鼠标/触摸输入。
- 验证逐字显示和点击补全文字。
- 打开菜单。
- 验证音频。
- 存档一次。
- 退出回 shell。
- 查看 FPS 和慢帧日志。

## 调试说明

常用 trace：

```bash
AETHERKIRI_INPUT_TRACE=1 ...
AETHERKIRI_FRAME_SPIKE_MS=20 ...
AETHERKIRI_VERBOSE_RENDER_LOG=1 ...
```

iOS 真机：

```bash
xcrun devicectl device process launch \
  --device <device-identifier> \
  --terminate-existing \
  --console \
  --environment-variables '{"AETHERKIRI_INPUT_TRACE":"1"}' \
  com.liuyu.aetherkiri.kr37s
```

`--console` 会等待进程运行结束，只在需要收集日志时使用，避免长时间占用终端。

## 代码修改原则

- Godot app 是当前正式产品 shell。
- 不要把本地测试游戏路径写入提交的项目设置或 profile。
- 优先做后端内聚的修复，避免无边界修改 engine core 行为。
- 不要把性能路径静默降级到 Debug CPU。
- `bridge/engine_api` 是稳定边界，接口应保持窄。
- 插件兼容只能实现真实行为，或明确记录为未实现；不要用假成功掩盖缺口。
- 保留用户工作区改动，不要 reset 或 revert 无关文件。
