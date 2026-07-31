#!/usr/bin/env python3
"""Inject one release version into Godot and every platform export preset."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


SEMVER_PATTERN = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?$"
)


def replace_exact(
    path: Path,
    pattern: str,
    replacement: str,
    expected_count: int,
    *,
    check: bool,
) -> None:
    original = path.read_text(encoding="utf-8")
    updated, count = re.subn(pattern, replacement, original, flags=re.MULTILINE)
    if count != expected_count:
        raise SystemExit(
            f"{path}: expected {expected_count} matches for {pattern!r}, found {count}"
        )
    if check:
        if updated != original:
            raise SystemExit(f"{path}: release version metadata is out of sync")
        return
    path.write_text(updated, encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Set the Aether release version in Godot project metadata."
    )
    parser.add_argument("version", help="SemVer release tag, with an optional v prefix")
    parser.add_argument(
        "--build-number",
        default="1",
        help="Positive integer used for Android versionCode and Apple CFBundleVersion",
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--github-env",
        type=Path,
        help="Append normalized version values to this GitHub Actions environment file",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify metadata without modifying files",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    version = args.version.removeprefix("v")
    match = SEMVER_PATTERN.fullmatch(version)
    if match is None:
        raise SystemExit(
            f"invalid release version {args.version!r}; expected SemVer such as "
            "0.3.0 or v0.3.0-beta.1"
        )

    build_number = args.build_number
    if not build_number.isascii() or not build_number.isdigit():
        raise SystemExit("build number must be a positive integer")
    build_number_value = int(build_number)
    if not 1 <= build_number_value <= 2_100_000_000:
        raise SystemExit("build number must be between 1 and 2100000000")
    build_number = str(build_number_value)

    apple_version = ".".join(match.group(index) for index in range(1, 4))
    project_root = args.project_root.resolve()
    project_file = project_root / "apps/godot_app/project.godot"
    export_file = project_root / "apps/godot_app/export_presets.cfg"

    replace_exact(
        project_file,
        r'^config/version="[^"]*"$',
        f'config/version="{version}"',
        1,
        check=args.check,
    )
    replace_exact(
        export_file,
        r'^application/short_version="[^"]*"$',
        f'application/short_version="{apple_version}"',
        4,
        check=args.check,
    )
    replace_exact(
        export_file,
        r'^application/version="[^"]*"$',
        f'application/version="{build_number}"',
        4,
        check=args.check,
    )
    replace_exact(
        export_file,
        r"^version/code=[0-9]+$",
        f"version/code={build_number}",
        2,
        check=args.check,
    )
    replace_exact(
        export_file,
        r'^version/name="[^"]*"$',
        f'version/name="{version}"',
        2,
        check=args.check,
    )

    if args.github_env is not None and not args.check:
        with args.github_env.open("a", encoding="utf-8") as github_env:
            github_env.write(f"AETHER_VERSION={version}\n")
            github_env.write(f"AETHER_APPLE_VERSION={apple_version}\n")
            github_env.write(f"AETHER_BUILD_NUMBER={build_number}\n")

    action = "Verified" if args.check else "Set"
    print(
        f"{action} Aether version {version} "
        f"(Apple {apple_version}, build {build_number})"
    )


if __name__ == "__main__":
    main()
