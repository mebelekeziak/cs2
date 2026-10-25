"""Import an extracted, ZIP or VPK Source 2 skybox pack into workspace/skyboxes.

RAR packs should be extracted with an archive manager first. Only compiled
materials/textures are copied, preserving their materials/... resource paths.
No installed game files are changed, and existing files are never overwritten.
"""
from __future__ import annotations

import argparse
import io
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import struct
import tempfile
import zipfile
import zlib

MAX_FILES = 4096
MAX_FILE = 256 * 1024 * 1024
MAX_TOTAL = 4 * 1024 * 1024 * 1024
SUFFIXES = (".vtex_c", ".vmat_c")


def linked(path: Path) -> bool:
    s = path.lstat()
    return stat.S_ISLNK(s.st_mode) or bool(getattr(s, "st_file_attributes", 0) & 0x400)


def resource_name(name: str) -> str | None:
    """Portable case-folded material path, rejecting ambiguous Windows names."""
    name = name.replace("\\", "/")
    p = PurePosixPath(name)
    parts = name.split("/")
    if p.is_absolute() or any(part in ("", ".", "..") for part in parts):
        raise ValueError(f"Unsafe archive path: {name!r}")
    if any(ord(c) < 32 or ord(c) > 126 or c in ':*?"<>|' for c in name):
        raise ValueError(f"Nonportable resource path: {name!r}")
    reserved = {"con", "prn", "aux", "nul", *(f"com{i}" for i in range(1, 10)), *(f"lpt{i}" for i in range(1, 10))}
    if any(part.endswith((".", " ")) or part.split(".")[0].lower() in reserved for part in parts):
        raise ValueError(f"Ambiguous Windows path: {name!r}")
    # Accept a single enclosing archive folder, using the actual materials root.
    lower = [part.lower() for part in parts]
    if "materials" not in lower:
        return None
    parts = lower[lower.index("materials"):]
    result = "/".join(parts)
    if len(result) >= 256:
        raise ValueError("Resource path exceeds 255 characters")
    return result if result.endswith(SUFFIXES) else None


def check_resource(path: Path) -> None:
    """Validate the Source 2 container, without claiming full native decoding."""
    length = path.stat().st_size
    if length < 16 or length > MAX_FILE:
        raise ValueError(f"Invalid resource size: {path.name}")
    with path.open("rb") as f:
        size, header_version, _, offset, count = struct.unpack("<IHHII", f.read(16))
        begin = offset + 8
        if header_version != 12 or not 16 <= size <= length or not 0 < count <= 64 or begin + count * 12 > size:
            raise ValueError(f"Invalid Source 2 resource header: {path.name}")
        f.seek(begin)
        blocks = f.read(count * 12)
        found = False
        for i in range(count):
            kind, relative, n = struct.unpack_from("<4sII", blocks, i * 12)
            at = begin + i * 12 + 4 + relative
            if at > size or n > size - at:
                raise ValueError(f"Invalid resource block: {path.name}")
            if kind == b"DATA":
                if found:
                    raise ValueError(f"Duplicate DATA block: {path.name}")
                found = True
        if not found:
            raise ValueError(f"Resource has no DATA block: {path.name}")


def vpk_members(path: Path):
    """Yield (name, length, reader) for a bounded Valve VPK v1/v2 index."""
    with path.open("rb") as f:
        header = f.read(28)
        if len(header) < 12:
            raise ValueError("Truncated VPK header")
        magic, version, tree_size = struct.unpack_from("<III", header)
        header_size = 12 if version == 1 else 28
        if magic != 0x55AA1234 or version not in (1, 2) or tree_size > 64 * 1024 * 1024:
            raise ValueError("Unsupported or oversized VPK index")
        f.seek(header_size)
        tree = f.read(tree_size)
        if len(tree) != tree_size:
            raise ValueError("Truncated VPK directory")
    pos = 0

    def string():
        nonlocal pos
        end = tree.index(0, pos)
        result = tree[pos:end].decode("utf-8")
        pos = end + 1
        return result

    while extension := string():
        while folder := string():
            while filename := string():
                crc, preload_size, archive, offset, size, terminator = struct.unpack_from("<IHHIIH", tree, pos)
                pos += 18
                if terminator != 0xFFFF or pos + preload_size > len(tree):
                    raise ValueError("Invalid VPK directory entry")
                preload = tree[pos:pos + preload_size]
                pos += preload_size
                name = (folder + "/" if folder != " " else "") + filename + "." + extension
                source = path if archive == 0x7FFF else path.with_name(path.name.replace("_dir.vpk", f"_{archive:03}.vpk"))
                if archive != 0x7FFF and source == path:
                    raise ValueError("External VPK chunks require a *_dir.vpk index")
                at = offset + (header_size + tree_size if archive == 0x7FFF else 0)

                def reader(source=source, at=at, size=size, preload=preload, crc=crc):
                    if linked(source) or not source.is_file():
                        raise ValueError("VPK chunk must be a regular file")
                    if at + size > source.stat().st_size:
                        raise ValueError("VPK resource is truncated")
                    with source.open("rb") as f:
                        f.seek(at)
                        data = preload + f.read(size)
                    if len(data) != len(preload) + size or zlib.crc32(data) & 0xFFFFFFFF != crc:
                        raise ValueError("VPK resource CRC mismatch")
                    return io.BytesIO(data)

                yield name, size + preload_size, reader


def source_members(source: Path):
    if linked(source):
        raise ValueError("Pack source must not be a link or reparse point")
    if source.is_dir():
        count = 0
        for directory, folders, files in os.walk(source, followlinks=False):
            for folder in folders:
                if linked(Path(directory) / folder):
                    raise ValueError("Pack directory contains a link or reparse point")
            for filename in files:
                count += 1
                if count > 16384:
                    raise ValueError("Pack contains too many files")
                file = Path(directory) / filename
                if linked(file) or not file.is_file():
                    raise ValueError("Pack contains a nonregular file")
                yield file.relative_to(source).as_posix(), file.stat().st_size, lambda file=file: file.open("rb")
    elif source.suffix.lower() == ".zip":
        with zipfile.ZipFile(source) as archive:
            if len(archive.infolist()) > 16384:
                raise ValueError("Pack contains too many archive entries")
            for item in archive.infolist():
                mode = item.external_attr >> 16
                if stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)):
                    raise ValueError("ZIP contains a nonregular entry")
                if item.is_dir():
                    resource_name(item.filename.rstrip("/"))
                    continue
                yield item.filename, item.file_size, lambda item=item: archive.open(item)
    elif source.suffix.lower() == ".vpk":
        yield from vpk_members(source)
    else:
        raise ValueError("Use an extracted pack directory, ZIP, or VPK; extract RAR packs first")


def check_destination(path: Path):
    # Check existing ancestors as well as final directory before resolving links.
    for ancestor in [path, *path.parents]:
        if ancestor.exists() or ancestor.is_symlink():
            if linked(ancestor) or not ancestor.is_dir():
                raise ValueError("Destination ancestors must be regular directories")


def import_pack(source: Path, destination: Path) -> list[str]:
    source = source.absolute()
    destination = destination.absolute()
    check_destination(destination)
    if source.is_dir() and (source == destination or source in destination.parents):
        raise ValueError("Destination cannot be inside the source pack")
    destination.parent.mkdir(parents=True, exist_ok=True)
    names: set[str] = set()
    total = 0
    installed: list[Path] = []
    with tempfile.TemporaryDirectory(prefix=".skybox-import-", dir=destination.parent) as tmp:
        stage = Path(tmp)
        for name, size, reader in source_members(source):
            name = resource_name(name)
            if name is None:
                continue
            if name in names:
                raise ValueError(f"Duplicate/case-colliding resource path: {name}")
            names.add(name)
            total += size
            if len(names) > MAX_FILES or not 16 <= size <= MAX_FILE or total > MAX_TOTAL:
                raise ValueError("Pack exceeds resource count/size limits")
            target = destination / name
            check_destination(target.parent)
            if target.exists() or target.is_symlink():
                raise ValueError(f"Refusing to overwrite existing resource: {name}")
            staged = stage / name
            staged.parent.mkdir(parents=True, exist_ok=True)
            with reader() as stream, staged.open("xb") as output:
                remaining = size
                while remaining:
                    data = stream.read(min(1024 * 1024, remaining))
                    if not data:
                        raise ValueError(f"Truncated resource: {name}")
                    output.write(data)
                    remaining -= len(data)
                if stream.read(1):
                    raise ValueError(f"Resource size changed: {name}")
            check_resource(staged)
        if not names:
            raise ValueError("No compiled .vtex_c or .vmat_c resources under materials/ were found")
        try:
            for name in sorted(names):
                target = destination / name
                check_destination(target.parent)
                target.parent.mkdir(parents=True, exist_ok=True)
                # Exclusive create remains safe if another importer starts meanwhile.
                with target.open("xb") as output:
                    installed.append(target)
                    with (stage / name).open("rb") as stream:
                        shutil.copyfileobj(stream, output, 1024 * 1024)
        except Exception:
            for target in reversed(installed):
                target.unlink(missing_ok=True)
            raise
    return sorted(names)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Extracted folder containing materials/, ZIP, or VPK index")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "skyboxes")
    args = parser.parse_args()
    try:
        imported = import_pack(args.source, args.output)
    except (OSError, ValueError, struct.error, zipfile.BadZipFile) as error:
        parser.exit(1, f"Skybox import failed: {error}\n")
    print(f"Imported {len(imported)} compiled resources into {args.output.absolute()}")
    print("Open World > Skybox > Refresh skyboxes. Single-cube textures appear as Custom entries.")
    print("Source 1 VTF/VMT files and ordinary PNG/JPG images need conversion with CS2 Workshop Tools.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
