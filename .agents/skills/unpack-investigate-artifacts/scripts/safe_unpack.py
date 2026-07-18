#!/usr/bin/env python3
"""Safely extract ZIP-compatible or TAR artifacts into a new directory."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import tarfile
import zipfile


CHUNK_SIZE = 1024 * 1024


class UnpackError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(CHUNK_SIZE), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_relative(name: str) -> Path:
    normalized = name.replace("\\", "/")
    path = PurePosixPath(normalized)
    if not normalized or path.is_absolute() or ".." in path.parts:
        raise UnpackError(f"unsafe member path: {name!r}")
    if path.parts and ":" in path.parts[0]:
        raise UnpackError(f"drive-qualified member path: {name!r}")
    parts = [part for part in path.parts if part not in {"", "."}]
    if not parts:
        raise UnpackError(f"empty member path: {name!r}")
    return Path(*parts)


def prepare_destination(destination: Path) -> None:
    if destination.exists() and any(destination.iterdir()):
        raise UnpackError(f"destination is not empty: {destination}")
    destination.mkdir(parents=True, exist_ok=True)


def copy_member(source, target: Path, expected_size: int) -> dict[str, object]:
    target.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    written = 0
    with target.open("wb") as output:
        while True:
            chunk = source.read(CHUNK_SIZE)
            if not chunk:
                break
            written += len(chunk)
            if written > expected_size:
                raise UnpackError(f"member expanded beyond declared size: {target}")
            digest.update(chunk)
            output.write(chunk)
    if written != expected_size:
        raise UnpackError(
            f"member size mismatch for {target}: expected {expected_size}, wrote {written}"
        )
    return {"path": target.as_posix(), "size": written, "sha256": digest.hexdigest()}


def validate_limits(count: int, total: int, max_members: int, max_bytes: int) -> None:
    if count > max_members:
        raise UnpackError(f"member count exceeds limit: {count} > {max_members}")
    if total > max_bytes:
        raise UnpackError(f"expanded size exceeds limit: {total} > {max_bytes}")


def extract_zip(archive: Path, destination: Path,
                max_members: int, max_bytes: int) -> list[dict[str, object]]:
    manifest: list[dict[str, object]] = []
    with zipfile.ZipFile(archive) as package:
        members = package.infolist()
        total = sum(member.file_size for member in members)
        validate_limits(len(members), total, max_members, max_bytes)
        seen: set[Path] = set()
        validated: list[tuple[zipfile.ZipInfo, Path]] = []
        for member in members:
            relative = safe_relative(member.filename)
            if relative in seen:
                raise UnpackError(f"duplicate member path: {member.filename!r}")
            seen.add(relative)
            mode = member.external_attr >> 16
            if stat.S_ISLNK(mode):
                raise UnpackError(f"symbolic link is not allowed: {member.filename!r}")
            validated.append((member, relative))
        for member, relative in validated:
            target = destination / relative
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            with package.open(member) as source:
                entry = copy_member(source, target, member.file_size)
            entry["path"] = relative.as_posix()
            manifest.append(entry)
    return manifest


def extract_tar(archive: Path, destination: Path,
                max_members: int, max_bytes: int) -> list[dict[str, object]]:
    manifest: list[dict[str, object]] = []
    with tarfile.open(archive, "r:*") as package:
        members = package.getmembers()
        total = sum(member.size for member in members if member.isfile())
        validate_limits(len(members), total, max_members, max_bytes)
        seen: set[Path] = set()
        validated: list[tuple[tarfile.TarInfo, Path]] = []
        for member in members:
            relative = safe_relative(member.name)
            if relative in seen:
                raise UnpackError(f"duplicate member path: {member.name!r}")
            seen.add(relative)
            if not member.isdir() and not member.isfile():
                raise UnpackError(f"links and special files are not allowed: {member.name!r}")
            validated.append((member, relative))
        for member, relative in validated:
            if member.isdir():
                (destination / relative).mkdir(parents=True, exist_ok=True)
                continue
            source = package.extractfile(member)
            if source is None:
                raise UnpackError(f"unable to read member: {member.name!r}")
            with source:
                entry = copy_member(source, destination / relative, member.size)
            entry["path"] = relative.as_posix()
            manifest.append(entry)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--max-members", type=int, default=100_000)
    parser.add_argument("--max-bytes", type=int, default=8 * 1024 * 1024 * 1024)
    args = parser.parse_args()

    artifact = args.artifact.resolve()
    destination = args.output.resolve()
    if not artifact.is_file():
        raise UnpackError(f"artifact is not a file: {artifact}")
    prepare_destination(destination)

    if zipfile.is_zipfile(artifact):
        kind = "zip"
        files = extract_zip(artifact, destination, args.max_members, args.max_bytes)
    elif tarfile.is_tarfile(artifact):
        kind = "tar"
        files = extract_tar(artifact, destination, args.max_members, args.max_bytes)
    else:
        raise UnpackError("unsupported artifact; expected ZIP-compatible or TAR format")

    result = {
        "artifact": str(artifact),
        "artifact_size": artifact.stat().st_size,
        "artifact_sha256": sha256_file(artifact),
        "format": kind,
        "files": files,
        "file_count": len(files),
        "expanded_bytes": sum(int(item["size"]) for item in files),
    }
    rendered = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except UnpackError as error:
        raise SystemExit(f"safe_unpack: {error}") from error
