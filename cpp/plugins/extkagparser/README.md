# ExtKAGParser compatibility plugin

This directory ports the `ExtKAGParser.dll` implementation from
[`krkrsdl3`](https://github.com/krkrsdl3/krkrsdl3), whose implementation in
turn derives from the KiriKiri/KAG parser by W.Dee and contributors.

Aether-specific adaptations keep the original script contract while using
Aether's UTF-16 `tjs_char`, portable storage/event headers, static plugin
registration, and repeatable module teardown. The corresponding source and
modifications are distributed here under the repository's documented
KiriKiri/krkrsdl3 licensing terms.
