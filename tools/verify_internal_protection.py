#!/usr/bin/env python3
import argparse
import json
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


def is_internal_source(path: str) -> bool:
    normalized = path.replace("\\", "/")
    return "/packages/AetherInternal/" in normalized


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify source-scoped AetherInternal obfuscation flags"
    )
    parser.add_argument("compile_commands", type=Path)
    args = parser.parse_args()

    with args.compile_commands.open("r", encoding="utf-8") as handle:
        entries = json.load(handle)

    internal_entries = []
    leaked_entries = []
    missing_entries = []
    for entry in entries:
        source = str(entry.get("file", ""))
        command = entry_command(entry)
        has_markers = all(marker in command for marker in PROTECTION_MARKERS)
        if is_internal_source(source):
            internal_entries.append(source)
            if not has_markers:
                missing_entries.append(source)
        elif any(marker in command for marker in PROTECTION_MARKERS):
            leaked_entries.append(source)

    if not internal_entries:
        raise SystemExit("no AetherInternal compilation units were found")
    if missing_entries:
        raise SystemExit(
            "Internal sources missing protection flags:\n"
            + "\n".join(sorted(set(missing_entries)))
        )
    if leaked_entries:
        raise SystemExit(
            "protection flags leaked into public sources:\n"
            + "\n".join(sorted(set(leaked_entries)))
        )

    unique_sources = len(set(internal_entries))
    print(f"verified {unique_sources} protected Internal compilation units")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
