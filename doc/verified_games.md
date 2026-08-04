# Verified Games

[简体中文](verified_games.zh-CN.md)

Last updated: 2026-08-05

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
| 恋がさくころ桜どき | Linux x64 release app; macOS app; iOS/iPadOS app build on iPad | Import, startup, initial title/UI and text rendering, and basic input | Smoke verified | [@KYoiRyi](https://github.com/KYoiRyi), [@MadCcc](https://github.com/MadCcc) | Local game files are not committed. |
| ましろ色シンフォニー | Linux x64 release app | Import, startup, initial title/UI and text rendering, and basic input | Smoke verified | [@KYoiRyi](https://github.com/KYoiRyi) | Local game files are not committed. |
| Clover Day's | Linux x64 release app | Import, startup, initial title/UI and text rendering, and basic input | Smoke verified | [@KYoiRyi](https://github.com/KYoiRyi) | Local game files are not committed. |
| 金色ラブリッチェ -Golden Time- | Linux x64 release app; macOS app; iOS/iPadOS app build on iPad | Import, startup, initial title/UI and text rendering, and basic input | Smoke verified | [@KYoiRyi](https://github.com/KYoiRyi), [@MadCcc](https://github.com/MadCcc) | Local game files are not committed. |
| もっと！孕ませ！炎のおっぱい異世界 おっぱいバニー学園！ | Web release on Chrome/Vite local server; macOS debug and release apps; iOS/iPadOS app build on iPad; Android release APK | Startup, script/plugin loading, title/menu rendering, basic input, continue/save-load route, MPEG-1/MP2 in-scene movie rendering through natural completion on macOS debug, CJK/symbol font rendering, and IndexedDB-backed userfs persistence behavior on Web | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. Web deployment still requires COOP/COEP headers. Live2D Cubism Core for Web remains an external proprietary runtime and must be supplied separately when a title needs it. |
| もっと！孕ませ！炎のおっぱい異世界おっぱいスパイ学園！ | macOS app; iOS/iPadOS app build on iPad | Import, startup, initial title/UI and text rendering, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 喫茶ステラと死神の蝶 | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, rapid title background switching/input stress, continue flow, scene/text rendering, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| RIDDLE JOKER | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, title motion/layering, continue flow, scene/text rendering, dialogue input stress, save/load smoke, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 9-nine-ここのつここのかここのいろ | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, gallery/movie playback smoke, scene/text rendering, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 9-nine-そらいろそらうたそらのおと | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, gallery/music playback smoke, scene/text rendering, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| PARQUET | macOS app; iOS/iPadOS app build on iPad | Import, startup, initial title/UI and text rendering, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 9-nine- 新章 | macOS app; iOS/iPadOS app build on iPad | Import, startup, initial title/UI and text rendering, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| as:9-nine- ARTEISIA | macOS app; iOS/iPadOS app build on iPad | Import, startup, initial title/UI and text rendering, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 乱れ雪月華 ～儚く散る細雪～ | macOS debug app; iOS/iPadOS debug app build on iPad | Startup, age-warning and title/menu rendering, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 乱れ雪月華 ～月夜の淫舞、狂気の契り～ | macOS debug app; iOS/iPadOS debug app build on iPad | Startup, title/menu rendering, and basic input through the new-game transition | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| オトメ*ドメイン | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, gallery scene replay flow, compiled PSB scenario label lookup, scene/text rendering, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| もっと！孕ませ！炎のおっぱい異世界おっぱいメイド学園！ | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, continue/load flow, scene/text rendering, voice playback smoke, save/load smoke, exit behavior, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| もっと！孕ませ！炎のおっぱい異世界超エロサキュバス学園！ | macOS release app; iOS/iPadOS release app build on iPad; Android release APK | Startup, title/menu rendering, continue/load flow, scene/text rendering, Live2D rendering smoke, voice playback smoke, save/load smoke, and CJK/symbol font rendering | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 天神乱漫 -LUCKY or UNLUCKY!?- | macOS release app; iOS/iPadOS release app build on iPad | Startup, opening movie transition, title/menu rendering, continue flow, scene/text rendering, audio playback, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| のーぶる☆わーくす | macOS release app; iOS/iPadOS release app build on iPad | Startup, opening movie transition, title/menu rendering, continue flow, scene/text rendering, audio playback, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| サノバウィッチ | macOS release app; iOS/iPadOS release app build on iPad | Startup, title/menu rendering, continue/load flow, scene/text rendering, audio playback, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 千恋＊万花 | macOS release app; iOS/iPadOS release app build on iPad | Startup, title/menu rendering, continue/load flow, scene/text rendering, audio playback, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 天使☆騒々 RE-BOOT! | macOS release app; iOS/iPadOS release app build on iPad | Startup, title/menu rendering, continue flow, scene/text rendering, gallery rendering and animation smoke, audio playback, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| ライムライト・レモネードジャム | Windows x64 debug app; macOS release app; iOS/iPadOS release app build on iPad | Startup, title animation and menu rendering, continue/load flow, scene/text rendering, gallery navigation/compositing, audio playback, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer), [@KYoiRyi](https://github.com/KYoiRyi) | Local game files are not committed. |
| ワガママハイスペック | macOS release app; iOS/iPadOS release app build on iPad | Startup, title/menu rendering, continue/load flow, scene/text rendering, music selection/playback, lock-screen audio recovery, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| ワガママハイスペック OC | macOS release app; iOS/iPadOS release app build on iPad | Startup, title/menu rendering, continue/load flow, scene/text rendering, music playback, lock-screen audio recovery, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 淫母マンション～ママは、性処理肉便器～ | macOS release app; iOS/iPadOS release app build on iPad | Startup, title/menu rendering, scene/text rendering, audio playback, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 天色＊アイルノーツ | macOS debug app | Startup, title/menu rendering, continue/load flow, background and character rendering, SD CG transition stability, scene/text rendering, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| GINKA | Windows x64 debug app | Import, startup, title/menu rendering, game-data loading, background and character rendering, and basic input | Smoke verified | [@KYoiRyi](https://github.com/KYoiRyi) | Local game files are not committed. |
| NEKOPARA Vol. 1 | macOS debug app | Startup, title/menu rendering, data-load and first-save flow, scene/text rendering, E-mote character rendering and animation, rapid dialogue input, and character-layer click forwarding | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| ネコぱら After ラ・ヴレ・ファミーユ | macOS debug app | Startup, title/menu rendering, new-game flow, scene/text rendering, E-mote character rendering and animation, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| ネコぱらExtra 仔ネコの日の約束 | macOS debug app | Import, startup, initial title/UI and text rendering, E-mote character rendering, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| ネコぱら vol.0 水無月ネコたちの日常！ | macOS debug app | Startup, title/menu rendering, start-game flow, scene/text rendering, E-mote character rendering and blinking, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| まいてつ -Pure Station- | macOS release app; iOS/iPadOS release app build on iPad | Startup, title/menu rendering, continue/load flow, scene/text rendering, E-mote character rendering and animation, gallery CG navigation/compositing and layer ordering, audio playback, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| まいてつ Last Run!! | macOS release app; iOS/iPadOS release app build on iPad | Startup, title/menu rendering, continue/load flow, scene/text rendering, E-mote character rendering and animation, gallery CG navigation/compositing and layer ordering, audio playback, input stress, and FPS smoke | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 冬之吻 | macOS debug app | Startup, title/menu rendering, first-save load flow, scene/text rendering, E-mote character composition and blinking, atomic character-distance/pose changes, and basic input | Flow verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 恋騎士 Purely☆Kiss | macOS release app; iOS/iPadOS release app build on iPad | Import, startup, title/menu rendering, scene/text rendering, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 銃騎士Cutie☆Bullet | macOS release app; iOS/iPadOS release app build on iPad | Import, startup, title/menu rendering, scene/text rendering, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 聖騎士Melty☆Lovers | macOS release app; iOS/iPadOS release app build on iPad | Import, startup, title/menu rendering, scene/text rendering, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 将軍様はお年頃 | macOS release app; iOS/iPadOS release app build on iPad | Import, startup, title/menu rendering, scene/text rendering, and basic input | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| 真愛の百合は赤く染まる | Linux x64 release app; iOS/iPadOS release app build on iPad | Import, startup, title/menu rendering, new-game flow, scene/text rendering, audio playback, and basic input | Flow verified | [@KYoiRyi](https://github.com/KYoiRyi) | Local game files are not committed. |
| 死に逝く君、館に芽吹く憎悪 | Linux x64 release app; iOS/iPadOS release app build on iPad | Import, startup, title/menu rendering, new-game flow, scene/text rendering, audio playback, and basic input | Flow verified | [@KYoiRyi](https://github.com/KYoiRyi) | Local game files are not committed. |
| 枯れない世界と終わる花 | Linux x64 release app; iOS/iPadOS release app build on iPad | Import, startup, title/menu rendering, new-game flow, scene/text rendering, audio playback, and basic input | Flow verified | [@KYoiRyi](https://github.com/KYoiRyi) | Local game files are not committed. |
| エッチで一途なド田舎兄さまと、古式ゆかしい病弱妹 | Linux x64 release app; iOS/iPadOS release app build on iPad | Import, startup, title/menu rendering, new-game flow, scene/text rendering, audio playback, and basic input | Flow verified | [@KYoiRyi](https://github.com/KYoiRyi) | Local game files are not committed. |

## How To Add A Game

Add one row per game and list the verified platforms/builds in that row. Include
the verifier's GitHub handle. Keep machine-local game paths out of the
repository.

Use "Flow verified" only when the flow is explicitly checked on that build. If
the runtime, renderer, filesystem, movie playback, plugin stubs, or font stack
changes substantially, mark affected entries as "Needs retest" until they are
checked again.
