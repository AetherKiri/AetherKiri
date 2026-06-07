# Verified Games

[简体中文](verified_games.zh-CN.md)

Last updated: 2026-06-07

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

| Game | Platform / build | Import path | Verified scope | Result | Verifier | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| もっと！孕ませ！炎のおっぱい異世界 おっぱいバニー学園！ | Web release, Chrome, Vite local server | Browser local directory import | Startup, script/plugin loading, title/menu rendering, basic input, continue/save-load route smoke, CJK/symbol font rendering, and IndexedDB-backed userfs persistence behavior | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. Web deployment still requires COOP/COEP headers. Live2D Cubism Core for Web remains an external proprietary runtime and must be supplied separately when a title needs it. |
| もっと！孕ませ！炎のおっぱい異世界 おっぱいバニー学園！ | macOS release app | App UI local directory import | Startup, title/menu rendering, basic input, save/load smoke, and CJK/symbol font rendering | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |
| もっと！孕ませ！炎のおっぱい異世界 おっぱいバニー学園！ | iOS/iPadOS app build | Files app import | Startup, title/menu rendering, touch input, save/load smoke, and CJK/symbol font rendering | Smoke verified | [@akitaSummer](https://github.com/akitaSummer) | Local game files are not committed. |

## How To Add A Game

Add one row per game and include the exact platform/build that was tested and
the verifier's GitHub handle. Keep machine-local game paths out of the
repository; describe them as browser local directory import, XP3 import, iOS
Files app import, or app UI import instead.

Use "Flow verified" only when the flow is explicitly checked on that build. If
the runtime, renderer, filesystem, movie playback, plugin stubs, or font stack
changes substantially, mark affected entries as "Needs retest" until they are
checked again.
