# CatSystem2 Runtime Progress

This document records evidence and checkpoints for the CatSystem2 runtime
provider (`packages/AetherInternal/src/catsystem2`). Machine-local game
paths, extracted assets, saves, and IDA databases are excluded from the
repository.

## Stage 0: Windows Production Build

Status: verified.

- The public `fix/windows-msvc-production-build` branch builds
  `engine_api.dll` with the CatSystem2 runtime under MSVC Release
  (`/bigobj`; Debug conflicts with the internal `/O2` artemis flags).
- Godot 4.7 export with embedded PCK launches the shell and imports
  titles. Sidecar engine logging (`aetherkiri-engine.log`, flushed per
  line) and native crash capture (`%LOCALAPPDATA%/AetherKiri/crash`)
  are available in release builds.

## Stage 1: Path And Title-Start Repair

Status: verified on Windows x64 release.

- Non-ASCII game roots crashed `open_game`: `path::string()` converts
  through the system ANSI code page. All CatSystem2 path handling now
  uses `u8string()`/`generic_u8string()`, native path concatenation for
  temporary files, stream-based `soundconf.xml` parsing, and
  `FT_New_Memory_Face` font loading with runtime-owned buffers.
- Two defects blocked starting a game from the title:
  - The authored KEYWAIT polling page compares `_CLICK_L_` against the
    key-mapped control name (`btnmap[0]`), while hit testing found the
    image-layer button `btn[0]`. `DispatchFesEvent` now publishes the
    same-owner mapped sibling name when it exists, so `GO_STORY` runs.
  - A plain localization patch archive shadowed the retail encrypted
    `op.cst` with a re-encoded body the stock zlib interpreter cannot
    read, failing every tick with "unable to decompress CatScene body".
    Plain script-class entries now require a decodable container
    (CatScene zlib header, FEN2/GES magic) before they may replace an
    encrypted original.
- Blank-area clicks complete foreground frame waits without requiring
  the keyskip latch, matching the native advance gesture; authored logo
  and caution sequences are skippable.
- Verified end to end by scripted clicks: title menu -> GO_STORY ->
  `sscript.kcs` -> OP movie playback (non-black captured frames) ->
  responsive message-window toolbar and confirm dialogs.

## Diagnostics

- `AETHERKIRI_CATSYSTEM2_DUMP_FES=1` dumps loaded FES program structure
  (tags, conditionals, commands) through the engine log.
- `AETHERKIRI_ENABLE_AUTO_START=1` with
  `AETHERKIRI_AUTO_START_GAME=<path>` and `AETHERKIRI_AUTO_PROBE_CLICKS`
  drives unattended verification runs without new host scripts.
- CatScene load failures report the script name, compressed and
  decompressed sizes, and the body head hex.
