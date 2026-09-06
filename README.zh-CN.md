<p align="center">
  <img src="apps/godot_app/assets/icon.png" width="112" alt="AetherKiri 应用图标">
</p>

<h1 align="center">AetherKiri</h1>

<p align="center">
  一个由 Godot 承载、可扩展的多 Runtime 视觉小说平台。
</p>

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh-CN.md">简体中文</a>
</p>

<p align="center">
  <a href="https://github.com/AetherKiri/AetherKiri/actions/workflows/build.yml"><img alt="macOS Build" src="https://img.shields.io/github/actions/workflow/status/AetherKiri/AetherKiri/build.yml?branch=main&amp;job=Build%20macOS%20App&amp;label=macOS%20Build"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/actions/workflows/build.yml"><img alt="iOS Build" src="https://img.shields.io/github/actions/workflow/status/AetherKiri/AetherKiri/build.yml?branch=main&amp;job=Build%20iOS%20App&amp;label=iOS%20Build"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/actions/workflows/build.yml"><img alt="Android Build" src="https://img.shields.io/github/actions/workflow/status/AetherKiri/AetherKiri/build.yml?branch=main&amp;job=Build%20Android%20App&amp;label=Android%20Build"></a>
  <a href="https://github.com/AetherKiri/AetherKiri/actions/workflows/build.yml"><img alt="Web Build" src="https://img.shields.io/github/actions/workflow/status/AetherKiri/AetherKiri/build.yml?branch=main&amp;job=Build%20Web%20App&amp;label=Web%20Build"></a>
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

AetherKiri 是一个以 Godot 4.7 为应用外壳的多 Runtime 视觉小说平台。统一的
`AetherRuntimePlayer` 通过版本化 Provider 接口承载多个原生 Runtime；Godot
负责产品 UI、最终帧显示、输入、设置页、导出配置和平台打包。Runtime
Dispatcher 会根据游戏标记和能力选择 KiriRuntime、OnsRuntime 或可选的
A Runtime。

默认产品渲染链路是 **Godot Native**：引擎帧通过 Godot 持有的
`RenderingDevice` 资源输出。**GPU Bridge** 保留为显式可选的兼容和性能对照
后端，用于将外部 native GPU render target 导入 Godot。**Debug CPU** 只作为
可见的诊断 fallback，不作为性能验收目标。

```text
Godot App Shell
  -> AetherRuntimePlayer
    -> Runtime Dispatcher
      -> KiriRuntime -> KiriKiri2 Core / Plugins
      -> OnsRuntime -> OnscripterYuri
      -> A Runtime
```

## 亮点

- Godot 4.7 应用外壳，使用原生 GDExtension 集成。
- 使用唯一稳定的 `AetherRuntimePlayer` 和版本化 Provider ABI 接入当前及后续
  Runtime。
- C++17 KiriKiri2 运行时核心，覆盖视觉、音频、存储、VM 和插件支持。
- 集成 OnscripterYuri；合成后的 RGBA 帧由 Godot `ImageTexture` 显示，
  Godot 的鼠标、触摸和键盘输入会映射回 ONS 事件。
- 已接入 macOS、iOS/iPadOS、Android 和 Web 导出链路。
- 可在运行时选择渲染后端，并持久化设置。
- 随产品内置多语言 KAG3 Demo，可从游戏库直接体验，也可由玩家删除。
- 提供 smoke、渲染、交互、性能和手动复现 probe 脚本。
- 已手动验证过的游戏记录在 [`doc/verified_games.zh-CN.md`](doc/verified_games.zh-CN.md)。
- 以 GPL-3.0-or-later 分发源码。

## 仓库结构

| 路径 | 用途 |
| --- | --- |
| `apps/godot_app/` | Godot 项目、场景、设置 UI、性能/日志面板、图标和导出配置。 |
| `bridge/godot_extension/` | Godot 原生宿主库入口。 |
| `bridge/engine_api/` | 宿主层驱动 C++ 引擎的 C ABI。 |
| `bridge/onscripter_runtime/` | OnscripterYuri 无窗口宿主、帧读取和输入桥接。 |
| `bridge/siglus_runtime/` | Siglus 运行时 provider，以及应用到 pristine siglus_rs 源码的构建期 overlay（补丁 + FFI 文件），见 `bridge/siglus_runtime/overlay/`。 |
| `cpp/core/` | KiriKiri2 运行时、视觉系统、音频、存储、VM 和插件支持。 |
| `cpp/plugins/` | 内置 native 插件实现和兼容 stub。 |
| `packages/AetherInternal/` | 可选的私有 E-mote package submodule；公开版本不依赖它也能构建。 |
| `packages/OnscripterYuri/` | 公开的 OnscripterYuri git submodule。 |
| `packages/siglus_rs/` | 公开的 siglus_rs（Rust 版 SiglusEngine）git submodule。保持只读；AetherKiri 侧改动全部位于 `bridge/siglus_runtime/overlay/`。 |
| `packages/tjs2Decompiler/` | 可选的 Rust TJS2 字节码反汇编/分析辅助工具；不链接进运行时构建。 |
| `demos/aetherkiri-kag3/` | AetherKiri 内置 KAG3 Demo 的完整源码。 |
| `tests/profiles/` | 单游戏 probe profile。提交到仓库的 profile 不能包含机器本地路径。 |
| `tools/` | 不参与 iOS/Android 目标构建的开发和兼容工具。 |
| `doc/development.zh-CN.md` | 完整开发文档，覆盖架构、文件作用、构建、测试、probe 和调试。 |
| `doc/diagnostics.zh-CN.md` | 应用内调试、一条命令采集、诊断包结构与证据优先调查指南。 |
| `doc/verified_games.zh-CN.md` | 当前运行时已手动 smoke test 的游戏清单。 |

## 内置 Demo

产品包内包含多语言 AetherKiri KAG3 Demo：
`apps/godot_app/builtin_demos/aetherkiri-kag3/data.xp3`。应用首次启动时会
将它原子复制到 `user://builtin_games/` 的可写目录，再作为普通游戏加入
游戏库，因此启动和游玩时长统计与玩家导入的游戏一致。

玩家在详情页删除该条目时，会同时删除可运行副本及其本地存档（包括 Web
版独立的持久化存档目录），并记录“已删除”状态；刷新游戏库或升级应用都
不会自动恢复，只会重试未完成的清理。签名应用包中的种子资源仍属于产品
本体，删除可写副本不会缩小已安装的 App/APK/PCK。
可编辑源码和重新构建说明位于
[`demos/aetherkiri-kag3/`](demos/aetherkiri-kag3/)。

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
iOS 和 Android 导出配置会引用 `apps/godot_app/assets/icons/` 下的生成 PNG
尺寸，包括 App Store 图标和启动器图标。

## 运行平台要求

| 平台 | 最低版本 | 说明 |
| --- | --- | --- |
| macOS | macOS 13.0（Ventura） | Godot App 导出配置为 Universal，但当前 native 构建 triplet 只有 `arm64`；Intel 支持还需要单独构建 `x86_64` native 产物。 |
| iOS / iPadOS | iOS / iPadOS 16.0 | 真机为 `arm64`；开发环境可构建 `arm64` 和 `x86_64` 模拟器版本。 |
| Android | Android 8.0（API 26） | 当前产品导出只打包 `arm64-v8a`。 |
| Web | 不限定操作系统版本 | 浏览器必须支持 WebAssembly SIMD、WebAssembly threads 和 `SharedArrayBuffer`，并通过配置了跨源隔离（COOP/COEP）的 HTTP 服务访问。 |
| Linux | 需要自行编译 | 没有官方预编译产品包，需要在本地编译 `x86_64` 导出。 |
| Windows | 需要自行编译 | 没有官方预编译产品包，需要在本地编译 native 目标。 |

## 环境要求

- CMake 3.28+
- Ninja
- Rust 工具链（rustup），并安装 `aarch64-linux-android` target
  （`rustup target add aarch64-linux-android`），用于内置 Siglus 运行时；
  iOS/Web 的 Siglus 构建还需要对应 target。找不到 Rust 工具链时构建会自动
  降级为“禁用 Siglus”。
- vcpkg，位于 `.devtools/vcpkg` 或通过 `VCPKG_ROOT` 指定
- Godot 位于 `/Applications/Godot.app`，或通过 `GODOT_BIN=/path/to/Godot` 指定
- macOS/iOS 导出需要 Xcode
- Android 导出需要 Android SDK/NDK。脚本会优先使用
  `ANDROID_HOME`/`ANDROID_SDK_ROOT`，否则使用 `$HOME/Library/Android/sdk`，
  并优先选择已安装的 NDK 28.x。可通过 `ANDROID_NDK_HOME`（或
  `ANDROID_NDK_VERSION`）指定 NDK；与 Godot 4.7 导出模板配套的 Android
  构建应使用 NDK 28.1.13356709。
- Web 导出需要 Emscripten/emsdk，并确保 `emcc`、`em++`、`emar` 在 `PATH` 中。
- Web 的 GDExtension 导出需要 Godot dlink 模板，文件名为
  `web_dlink_debug.zip` 和 `web_dlink_release.zip`。
- 本地 Web dev server 使用 TypeScript/Vite，需要 Node.js 和 npm。

## 构建

首次检出后先初始化公开的 ONS 运行时：

```bash
git submodule update --init packages/OnscripterYuri
```

需要分析编译后 TJS2 的插件兼容开发，可再初始化可选辅助工具：

```bash
git submodule update --init packages/tjs2Decompiler
```

构建方式和证据校验流程见
[`doc/development.zh-CN.md`](doc/development.zh-CN.md#编译后-tjs2-分析)。

公开仓库在没有私有 package 权限时也可以正常构建并运行 CI。有权限的维护者可在构建前初始化完整 E-mote 实现：

```bash
git submodule update --init packages/AetherInternal
```

CMake 检测到 package 后会自动启用。使用
`-DAETHERKIRI_ENABLE_INTERNAL=OFF` 可强制验证公开 fallback；也可通过
`-DAETHERKIRI_INTERNAL_DIR=/absolute/path/to/AetherInternal` 指定独立检出目录。
GitHub Actions 的 `Build` workflow 会在可信运行中使用仓库 Secret
`AETHERSECRET` 作为只读 SSH 密钥，递归初始化私有 submodule。来自 fork
或 Dependabot 的 PR 无法访问仓库 Secret，因此这些不可信运行仍使用公开 fallback。

私有 package 只扩展 `motionplayer`，不会替换公开 target 或公开源码列表。
公开 backend 始终是脚本侧的唯一实现；检测到私库时，仅通过版本化扩展接口加入
本次新增的 E-mote 模块识别、眨眼/物理元数据、自动眨眼、胸部/头发/尾巴物理、
重复标签的精确遮罩策略及私有状态存档。私库不复制公开源码，也不使用补丁覆盖。
两种构建运行同一套公开 motionplayer 测试。package/API 版本不一致时 CMake 会
直接停止配置，不会静默产出不兼容的组合。

常用构建命令：

```bash
./build.sh macos debug
./build.sh macos release
./build.sh ios debug --simulator
./build.sh ios release
./build.sh android debug --abi=arm64-v8a
./build.sh android release --abi=arm64-v8a
./build.sh web debug
./build.sh web release
```

脚本会构建 native engine 和 Godot host library，将产物放到
`apps/godot_app/bin/`，并在 Godot 可用时运行对应的 Godot export preset。
Android 当前只接入了 `arm64-v8a`。

## ONScripter 游戏

OnscripterYuri 以公开 Git submodule 接入。关于集成授权与许可证的公开请求及
合规承诺记录在[上游 issue #75](https://github.com/YuriSizuku/OnscripterYuri/issues/75)。
实际使用权仍以 OnscripterYuri 的 GPL 声明以及源码中保留的版权声明为准。

直接把游戏目录加入游戏库即可。目录中出现 `0.txt`、`00.txt`、
`nscript.dat`、`nscr_sec.dat`、`nscript.___`、`onscript.nt2` 或
`onscript.nt3` 时，AetherKiri 会自动选择 OnscripterYuri。引擎以脚本的原始
分辨率合成，最终 RGBA 帧上传到 Godot 纹理并由现有 `TextureRect` 等比显示；
不会创建第二个原生窗口。

ONS 存档写入对应游戏目录下的 `savedata/`；只要游戏目录本身被保留，更新或
重新安装应用后存档仍会随游戏保留。首次启动时会把旧版应用目录
`onscripter_saves/<游戏名-路径哈希>/` 中的存档无覆盖复制到 `savedata/`；游戏
目录只读时才回退到应用目录。默认脚本编码与上游一致为 GBK；可用环境变量
`AETHERKIRI_ONS_ENCODING=gbk|sjis|utf8` 覆盖。与现有 KiriKiri 运行时相同，
一个应用进程只运行一个视觉小说会话；退出游戏后应重新启动 Aether。

仓库包含一个最小 ONS 渲染测试：

```bash
AETHERKIRI_SMOKE_GAME="$PWD/tests/fixtures/onscripter_smoke" \
  /Applications/Godot.app/Contents/MacOS/Godot \
  --headless --path apps/godot_app \
  --script res://scripts/smoke_test.gd
```

ONS 的 `mpegplay`、`avi` 和 `movie` 命令共用 AetherKiri 的 FFmpeg
媒体管线。`movie` 的 `click`、`loop`、`pos`、`async` 与 `movie stop`
均已接入；视频帧按脚本坐标叠加到 ONS 最终画面，音轨由媒体管线同步播放。
NSA/NS2/SAR 中的电影会先按需提取到用户缓存目录，不要求游戏资源必须是松散文件。

需要验证电影命令时，将任意测试视频放到
`tests/fixtures/onscripter_movie_smoke/video.avi`，然后运行：

```bash
AETHERKIRI_SMOKE_GAME="$PWD/tests/fixtures/onscripter_movie_smoke" \
AETHERKIRI_SMOKE_EXPECT_SCRIPT_MEDIA=1 \
  /Applications/Godot.app/Contents/MacOS/Godot \
  --headless --path apps/godot_app \
  --script res://scripts/smoke_test.gd
```

## 运行和测试构建产物

Web 构建会生成 Emscripten GDExtension side module：
`apps/godot_app/bin/web/<debug|release>/aether_kiri_godot.wasm`。如果 dlink
模板已安装，脚本会继续导出 Godot Web app 到
`out/godot/web/<debug|release>/index.html`。Web 导出启用了线程支持，部署时需要
跨源隔离相关 HTTP 头。云端部署的 Web 版通过浏览器文件/目录选择器读取用户授权的
本地游戏目录或 XP3，并用 `blob:` URL 做按需 Range 读取，不需要把 2-3G 游戏包整包
复制到 Emscripten 虚拟文件系统。

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
out/godot/android/debug/Aether-debug.apk
```

安装并启动到已连接设备或模拟器：

```bash
adb install -r out/godot/android/debug/Aether-debug.apk
adb shell monkey -p org.github.krkr2.aetherkiri \
  -c android.intent.category.LAUNCHER 1
```

构建 release APK：

```bash
./build.sh android release --abi=arm64-v8a
```

Release APK 输出到：

```text
out/godot/android/release/Aether-release.apk
```

release preset 在配置项目发布 keystore 之前会保持未签名。安装或分发前需要签名：

```bash
apksigner sign --ks /path/to/release.keystore \
  out/godot/android/release/Aether-release.apk
```

Android 上，如果平台允许访问文件系统，可通过 App UI 导入游戏；受限设备可将游戏目录复制到
App 的 documents/storage 位置后点击刷新。

### Web

构建前先激活 Emscripten：

```bash
source /path/to/emsdk/emsdk_env.sh
./build.sh web debug
```

导出路径：

```text
out/godot/web/debug/index.html
```

Web 产物需要通过 HTTP 服务访问，不能直接打开本地文件：

```bash
npm install
npm run web:dev:debug
```

Vite 会给导出的静态文件加上 `SharedArrayBuffer` 和 Godot 线程版 Web 导出需要的
COOP/COEP 头。Release 导出可用 `npm run web:dev:release`。Web 构建按线程 +
wasm SIMD 优先配置，OpenCV intrinsics 和 pthread 后端会参与 wasm 依赖构建。

云端部署时不需要配置服务器上的游戏路径；用户在浏览器中点击“导入”并授权选择本地
目录或 XP3 文件即可。浏览器导入的游戏文件只作为只读输入挂载；存档、游戏配置等运行时
写入会保存到当前站点的 IndexedDB `/userfs` 持久区，而不是写回用户原始游戏目录。
本地开发如需跳过浏览器选择器，可以临时使用 Vite 只读开发挂载：

```bash
AETHERKIRI_GAME_ROOT=/absolute/path/to/game \
AETHERKIRI_WEB_AUTO_START=1 \
npm run web:dev:release
```

多个游戏根目录可用 `AETHERKIRI_GAME_ROOTS`，分隔符跟当前系统的 PATH 分隔符一致。
`AETHERKIRI_WEB_AUTO_START_INDEX=1` 或 `AETHERKIRI_WEB_AUTO_START_NAME=title`
可指定自动启动哪一个挂载项。这些环境变量只适合本机调试，不是云端产品导入方案。

## 验证

迁移检查：

```bash
rg "F[l]utter|f[l]utter|A[N]GLE|Platform[ ]Graphics" README.md README.zh-CN.md apps bridge build CMakeLists.txt
rg "u[n]official-angle|l[i]bEGL|l[i]bGLESv2" CMakeLists.txt bridge cpp build vcpkg.json
./build.sh macos debug
./build.sh ios debug --simulator
./build.sh android debug --abi=arm64-v8a
./build.sh web debug
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

AetherKiri 以 GPL-3.0-or-later 分发。完整许可证文本见 `LICENSE`，第三方授权声明保留在 `THIRD_PARTY_LICENSES.md`。
Apple App Store 分发相关的有限额外许可见 `COPYING.iOS`；该许可仅供 Aether 官方 iOS、macOS 版本或版权持有人书面授权的发布者使用，第三方分支及衍生 App 不得援引。该许可仅适用于明确认可声明的版权持有人有权授权的部分，不能代表其他版权持有人授予上游或第三方材料的权利，也不撤销 GPL 已授予的权利。
