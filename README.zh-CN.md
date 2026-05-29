# AetherKiri

[English](README.md) | [简体中文](README.zh-CN.md)

AetherKiri 是一个由 Godot 承载的 KiriKiri2 运行时。项目使用 C++ 引擎核心，
通过 Godot 4.6 GDExtension 加载，并以 Godot 原生渲染作为默认产品链路。

## 架构

```text
Godot App Shell
  -> GDExtension Host
    -> C++ Engine Core
      -> KiriKiri Runtime / Plugins
```

默认渲染器是 **Godot Native**，通过 Godot App 持有的
`RenderingDevice` 资源完成渲染。**GPU Bridge** 保留为显式可选的兼容和
性能对照后端，用于将外部 native GPU render target 导入 Godot。
**Debug CPU** 只作为可见的调试 fallback，不作为性能验收目标。

## 仓库结构

- `apps/godot_app/` - Godot 项目、场景、设置 UI、性能/日志面板和导出配置。
- `bridge/godot_extension/` - Godot 原生宿主库入口。
- `bridge/engine_api/` - 宿主层驱动 C++ 引擎的 C ABI。
- `cpp/core/` - KiriKiri2 运行时、视觉系统、音频、存储、VM 和插件支持。
- `cpp/plugins/` - 内置 native 插件实现和兼容 stub。
- `tests/profiles/` - 单游戏测试 profile。提交到仓库的 profile 不能包含机器本地路径。

## 渲染后端

| 后端 | 用途 |
| --- | --- |
| Godot Native | 默认的 Godot-owned GPU 渲染路径。 |
| GPU Bridge | 外部 GPU render target bridge，用于对照和兼容。 |
| Debug CPU | RGBA readback/upload 调试 fallback。 |

Godot 设置页会持久化所选后端。游戏运行中切换后端时会提示需要重启当前游戏
会话，因为渲染资源必须重新创建。

## 构建

依赖：

- CMake 3.28+
- Ninja
- vcpkg，位于 `.devtools/vcpkg` 或通过 `VCPKG_ROOT` 指定
- Godot 位于 `/Applications/Godot.app`，或通过 `GODOT_BIN=/path/to/Godot` 指定
- macOS/iOS 导出需要 Xcode

常用构建：

```bash
./build.sh macos debug
./build.sh macos release
./build.sh ios debug --simulator
./build.sh ios release
```

脚本会构建 native engine 和 Godot host library，将产物放到
`apps/godot_app/bin/`，并在 Godot 可用时运行对应的 Godot export preset。

## 构建产物测试

### macOS App

构建并启动导出的 App：

```bash
./build.sh macos release
open out/godot/macos/release/AetherKiri.app
```

如果需要从终端看 debug 日志：

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

## 验证

迁移检查：

```bash
rg "F[l]utter|f[l]utter|A[N]GLE|Platform[ ]Graphics" README.md README.zh-CN.md apps bridge build CMakeLists.txt
rg "u[n]official-angle|l[i]bEGL|l[i]bGLESv2" CMakeLists.txt bridge cpp build vcpkg.json
./build.sh macos debug
./build.sh ios debug --simulator
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
