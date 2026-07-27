#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


PROTECTION_MARKERS = (
    "-fpass-plugin=",
    "-kagura-config=",
    "-kagura-build-id=",
    "AETHERKIRI_KAGURA_RUNTIME=1",
)

ANDROID_FLA_MARKERS = (
    "AETHERKIRI_KAGURA_FLA=1",
    "AETHERKIRI_KAGURA_RUNTIME=1",
)

REQUIRED_PASSES = (
    "str",
    "str_aes",
    "mvo",
    "pe",
    "anti_debug",
)

REQUIRED_RUNTIME_SOURCES = (
    "/runtime/core/aes.c",
    "/runtime/anti_debug/anti_debug.c",
    "/runtime/ios/jailbreak_detection.c",
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
    parser.add_argument("--runtime-only", action="store_true")
    parser.add_argument("--fla-only", action="store_true")
    parser.add_argument("compile_commands", type=Path)
    args = parser.parse_args()

    with args.compile_commands.open("r", encoding="utf-8") as handle:
        entries = json.load(handle)

    if args.runtime_only and args.fla_only:
        parser.error("--runtime-only and --fla-only are mutually exclusive")

    if not args.runtime_only:
        policy_path = (
            Path(__file__).resolve().parents[1]
            / "cmake"
            / "internal_obfuscation.json"
        )
        with policy_path.open("r", encoding="utf-8") as handle:
            policy = json.load(handle)
        passes = policy.get("passes", {})
        required_passes = ("fla",) if args.fla_only else REQUIRED_PASSES
        disabled_passes = [name for name in required_passes if passes.get(name) is not True]
        if disabled_passes:
            raise SystemExit(
                "required Kagura passes are disabled: " + ", ".join(disabled_passes)
            )

    internal_entries = []
    leaked_entries = []
    missing_entries = []
    for entry in entries:
        source = str(entry.get("file", ""))
        command = entry_command(entry)
        if args.runtime_only:
            required_markers = ("AETHERKIRI_KAGURA_RUNTIME=1",)
        elif args.fla_only:
            required_markers = ANDROID_FLA_MARKERS
        else:
            required_markers = PROTECTION_MARKERS
        has_markers = all(marker in command for marker in required_markers)
        if is_internal_source(source):
            internal_entries.append(source)
            normalized_source = source.replace("\\", "/")
            if args.fla_only:
                requires_markers = "/packages/AetherInternal/src/" in normalized_source
            else:
                # Vendored Cubism is compiled as a dependency, not as owned
                # protected code. Scope the requirement to AetherInternal/src.
                requires_markers = "/packages/AetherInternal/src/" in normalized_source
            if requires_markers and not has_markers:
                missing_entries.append(source)
        elif any(
            marker in command
            for marker in required_markers
        ):
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

    compiled_sources = {
        str(entry.get("file", "")).replace("\\", "/") for entry in entries
    }
    missing_runtime_sources = [
        suffix
        for suffix in REQUIRED_RUNTIME_SOURCES
        if not any(source.endswith(suffix) for source in compiled_sources)
    ]
    if missing_runtime_sources:
        raise SystemExit(
            "Kagura runtime sources missing from target build:\n"
            + "\n".join(missing_runtime_sources)
        )

    unique_sources = len(set(internal_entries))
    print(f"verified {unique_sources} protected Internal compilation units")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
