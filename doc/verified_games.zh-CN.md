# 已验证游戏

[English](verified_games.md)

最后更新：2026-07-18

本文档记录已经用 AetherKiri 手动 smoke test 或 flow test 过的游戏。它是兼容性记录，
不代表每条路线、每个视频、每个插件路径或每个存档状态都已经完整验证。

## 验证级别

| 级别 | 含义 |
| --- | --- |
| 冒烟验证通过 | 游戏可以在对应平台导入、启动、渲染初始 UI，并响应基础输入。 |
| 流程验证通过 | 已手动检查命名流程，例如存读档、继续游戏或场景切换。 |
| 需要复测 | 游戏之前可以运行，但在引擎、渲染器、插件或 Web 文件系统改动后需要重新确认。 |

## 当前清单

| 游戏 | 已验证平台/构建 | 验证范围 | 结果 | 验证人 | 备注 |
| --- | --- | --- | --- | --- | --- |
| もっと！孕ませ！炎のおっぱい異世界 おっぱいバニー学園！ | Web release（Chrome，Vite 本地服务器）；macOS release app；iOS/iPadOS iPad app build；Android release APK | 启动、脚本/插件加载、标题/菜单渲染、基础输入、继续/存读档路径冒烟、CJK/符号字体渲染，以及 Web 端 IndexedDB `/userfs` 持久化行为 | 冒烟验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。Web 部署仍需要 COOP/COEP 头。需要 Live2D 的游戏仍需单独提供 Web 版 Live2D Cubism Core 专有运行时。 |
| 喫茶ステラと死神の蝶 | macOS release app；iOS/iPadOS iPad release app build；Android release APK | 启动、标题/菜单渲染、标题背景快速切换/输入压力、继续游戏流程、场景/文字渲染，以及 CJK/符号字体渲染 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| RIDDLE JOKER | macOS release app；iOS/iPadOS iPad release app build；Android release APK | 启动、标题/菜单渲染、标题动画/图层、继续游戏流程、场景/文字渲染、对话输入压力、存读档冒烟，以及 CJK/符号字体渲染 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| 9-nine-ここのつここのかここのいろ | macOS release app；iOS/iPadOS iPad release app build；Android release APK | 启动、标题/菜单渲染、画廊/影片播放冒烟、场景/文字渲染，以及 CJK/符号字体渲染 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| 9-nine-そらいろそらうたそらのおと | macOS release app；iOS/iPadOS iPad release app build；Android release APK | 启动、标题/菜单渲染、画廊/音乐鉴赏播放冒烟、场景/文字渲染，以及 CJK/符号字体渲染 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| オトメ*ドメイン | macOS release app；iOS/iPadOS iPad release app build；Android release APK | 启动、标题/菜单渲染、画廊场景回放流程、编译版 PSB 场景标签解析、场景/文字渲染，以及 CJK/符号字体渲染 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| もっと！孕ませ！炎のおっぱい異世界おっぱいメイド学園！ | macOS release app；iOS/iPadOS iPad release app build；Android release APK | 启动、标题/菜单渲染、继续/读档流程、场景/文字渲染、语音播放冒烟、存读档冒烟、退出行为，以及 CJK/符号字体渲染 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| もっと！孕ませ！炎のおっぱい異世界超エロサキュバス学園！ | macOS release app；iOS/iPadOS iPad release app build；Android release APK | 启动、标题/菜单渲染、继续/读档流程、场景/文字渲染、Live2D 渲染冒烟、语音播放冒烟、存读档冒烟，以及 CJK/符号字体渲染 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| 天神乱漫 -LUCKY or UNLUCKY!?- | macOS release app；iOS/iPadOS iPad release app build | 启动、开场影片切换、标题/菜单渲染、继续游戏流程、场景/文字渲染、音频播放和基础输入 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| のーぶる☆わーくす | macOS release app；iOS/iPadOS iPad release app build | 启动、开场影片切换、标题/菜单渲染、继续游戏流程、场景/文字渲染、音频播放和基础输入 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| サノバウィッチ | macOS release app；iOS/iPadOS iPad release app build | 启动、标题/菜单渲染、继续/读档流程、场景/文字渲染、音频播放和基础输入 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| 千恋＊万花 | macOS release app；iOS/iPadOS iPad release app build | 启动、标题/菜单渲染、继续/读档流程、场景/文字渲染、音频播放和基础输入 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| 天使☆騒々 RE-BOOT! | macOS release app；iOS/iPadOS iPad release app build | 启动、标题/菜单渲染、继续游戏流程、场景/文字渲染、画廊渲染与动画冒烟、音频播放和基础输入 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| ライムライト・レモネードジャム | macOS release app；iOS/iPadOS iPad release app build | 启动、标题动画与菜单渲染、继续/读档流程、场景/文字渲染、画廊导航与图像合成、音频播放和基础输入 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| ワガママハイスペック | macOS release app；iOS/iPadOS iPad release app build | 启动、标题/菜单渲染、继续/读档流程、场景/文字渲染、音乐选择与播放、锁屏恢复音频和基础输入 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| ワガママハイスペック OC | macOS release app；iOS/iPadOS iPad release app build | 启动、标题/菜单渲染、继续/读档流程、场景/文字渲染、音乐播放、锁屏恢复音频和基础输入 | 流程验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |
| 淫母マンション～ママは、性処理肉便器～ | macOS release app；iOS/iPadOS iPad release app build | 启动、标题/菜单渲染、场景/文字渲染、音频播放和基础输入 | 冒烟验证通过 | [@akitaSummer](https://github.com/akitaSummer) | 本地游戏文件不提交到仓库。 |

## 如何新增游戏

每个游戏添加一行，并在同一行记录已验证的平台/构建和验证人的 GitHub handle。不要把
本机游戏路径写进仓库。

只有明确检查过某个流程时，才把结果写成“流程验证通过”。如果运行时、渲染器、文件
系统、视频播放、插件 stub 或字体栈有较大改动，受影响条目应先标记为“需要复测”，
直到重新验证完成。
