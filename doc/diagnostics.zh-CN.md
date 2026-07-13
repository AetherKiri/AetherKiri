# 统一诊断会话

[English](diagnostics.md) | 简体中文

统一诊断会话用于替代手工拼接 Godot 输出、C++ 日志、平台日志和临时性能探针。Debug 构建默认启用低开销 `baseline` 环形缓冲；Release 只有显式设置 `AETHERKIRI_DIAGNOSTICS=1` 时才启用。

## 一条命令完成复现

```bash
python3 tools/diagnose.py run android
python3 tools/diagnose.py run macos
python3 tools/diagnose.py run ios --device "设备名称或 UDID"
python3 tools/diagnose.py run ios-simulator
python3 tools/diagnose.py run web
```

命令默认直接使用已经安装或已导出的应用，快速重启并进入日志采集，不执行构建或安装。只有需要更新应用时才显式使用 `--build-install`；已有构建产物可配合 `--build-install --reuse-build` 只安装不重编。问题出现后点击游戏右上角的“标记问题”，再回到终端按 Enter。非交互环境使用 `--duration 30`。

标记成功后按钮会显示“已标记 #N / 诊断日志已保存”，移动端短震动，并向 logcat/控制台输出 `[diagnostics] issue_marker`。事件会立即 flush 到 `Diagnostics/<session>/events.jsonl`，同时封存对应的 `incidents/marker-NN-pre/post.jsonl`；单次会话达到 8 个标记后按钮会明确显示已满。

Android 会在采集前运行设备预检。请先用 `adb devices` 确认设备状态为 `device`；多设备连接时通过 `--device SERIAL` 指定目标，无线 mDNS 序列号即使包含 Bonjour 生成的空格后缀也会完整保留。工具优先使用 `ANDROID_SDK_ROOT`/`ANDROID_HOME` 下与构建链一致的 ADB。只有 `--build-install` 模式涉及安装，并仅在传输掉线时重连重试一次。

移动端原始诊断文件无需 root 即可访问：

- Android：`/storage/emulated/0/Documents/AetherKiri/Diagnostics/<session>/`
- iOS/iPadOS：“文件”App → “我的 iPhone/iPad” → AetherKiri → `AetherKiri/Diagnostics/<session>/`；也支持 Finder/iTunes 文件共享。

结果写入 `out/diagnostics/<时间>-<平台>-<session>/` 和同名 ZIP。诊断包包含构建元数据、统一 `events.jsonl`、标记前 10 秒与后 5 秒窗口、平台日志和只陈述证据的 `summary.md`。如果 UI 卡死无法点击，工具会写入带 `ui_marker_missing=true` 的 host marker，仍然保留平台证据。

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

## 验证

```bash
cmake -S . -B out/macos/debug -DENABLE_TESTS=ON -DBUILD_TOOLS=OFF
cmake --build out/macos/debug --target engine-api-diagnostics
out/macos/debug/tests/unit-tests/engine_api/engine-api-diagnostics
python3 -m unittest discover -s tests/python -v
/Applications/Godot.app/Contents/MacOS/Godot --headless \
  --path apps/godot_app --script res://scripts/diagnostic_session_test.gd
```

Godot 测试需要能够写入平台正常的 `user://` 目录。
