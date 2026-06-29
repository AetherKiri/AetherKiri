# Verified Games

[简体中文](verified_games.zh-CN.md)

Last updated: 2026-06-30

This document tracks games that have been manually smoke-tested with
AetherKiri. It is a compatibility notebook, not a guarantee that every route,
movie, plugin path, or save state in a title has been exhaustively validated.

## Verification Levels

| Level | Meaning |
| --- | --- |
| Smoke verified | The game can be imported, launched, render its initial UI, and respond to basic input on the listed platform. |
| Flow verified | A named in-game flow such as save/load, continue, or a scene transition was manually checked. |
| Needs retest | The title previously ran but should be checked again after engine, renderer, plugin, or Web filesystem changes. |

## Current List

| Game | Verified platforms / builds | Verified scope | Result | Verifier | Notes |
| --- | --- | --- | --- | --- | --- |
| もっと！孕ませ！炎のおっぱい異世界 おっぱいバニー学園！ | Web release on Chrome/Vite local server; macOS release app; iOS/iPadOS app build on iPad; Android release APK | Startup, script/plugin loading, title/menu rendering, basic input, continue/save-load route smoke, CJK/symbol font rendering, and IndexedDB-backed userfs persistence behavior on Web | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. Web deployment still requires COOP/COEP headers. Live2D Cubism Core for Web remains an external proprietary runtime and must be supplied separately when a title needs it. |
| 喫茶ステラと死神の蝶 | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, rapid title background switching/input stress, continue flow, scene/text rendering, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| RIDDLE JOKER | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, title motion/layering, continue flow, scene/text rendering, dialogue input stress, save/load smoke, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 9-nine-ここのつここのかここのいろ | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, gallery/movie playback smoke, scene/text rendering, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 9-nine-そらいろそらうたそらのおと | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, gallery/music playback smoke, scene/text rendering, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| オトメ*ドメイン | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, gallery scene replay flow, compiled PSB scenario label lookup, scene/text rendering, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| もっと！孕ませ！炎のおっぱい異世界おっぱいメイド学園！ | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, continue/load flow, scene/text rendering, voice playback smoke, save/load smoke, exit behavior, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| もっと！孕ませ！炎のおっぱい異世界超エロサキュバス学園！ | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, continue/load flow, scene/text rendering, Live2D rendering smoke, voice playback smoke, save/load smoke, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |

## How To Add A Game

Add one row per game and list the verified platforms/builds in that row. Include
the verifier's GitHub handle. Keep machine-local game paths out of the
repository.

Use "Flow verified" only when the flow is explicitly checked on that build. If
the runtime, renderer, filesystem, movie playback, plugin stubs, or font stack
changes substantially, mark affected entries as "Needs retest" until they are
checked again.
