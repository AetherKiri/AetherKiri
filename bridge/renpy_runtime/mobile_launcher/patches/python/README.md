# Ren'Py Python cooperative-loop patch skeleton

`0001-cooperative-loop-skeleton.patch` is an opt-in source patch against an
official Ren'Py source checkout. It applies cleanly to the current source
layout and is syntax-checked by the mobile launcher scaffold.

Apply or validate it with:

```text
bridge/renpy_runtime/mobile_launcher/build.sh \\
  --check-python-patch --renpy-src /path/to/renpy

bridge/renpy_runtime/mobile_launcher/build.sh \\
  --apply-python-patch --renpy-src /path/to/renpy
```

The apply command refuses a dirty checkout, runs `git apply --check`, applies
the patch, and runs `py_compile` on all three changed modules. It does not
commit the source or build an archive.

## What the skeleton adds

- `renpy/main.py`: an opt-in monotonic tick deadline, begin/end helpers, and a
  host-safe cleanup hook that deliberately avoids Android task finish and
  `System.exit`
- `renpy/execution.py`: `CooperativeYield` and an opt-in deadline check at the
  context boundary; default `run_context(top)` behavior is preserved
- `renpy/display/core.py`: one non-blocking event poll primitive and a small
  interface hook

These are explicit seams for the native fork. They do not yet turn
`run_context`, `main.run`, or `Interface.interact_core` into resumable
coroutines. The current Ren'Py execution path can still block in script
execution and event handling. A playable Android/iOS fork must extend this
patch to preserve context state across ticks, dispatch input, render/publish
frames, and call `cooperative_shutdown` on the interpreter-owning thread.

The patch intentionally contains no replacement for `SDL_main`,
`launcher_main`, `Py_RunMain`, `SDL_RunApp`, or `UIApplicationMain`, and it
must never be treated as a playable binary by itself.
