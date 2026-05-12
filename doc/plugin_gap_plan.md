# AetherKiri Plugin Gap Plan

Date: 2026-05-13

## Scope

Compare and close plugin compatibility gaps between:

- AetherKiri: `cpp/plugins`
- KiriKiri2 reference: `/Users/liuyu/AkitaSummer/kirikiri2/kirikiri2/src/plugins/win32`
- krkrz reference: `/Users/liuyu/AkitaSummer/krkrz`

`krkrz` does not provide a peer `plugins` tree. It is useful mainly as a
reference for plugin loading, exported host APIs, Susie support, and movie
infrastructure.

## Current State

AetherKiri already has a static internal plugin registration path through
`ncbAutoRegister` and `TVPLoadInternalPlugins()`. It also contains several
game-specific compatibility plugins that do not exist in the original
KiriKiri2 tree, including:

- `motionplayer.dll` / `emoteplayer.dll`
- `psbfile.dll`
- `krkrlive2d.dll`
- `krkrgles.dll`
- `DrawDeviceD2Dm.dll`
- `AlphaMovie.dll`
- several compatibility stubs

Compared with KiriKiri2's `src/plugins/win32`, many plugins are still missing
or only represented by link-success stubs.

## Phase 1: Link-Time Compatibility Stubs

Goal: make common `Plugins.link()` calls fail less often while keeping behavior
explicitly conservative.

1. Add no-op module registrations for plugins seen failing in local logs:
   - `flashPlayer.dll`
   - `layerExSubImage.dll`
   - `gfxEffect.dll`
2. Add low-risk no-op registrations for common platform-only plugins where the
   engine already has native or cross-platform substitutes:
   - `clipboardEx.dll`
   - `shellExecute.dll`
   - `process.dll`
   - `tasktray.dll`
   - `adjustMonitor.dll`
   - `fpslimit.dll`
   - `systemEx.dll`
3. Add tests that assert these modules can be linked through the internal
   plugin registry.

Commit rule: one commit for the stub module set, one commit for tests.

## Phase 2: Enable Existing Implementations

Goal: turn existing but disabled source into actual registered modules.

1. Evaluate and enable `json.dll`.
2. Evaluate `steam` / `DrawDeviceForSteam`; keep disabled unless the dependency
   and behavior are clean across non-Windows targets.
3. Confirm `AlphaMovie.dll`, `packinone.dll`, and `extNagano.dll` are retained
   in final links under whole-archive builds.

Commit rule: each enabled plugin gets its own commit.

## Phase 3: Complete Existing Partial Implementations

Goal: improve behavior where AetherKiri already has real plugin code.

1. `motionplayer.dll` / `emoteplayer.dll`
   - Replace remaining explicit runtime stubs where behavior is known.
   - Keep reverse-engineering-only placeholders documented when behavior is
     genuinely unknown.
   - Add focused tests for player state, timeline, resource lookup, and layer
     update behavior.
2. `psbfile.dll`
   - Complete `collectResources()` for image, motion, scene, MMO, and sound
     archive types.
   - Implement or intentionally document `PSBMedia::GetListAt`.
   - Add tests using existing PSB/PIMG fixtures under `tests/test_files`.
3. `AlphaMovie.dll` / movie stack
   - Decide whether `AlphaMovie` should be a thin wrapper over the existing
     FFmpeg movie path or remain a documented no-op.
   - If implemented, expose state, dimensions, frame/time properties, and stop
     callbacks consistently.

Commit rule: each behavior area and its tests are committed separately.

## Phase 4: High-Value Missing Plugins

Goal: port or reimplement plugins that unlock broad game compatibility.

Priority candidates:

- Data/parsing: `base64`, `binaryStream`, `encode`, `expat`, `lineParser`,
  `memfile`, `minizip`, `sqlite3`, `sqlite3_xp3_vfs`
- Network: `httprequest`, `httpserv`, `xmlhttprequest`
- Graphics/image: `imagesaver`, `layerEx`, `layerExSave`, `layerExGdiPlus`,
  `qrcode`
- Scripting/runtime: `javascript`, `squirrel`, `onigruma`
- System/Windows compatibility: `win32ole`, `wsh`, `windowExProgress`

Selection rule: implement in the order they appear in real game logs or test
fixtures, not by trying to mechanically port every KiriKiri2 plugin.

Commit rule: one plugin per commit unless a helper library is shared.

## Phase 5: Compatibility Audit Tooling

Goal: make gaps visible and prevent regressions.

1. Add a small registry test that lists internal plugin module names and checks
   expected names.
2. Add a script or test fixture that compares AetherKiri plugin registrations
   with the KiriKiri2 reference directory.
3. Add log-based triage documentation:
   - missing module name
   - whether a no-op stub is acceptable
   - whether behavior must be implemented
   - affected game or fixture

Commit rule: tests/tooling first, documentation updates after.

## Current Next Step

Start with Phase 1. It is low risk, improves game startup compatibility, and
creates the test harness needed before deeper plugin work.
