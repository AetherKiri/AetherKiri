# AetherKiri KAG3 Demo

This is the source tree for the demo game bundled with AetherKiri. It retains
the original KiriKiri/KAG3 feature tour, replaces the former KiriKiri SDL2
section with an AetherKiri introduction, and provides Japanese, English,
Simplified Chinese, Traditional Chinese, and Korean entry points.

The self-contained game files are under `data/`. Build the product seed with:

```bash
KRKRREL_BIN=/path/to/krkrrel ./build.sh
```

The script writes the archive used by the Godot product to
`apps/godot_app/builtin_demos/aetherkiri-kag3/data.xp3`. After rebuilding,
increment `BuiltinDemo.DEMO_VERSION` when an installed copy must be upgraded
and update `manifest.json` with the new size and SHA-256.

The demo follows the KAG3 license in `LICENSE.txt`. The bundled Noto Sans CJK
font follows the license stored at
`data/system_polyfill/NotoSansCJK-LICENSE.txt`.
