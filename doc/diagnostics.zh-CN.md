# 统一诊断会话

[English](diagnostics.md) | [简体中文](diagnostics.zh-CN.md)

[English](diagnostics.md) | 简体中文

统一诊断会话用于替代手工拼接 Godot 输出、C++ 日志、平台日志和临时性能探针。Debug 构建默认使用低开销 `baseline`，Release 默认关闭；Release 可在应用设置中显式开启，主机采集也会为本次会话覆盖该设置。

## 维护者快速选择

| 需求 | 首选入口 | 结果 |
| --- | --- | --- |
| 游戏仍能操作，想立即看帧耗时、日志、输入或插件状态 | 设置 → 诊断 → `Baseline`，进入游戏后点右上角 `DBG` | 应用内调试抽屉，不需要连接电脑 |
| 问题难描述，需要把完整证据交给别人 | `python3 tools/diagnose.py run <platform>` | `out/diagnostics/...zip` |
| UI 已卡死，无法点“标记问题” | 回到采集终端按 Enter | 带 `ui_marker_missing=true` 的 host marker 和平台证据 |
| 已拿到 ZIP，需要分析 | 把 ZIP 交给 Codex 并使用 `unpack-investigate-artifacts` skill | 静态、可追溯的证据报告；不会重新运行应用 |

## 应用内调试

设置页不再暴露一长串互不相关的日志开关，而是分为三组：

- **诊断**：选择诊断模式、悬浮信息的关闭/简要/详细，以及是否给真正的错误弹窗附带最近 20 行上下文。
- **兼容性**：选择核心或完整插件加载范围，以及缺失插件是否返回兼容对象。关闭“缺失插件兼容”会暴露真实错误，适合定位兼容边界。
- **高级开发工具**：插件调用追踪、完整 Trace、旧版控制台文件和 TJS 导出。它们只对本次运行有效，不会保存到下次启动；两个高开销追踪在 30 秒后自动关闭。

原开发者开关的去向和使用边界：

| 原开关 | 现在的位置 | 实际用途 | 是否与统一诊断重复 |
| --- | --- | --- | --- |
| Plugin Load Mode | 兼容性 → 插件加载范围 | `core` 只预载核心兼容插件，`full` 使用旧版完整注册路径 | 不重复；这是运行兼容策略，不是日志等级 |
| Mock Bypass | 兼容性 → 缺失插件兼容 | 开启时给缺失插件返回兼容对象；关闭后暴露真实缺失错误 | 不重复；日常兼容需要开启，定位兼容边界时才关闭 |
| Plugin Call Trace | 高级开发工具 | 把每次原生插件调用写入 `plugin_trace.log` | 与 `plugin`/`full` 有部分交集，但粒度更细、开销更高，只用于短窗口 |
| Trace Log | 高级开发工具 | 打开原生 `spdlog` 的 trace 级明细 | 与 `full` 有部分交集；仅在结构化事件不足时短时开启 |
| Console Log File | 高级开发工具 | 继续生成旧版 `krkr.console.log` | 大多被统一事件和平台日志覆盖，只为旧流程兼容保留 |
| Export TJS Scripts | 高级开发工具 | 游戏加载时导出 TJS 字节码反汇编 | 不属于通用日志；只用于脚本兼容/逆向调查 |
| Log Alerts | 已删除 | 曾把 Warning/Error/Fatal 逐条变成系统弹窗 | 与事件页重复且会打断游戏、改变时序，没有长期保留价值 |
| Attach Logs to Errors | 诊断 → 错误上下文 | 只给真正的错误对话框附加最近 20 行引擎上下文 | 有意保留；它帮助脱离调试抽屉阅读明确错误 |

进入游戏后，右上角有两个互不重叠的入口：

- **标记问题**：立即写入 Godot 与 C++ 事件流，封存前 10 秒并继续收集后 5 秒，同时保存当时的性能、输入、内存和插件摘要。按钮、状态提示、移动端短震动和控制台行共同确认操作成功；不会自动截图。
- **DBG**：打开调试抽屉。抽屉关闭时不格式化文本；打开后以 4 Hz 更新。

调试抽屉包含：

- **概览**：FPS、P50/P95/P99/最大帧时间、C++ tick、Godot update、渲染器、纹理/画布、内存/缓存、错误数和丢弃事件数。
- **事件**：按级别和子系统筛选统一 JSONL 事件。
- **日志**：最多显示最近 200 行，可搜索、按级别筛选、暂停、复制和清空。
- **输入**：接收、转发、阻塞、节流、越界和抑制计数；可在画面上显示触点与最近命中位置。
- **插件**：加载成功/失败/fallback、缺失成员和调用统计；也可临时打开高级追踪。
- **操作**：选择问题类型并标记、保存状态快照、手动截图、捕获下一个慢帧、自检、复制摘要或直接导出 ZIP。

截图只在明确点击时产生。状态快照不包含整份日志和事件副本，避免诊断包无界增长。

## 一条命令完成复现

```bash
python3 tools/diagnose.py run android
python3 tools/diagnose.py run macos
python3 tools/diagnose.py run ios --device "设备名称或 UDID"
python3 tools/diagnose.py run ios-simulator
python3 tools/diagnose.py run web
```

命令默认直接使用已经安装或已导出的应用，快速重启并进入日志采集，不执行构建或安装。只有需要更新应用时才显式使用 `--build-install`；已有构建产物可配合 `--build-install --reuse-build` 只安装不重编。`--profile` 是本次主机会话的权威档位，会覆盖应用内保存的档位。问题出现后点击游戏右上角的“标记问题”，再回到终端按 Enter。非交互环境使用 `--duration 30`。

标记成功后按钮会显示“已标记 #N / 诊断日志已保存”，移动端短震动，并向 logcat/控制台输出 `[diagnostics] issue_marker`。事件会立即 flush 到 `Diagnostics/<session>/events.jsonl`，同时封存对应的 `incidents/marker-NN-pre/post.jsonl`；单次会话达到 8 个标记后按钮会明确显示已满。

Android 会在采集前运行设备预检。请先用 `adb devices` 确认设备状态为 `device`；多设备连接时通过 `--device SERIAL` 指定目标，无线 mDNS 序列号即使包含 Bonjour 生成的空格后缀也会完整保留。工具优先使用 `ANDROID_SDK_ROOT`/`ANDROID_HOME` 下与构建链一致的 ADB。只有 `--build-install` 模式涉及安装，并仅在传输掉线时重连重试一次。

移动端原始诊断文件无需 root 即可访问：

- Android：`/storage/emulated/0/Documents/AetherKiri/Diagnostics/<session>/`
- iOS/iPadOS：“文件”App → “我的 iPhone/iPad” → AetherKiri → `AetherKiri/Diagnostics/<session>/`；也支持 Finder/iTunes 文件共享。

结果写入 `out/diagnostics/<时间>-<平台>-<session>/` 和同名 ZIP。诊断包包含构建元数据、统一 `events.jsonl`、标记前 10 秒与后 5 秒窗口、平台日志和只陈述证据的 `summary.md`。应用内保存的状态快照和显式截图会归入 `attachments/`。如果 UI 卡死无法点击，工具会写入带 `ui_marker_missing=true` 的 host marker，仍然保留平台证据。

诊断包的关键文件：

| 路径 | 含义 |
| --- | --- |
| `metadata.json` | Git 提交、平台、设备、请求/实际档位和渲染后端 |
| `events.jsonl` | 跨 Godot/C ABI/C++/host 的归一化事件流 |
| `incidents/` | 每个应用内标记的前后窗口 |
| `attachments/` | 用户显式保存的状态快照和截图 |
| `platform/` | logcat、devicectl、系统日志、crash、meminfo/gfxinfo 或 trace |
| `summary.md` | 自动线索，不是根因结论；关键判断必须回查原始证据 |

## 诊断等级

| 等级 | 重点 | 开销 |
| --- | --- | --- |
| `baseline` | 生命周期、Warning/Error、每秒帧统计、20 ms 慢帧 | 低，默认 |
| `input` | 输入接收、转发、抑制和慢输入窗口 | 低到中 |
| `render` | GPU fallback、tick/draw/upload/present | 中 |
| `storage` | 存储、归档、图片加载与缓存 | 中 |
| `script` | 应用与 TJS VM | 中到高 |
| `audio` / `video` / `plugin` | 对应子系统 | 中 |
| `system` | Android Perfetto 或平台系统证据 | 中到高 |
| `full` | 所有详细探针 | 高，仅短时间使用 |

连续复现直接重复运行命令即可。`baseline` 不逐帧刷盘；事件每秒、标记问题、进入后台和退出时批量刷新。事件文件按 4 MiB 轮转两份，内存环最多 2000 条，单次会话最多 8 个标记。

## 平台采集内容

- Android：PID logcat、Warning/Error、crash buffer、`dumpsys meminfo`、`gfxinfo framestats`、应用诊断目录；`system/full` 额外尝试 Perfetto。
- iOS：devicectl 或 simctl 控制台、应用数据容器、真机系统 crash logs。
- macOS：stdout/stderr、统一日志、Godot 应用数据目录。
- Web：Vite 客户端日志接口接收结构化事件；普通 Web 部署在会话结束时下载 `.aetherdiag.json`。

当前默认保留原始路径、游戏名和日志文本，不自动脱敏。截图默认关闭，避免改变小性能问题的时序。

## 常见问题

- **Android 显示无线设备但不可用**：以 `adb devices -l` 的真实状态为准；必须是 `device`，`offline`/`unauthorized` 需要重新解锁或配对。含空格的 mDNS 序列号会被完整传给 `adb -s`。
- **脚本提示应用诊断目录未就绪**：当前安装包可能早于统一诊断功能；仅这时使用一次 `--build-install`。
- **点击标记后不确定是否成功**：检查按钮是否短暂显示 `✓ #N`、状态文字、移动端震动，以及 logcat/控制台中的 `[diagnostics] issue_marker`。四者都没有时按 UI 输入问题调查。
- **没有应用内 marker**：不要丢弃诊断包。检查 host marker 的 `ui_marker_missing`，并明确说明缺失的是应用侧时间锚点。
- **事件丢弃数大于 0**：缩短复现窗口，先保持同一档位重试；不要立刻升级到 `full`。
- **诊断本身影响性能**：先退回 `baseline`，关闭调试抽屉和详细悬浮层。`full`、完整 Trace、插件逐调用追踪只用于短窗口。

## 调查现有诊断包

`unpack-investigate-artifacts` skill 只消费已有 ZIP 或目录，不会构建、安装、启动应用，也不会运行 `tools/diagnose.py`。它先校验哈希、成员路径和解包安全，再核对 session、sequence、clock、queue drops、marker 窗口、快照/截图/自检，最后报告“最深的已确认成功边界”和“第一个没有证据支持的边界”。时间相邻不会被自动宣称为根因。

## 验证

```bash
cmake -S . -B out/macos/debug -DENABLE_TESTS=ON -DBUILD_TOOLS=OFF
cmake --build out/macos/debug --target engine-api-diagnostics
out/macos/debug/tests/unit-tests/engine_api/engine-api-diagnostics
python3 -m unittest discover -s tests/python -v
/Applications/Godot.app/Contents/MacOS/Godot --headless \
  --path apps/godot_app --script res://scripts/diagnostic_session_test.gd
/Applications/Godot.app/Contents/MacOS/Godot --headless \
  --path apps/godot_app --script res://scripts/debug_console_test.gd
```

Godot 测试需要能够写入平台正常的 `user://` 目录。
