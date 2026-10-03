# Ren'Py SDK smoke fixture

This is a source-only Ren'Py project in the layout produced by the official
Ren'Py SDK launcher. It intentionally has no images, audio, compiled scripts,
or SDK files. The script uses only built-in displayables and exercises startup,
dialogue, menu input, and a clean return from `label start`.

Run the smoke check with an extracted official Ren'Py SDK:

```sh
RENPY_SDK=/path/to/renpy-8.x-sdk tools/run_renpy_smoke.sh
```

`tools/run_renpy_smoke.sh` copies the fixture to a temporary directory before
asking the SDK to compile and initialize it. SDK-generated files are removed
with that temporary directory, so this fixture remains source-only.

This automated check covers script compilation and project initialization only;
it does not exercise dialogue, choice input, rendering, audio, or the AetherKiri
provider. For interactive verification, run the fixture with the SDK launcher
and choose Start. Do not interpret this check as AetherKiri runtime support.

The SDK is not downloaded or bundled by the runner. See the official
[SDK downloads](https://www.renpy.org/latest.html) and
[command-line documentation](https://www.renpy.org/doc/html/cli.html).
