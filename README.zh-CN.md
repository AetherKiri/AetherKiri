<p align="center">
  <img src="apps/godot_app/assets/icon.png" width="112" alt="AetherKiri 应用图标">
</p>

<h1 align="center">AetherKiri</h1>

<p align="center">
  一个由 Godot 承载、以 C++ 引擎核心驱动的 KiriKiri2 运行时。
</p>

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh-CN.md">简体中文</a>
</p>

<p align="center">
  <a href="https://github.com/AetherKiri/AetherKiri/actions/workflows/macos.yml"><img alt="macOS Build" src="https://github.com/AetherKiri/AetherKiri/actions/workflows/macos.yml/badge.svg"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/actions/workflows/ios.yml"><img alt="iOS Build" src="https://github.com/AetherKiri/AetherKiri/actions/workflows/ios.yml/badge.svg"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/actions/workflows/android.yml"><img alt="Android Build" src="https://github.com/AetherKiri/AetherKiri/actions/workflows/android.yml/badge.svg"></a>
</p>

<p align="center">
  <a href="https://github.com/AetherKiri/AetherKiri/blob/main/LICENSE"><img alt="GitHub License" src="https://img.shields.io/github/license/AetherKiri/AetherKiri?logo=gnu&label=license"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/commits/main"><img alt="GitHub Last Commit" src="https://img.shields.io/github/last-commit/AetherKiri/AetherKiri?logo=github"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/issues"><img alt="GitHub Issues" src="https://img.shields.io/github/issues/AetherKiri/AetherKiri?logo=github"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/pulls"><img alt="GitHub Pull Requests" src="https://img.shields.io/github/issues-pr/AetherKiri/AetherKiri?logo=github"></a>
  <a href="https://github.com/AetherKiri/AetherKiri"><img alt="GitHub Repository Size" src="https://img.shields.io/github/repo-size/AetherKiri/AetherKiri?logo=github"></a>
  <a href="https://github.com/AetherKiri/AetherKiri"><img alt="GitHub Top Language" src="https://img.shields.io/github/languages/top/AetherKiri/AetherKiri?logo=github"></a>
</p>

## 项目概览

AetherKiri 用 Godot 4.6 作为应用外壳，在其中运行 KiriKiri2 内容。项目由
C++17 引擎核心、C ABI 桥接层和 Godot GDExtension 宿主组成；Godot 侧负责
产品 UI、渲染资源、设置页、导出配置和平台打包。

默认产品渲染链路是 **Godot Native**：引擎帧通过 Godot 持有的
`RenderingDevice` 资源输出。**GPU Bridge** 保留为显式可选的兼容和性能对照
后端，用于将外部 native GPU render target 导入 Godot。**Debug CPU** 只作为
可见的诊断 fallback，不作为性能验收目标。

```text
Godot App Shell
  -> GDExtension Host
    -> C ABI Engine API
      -> C++ Engine Core
        -> KiriKiri Runtime / Plugins
```

## 亮点

- Godot 4.6 应用外壳，使用原生 GDExtension 集成。
- C++17 KiriKiri2 运行时核心，覆盖视觉、音频、存储、VM 和插件支持。
- 已接入 macOS、iOS/iPadOS 和 Android 导出链路。
- 可在运行时选择渲染后端，并持久化设置。
- 提供 smoke、渲染、交互、性能和手动复现 probe 脚本。
- 以 GPL-3.0-or-later 分发源码。

## 仓库结构

| 路径 | 用途 |
| --- | --- |
| `apps/godot_app/` | Godot 项目、场景、设置 UI、性能/日志面板、图标和导出配置。 |
| `bridge/godot_extension/` | Godot 原生宿主库入口。 |
| `bridge/engine_api/` | 宿主层驱动 C++ 引擎的 C ABI。 |
| `cpp/core/` | KiriKiri2 运行时、视觉系统、音频、存储、VM 和插件支持。 |
| `cpp/plugins/` | 内置 native 插件实现和兼容 stub。 |
| `tests/profiles/` | 单游戏 probe profile。提交到仓库的 profile 不能包含机器本地路径。 |
| `tools/` | 不参与 iOS/Android 目标构建的开发和兼容工具。 |
| `doc/development.zh-CN.md` | 完整开发文档，覆盖架构、文件作用、构建、测试、probe 和调试。 |

## 渲染后端

| 后端 | 作用 | 状态 |
| --- | --- | --- |
| Godot Native | Godot-owned GPU 渲染路径。 | 默认产品链路 |
| GPU Bridge | 外部 GPU render target bridge，用于对照和兼容。 | 可选后端 |
| Debug CPU | RGBA readback/upload fallback。 | 仅用于调试 |

Godot 设置页会持久化所选后端。游戏运行中切换后端时会提示需要重启当前游戏
会话，因为渲染资源必须重新创建。

## 图标与资源

页首展示的图标就是 Godot 项目实际配置的应用图标：

- 应用图标：`apps/godot_app/assets/icon.png`
- Godot 项目使用的 SVG 源：`apps/godot_app/assets/icon.svg`
- 导出图标集合：`apps/godot_app/assets/icons/`
- 设计源资源：`assets/sharks.svg` 和 `assets/apple_icon_mask.svg`

iOS 和 Android 导出配置会引用 `apps/godot_app/assets/icons/` 下的生成 PNG
尺寸，包括 App Store 图标和启动器图标。

## 环境要求

- CMake 3.28+
- Ninja
- vcpkg，位于 `.devtools/vcpkg` 或通过 `VCPKG_ROOT` 指定
- Godot 位于 `/Applications/Godot.app`，或通过 `GODOT_BIN=/path/to/Godot` 指定
- macOS/iOS 导出需要 Xcode
- Android 导出需要 Android SDK/NDK

Android 构建会优先使用 `ANDROID_HOME` 或 `ANDROID_SDK_ROOT`，否则使用
`$HOME/Library/Android/sdk`，并自动选择已安装的最新 NDK。

## 构建

常用构建命令：

```bash
./build.sh macos debug
./build.sh macos release
./build.sh ios debug --simulator
./build.sh ios release
./build.sh android debug --abi=arm64-v8a
./build.sh android release --abi=arm64-v8a
```

脚本会构建 native engine 和 Godot host library，将产物放到
`apps/godot_app/bin/`，并在 Godot 可用时运行对应的 Godot export preset。
Android 当前只接入了 `arm64-v8a`。

## 运行和测试构建产物

### macOS

构建并启动导出的 App：

```bash
./build.sh macos release
open out/godot/macos/release/AetherKiri.app
```

如果需要从终端查看 debug 日志：

```bash
./build.sh macos debug
out/godot/macos/debug/AetherKiri.app/Contents/MacOS/AetherKiri
```

可以通过 App UI 添加游戏；也可以仅对当前运行传入本地测试游戏：

```bash
AETHERKIRI_GAME_PATH="/path/to/game" \
out/godot/macos/debug/AetherKiri.app/Contents/MacOS/AetherKiri
```

### iOS 模拟器

构建模拟器导出：

```bash
./build.sh ios debug --simulator
```

之后可以打开生成的 Xcode 工程运行，或在 Xcode 构建出 `.app` 后用
`simctl` 安装：

```bash
xcrun simctl boot "iPad Pro 11-inch (M4)"
xcrun simctl install booted /path/to/AetherKiri.app
xcrun simctl launch booted com.example.aetherkiri
```

bundle identifier 取决于 export preset 和签名配置。

### iOS 真机

构建 iOS 导出工程：

```bash
./build.sh ios release
```

然后用 Xcode 或命令行构建：

```bash
xcodebuild \
  -project out/godot/ios/release/AetherKiri.xcodeproj \
  -scheme AetherKiri \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  -allowProvisioningUpdates \
  build
```

Xcode 生成 `AetherKiri.app` 后，安装到已配对设备：

```bash
xcrun devicectl list devices
xcrun devicectl device install app \
  --device <device-identifier> \
  /path/to/AetherKiri.app
```

iOS/iPadOS 上通过“文件”App 将游戏复制到：

```text
我的 iPhone/iPad -> AetherKiri -> Games
```

回到 AetherKiri 后点击刷新。

### Android

构建 debug APK：

```bash
./build.sh android debug --abi=arm64-v8a
```

APK 输出到：

```text
out/godot/android/debug/AetherKiri-debug.apk
```

安装并启动到已连接设备或模拟器：

```bash
adb install -r out/godot/android/debug/AetherKiri-debug.apk
adb shell monkey -p org.github.krkr2.aetherkiri \
  -c android.intent.category.LAUNCHER 1
```

构建 release APK：

```bash
./build.sh android release --abi=arm64-v8a
```

Release APK 输出到：

```text
out/godot/android/release/AetherKiri-release.apk
```

release preset 在配置项目发布 keystore 之前会保持未签名。安装或分发前需要签名：

```bash
apksigner sign --ks /path/to/release.keystore \
  out/godot/android/release/AetherKiri-release.apk
```

Android 上，如果平台允许访问文件系统，可通过 App UI 导入游戏；受限设备可将游戏目录复制到
App 的 documents/storage 位置后点击刷新。

## 验证

迁移检查：

```bash
rg "F[l]utter|f[l]utter|A[N]GLE|Platform[ ]Graphics" README.md README.zh-CN.md apps bridge build CMakeLists.txt
rg "u[n]official-angle|l[i]bEGL|l[i]bGLESv2" CMakeLists.txt bridge cpp build vcpkg.json
./build.sh macos debug
./build.sh ios debug --simulator
./build.sh android debug --abi=arm64-v8a
build/validate_godot_native.sh
build/validate_gpu_bridge.sh
```

Godot 脚本检查：

```bash
/Applications/Godot.app/Contents/MacOS/Godot \
  --headless \
  --path apps/godot_app \
  --check-only \
  --quit
```

## 单游戏测试 Profile

Probe 脚本可以通过 `AETHERKIRI_TEST_CONFIG` 读取配置。提交到仓库的 profile
必须保持通用，不能提交本地绝对游戏路径。机器本地路径请通过
`AETHERKIRI_SMOKE_GAME` 传入，或创建未跟踪的本地 profile。

Smoke test 示例：

```bash
AETHERKIRI_TEST_CONFIG="$PWD/tests/profiles/kr37s.json" \
AETHERKIRI_SMOKE_GAME="/path/to/game" \
/Applications/Godot.app/Contents/MacOS/Godot \
  --path apps/godot_app \
  --script res://scripts/smoke_test.gd
```

渲染/交互 probe 示例：

```bash
AETHERKIRI_TEST_CONFIG="$PWD/tests/profiles/kr37s.json" \
AETHERKIRI_SMOKE_GAME="/path/to/game" \
/Applications/Godot.app/Contents/MacOS/Godot \
  --path apps/godot_app \
  --script res://scripts/step_render_probe.gd
```

手动渲染 probe，可用于点按复现问题：

```bash
AETHERKIRI_TEST_CONFIG="$PWD/tests/profiles/kr37s.json" \
AETHERKIRI_SMOKE_GAME="/path/to/game" \
/Applications/Godot.app/Contents/MacOS/Godot \
  --path apps/godot_app \
  --script res://scripts/manual_render_probe.gd
```

手动 probe 会把鼠标、滚轮、触控和键盘输入转发给游戏。按 `F12` 保存
`/tmp/aetherkiri-manual-*.png`，按 `Esc` 退出。

Profile 字段：

- `game_path`: 可选游戏目录或 XP3 路径。提交的 profile 中应保持为空，除非路径可移植。
- `backend`: 渲染后端，通常是 `Godot Native`。
- `surface_size`: 引擎渲染 surface，例如 `[1280, 720]`。
- `window_size`: probe 窗口尺寸。
- `coord_size`: 录制点击坐标所使用的坐标空间。
- `startup_timeout_frames`、`warmup_frames`、`after_click_frames`、
  `measure_frames`: 时序参数。
- `clicks`: 有序交互步骤，每个步骤包含 `name`、`x`、`y` 和可选的
  `after_frames`。
- `perf_input`: `perf_input_probe.gd` 的兼容参数。

验收要求包括启动、渲染、输入、菜单操作、音频、存档路径、干净退出，以及
Godot Native 或 GPU Bridge 达到性能目标。Debug CPU 只作为诊断 fallback。

## 文档

- 开发文档：`doc/development.zh-CN.md`
- 插件说明：`doc/krkr2_plugins.md`
- 工具说明：`tools/README.md`

## 许可证

AetherKiri 以 GPL-3.0-or-later 分发。完整许可证文本见 `LICENSE`。
