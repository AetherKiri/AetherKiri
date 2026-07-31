#!/usr/bin/env python3
import json
import sys
from pathlib import Path


PROTECTION_MARKERS = (
    "-fpass-plugin=",
    "-kagura-config=",
    "-kagura-build-id=",
)


def entry_command(entry: dict) -> str:
    if "arguments" in entry:
        return " ".join(str(value) for value in entry["arguments"])
    return str(entry.get("command", ""))


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_internal_protection.py <compile_commands.json>")

    with Path(sys.argv[1]).open("r", encoding="utf-8") as handle:
        entries = json.load(handle)

    protected = []
    missing = []
    leaked = []
    for entry in entries:
        source = str(entry.get("file", "")).replace("\\", "/")
        command = entry_command(entry)
        has_markers = all(marker in command for marker in PROTECTION_MARKERS)
        owned = "/packages/AetherInternal/src/" in source
        if owned:
            protected.append(source)
            if not has_markers:
                missing.append(source)
        elif any(marker in command for marker in PROTECTION_MARKERS):
            leaked.append(source)

    if not protected:
        raise SystemExit("no AetherInternal owned compilation units were found")
    if missing:
        raise SystemExit(
            "Internal sources missing protection flags:\n"
            + "\n".join(sorted(set(missing)))
        )
    if leaked:
        raise SystemExit(
            "protection flags leaked into public sources:\n"
            + "\n".join(sorted(set(leaked)))
        )

    print(f"verified {len(set(protected))} protected Internal compilation units")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
