"""Read-only CS2 custom-model resource and direct-dependency inspection.

Uses only Python's standard library. Reads compiled-model headers/RERL blocks
and csgo/core VPK directory trees; no mesh data, processes or game files change.
The result establishes resource presence, not successful engine loading or pose.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game")
WORLD_GRAPH = "animation/graphs/worldmodel/worldmodel.vnmgraph"
MAX_TREE = 64 * 1024 * 1024
MAX_REFS = 4 * 1024 * 1024


def read_at(stream, offset, count):
    stream.seek(offset)
    data = stream.read(count)
    if len(data) != count:
        raise ValueError("Truncated metadata")
    return data


def text_at(data, offset):
    if offset < 0 or offset >= len(data):
        raise ValueError("String outside metadata")
    end = data.find(b"\0", offset, min(len(data), offset + 1025))
    if end < 0:
        raise ValueError("Unterminated metadata string")
    return data[offset:end].decode("utf-8"), end + 1


def normalized(value):
    value = value.replace("\\", "/").lower()
    if not value or len(value) > 1024 or ":" in value or any(ord(x) < 32 or ord(x) == 127 for x in value):
        raise ValueError("Invalid resource path")
    if any(x in ("", ".", "..") or x.endswith((".", " ")) for x in value.split("/")):
        raise ValueError("Non-relative resource path")
    return value


def model_references(path):
    size = path.stat().st_size
    with path.open("rb") as stream:
        file_size, header_version, resource_version, table_offset, count = struct.unpack("<IHHII", read_at(stream, 0, 16))
        if not 16 <= file_size <= size or header_version != 12 or resource_version != 1 or not 0 < count <= 512:
            raise ValueError("Unsupported compiled-model header")
        table_start = table_offset + 8
        if table_start < 16 or table_start + count * 12 > file_size:
            raise ValueError("Compiled-model block table outside file")
        table = read_at(stream, table_start, count * 12)
        found = False
        refs = []
        for i in range(count):
            tag, relative, block_size = struct.unpack_from("<4sII", table, i * 12)
            block_start = table_start + i * 12 + 4 + relative
            if block_start + block_size > file_size:
                raise ValueError("Compiled-model block outside file")
            if tag != b"RERL":
                continue
            if found or block_size > MAX_REFS:
                raise ValueError("Invalid compiled-model references")
            found = True
            block = read_at(stream, block_start, block_size)
            offset, entries = struct.unpack_from("<II", block)
            if offset < 8 or not 0 < entries <= 4096 or offset + entries * 16 > len(block):
                raise ValueError("Compiled-model reference table outside block")
            for j in range(entries):
                field = offset + j * 16 + 8
                string_at = field + struct.unpack_from("<q", block, field)[0]
                name, _ = text_at(block, string_at)
                refs.append(normalized(name))
        if not found:
            raise ValueError("Compiled model has no external references")
        return refs


def vpk_names(path):
    if not path.exists():
        return set()
    with path.open("rb") as stream:
        magic, version, tree_size = struct.unpack("<III", read_at(stream, 0, 12))
        header = 12 if version == 1 else 28
        if magic != 0x55AA1234 or version not in (1, 2) or not 0 < tree_size <= MAX_TREE:
            raise ValueError(f"Unsupported VPK directory: {path}")
        inline_end = path.stat().st_size
        if version == 2:
            data_size = struct.unpack_from("<I", read_at(stream, 0, 28), 12)[0]
            if header + tree_size + data_size > inline_end:
                raise ValueError("Invalid VPK inline section size")
            inline_end = header + tree_size + data_size
        tree = read_at(stream, header, tree_size)
    names, archives = set(), {0x7FFF: inline_end}
    at = 0
    while True:
        extension, at = text_at(tree, at)
        if not extension:
            break
        while True:
            folder, at = text_at(tree, at)
            if not folder:
                break
            while True:
                base, at = text_at(tree, at)
                if not base:
                    break
                _, preload, archive, offset, length, terminator = struct.unpack_from("<IHHIIH", tree, at)
                at += 18
                if terminator != 0xFFFF or at + preload > len(tree):
                    raise ValueError("Invalid VPK directory entry")
                at += preload
                name = normalized(("" if folder == " " else folder + "/") + base +
                                  ("" if extension == " " else "." + extension))
                if not length and preload:
                    names.add(name)
                    continue
                if archive not in archives:
                    archive_path = path if archive == 0x7FFF else path.with_name(path.name.replace("_dir.vpk", f"_{archive:03}.vpk"))
                    archives[archive] = archive_path.stat().st_size if archive_path.is_file() else 0
                data_start = header + tree_size if archive == 0x7FFF else 0
                if archives[archive] and data_start + offset + length <= archives[archive]:
                    names.add(name)
    if at != len(tree):
        raise ValueError("Unexpected VPK tree tail")
    return names


def loose_exists(root, name):
    path = root / name
    return path.is_file() and path.resolve().is_relative_to(root.resolve())


def helper_name(name):
    name = name.lower()
    return "nohitbox" in name or "no_hitbox" in name or any(
        x in {"arm", "arms", "null", "helper"} for x in re.split(r"[/ _\-.()\[\]]", name))


def inspect(game, archive=None):
    csgo, core = game / "csgo", game / "core"
    packed = vpk_names(csgo / "pak01_dir.vpk") | vpk_names(core / "pak01_dir.vpk")
    models = []
    for folder in ("agents", "characters", "models", "phase2"):
        if not (csgo / folder).is_dir():
            continue
        for path in sorted((csgo / folder).rglob("*.vmdl_c")):
            item = {"path": path.relative_to(csgo).as_posix(), "size": path.stat().st_size}
            try:
                refs = model_references(path)
                item["animation"] = "installed_ag2" if WORLD_GRAPH in refs else "other_ag2" if any(x.endswith(".vnmgraph") for x in refs) else "legacy" if any(x.endswith(".vanmgrph") for x in refs) else "other"
                item["missing_direct_refs"] = [r for r in refs if r + "_c" not in packed
                    and not loose_exists(csgo, r + "_c") and not loose_exists(core, r + "_c")]
                item["direct_refs_present"] = not item["missing_direct_refs"]
                item["passes_resource_validation"] = item["animation"] == "installed_ag2" and item["direct_refs_present"]
            except (OSError, ValueError, struct.error) as exc:
                item["error"] = str(exc)
                item["passes_resource_validation"] = False
            models.append(item)
    examples = [x for x in models if "/hmn_ag2/zenless_zone_zero/" in x["path"]]
    result = {"utc": datetime.now(timezone.utc).isoformat(), "game": str(game.resolve()),
        "steam_inf": (csgo / "steam.inf").read_text(encoding="utf-8-sig"),
        "default_renderer": "D3D11 (-dx11)" if re.search(r'"DefaultRenderSystem"\s+"-dx11"',
            (csgo / "gameinfo.gi").read_text(encoding="utf-8-sig")) else "Not established",
        "live_process_or_render_observation": False,
        "loose_model_candidates": len(models),
        "animation_counts": dict(Counter(x.get("animation", "malformed") for x in models)),
        "candidates_with_missing_direct_refs": sum(bool(x.get("missing_direct_refs")) for x in models),
        "passing_direct_resource_validation": sum(x["passes_resource_validation"] for x in models),
        "catalog_candidates_after_helper_filter": sum(x["passes_resource_validation"] and not helper_name(x["path"]) for x in models),
        "packed_resource_names": len(packed), "zenless_zone_zero_examples": examples,
        "limitations": ["Direct references only; transitive payloads and mesh/skeleton fit are not validated.",
            "Installed search roots include csgo/core; runtime engine loading remains unobserved.",
            "Arms/helpers are separately removed by catalog filename rules."]}
    identities = {}
    for rel in ("bin/win64/cs2.exe", "csgo/bin/win64/client.dll", "bin/win64/animationsystem.dll", "bin/win64/rendersystemdx11.dll"):
        path = game / rel
        raw = path.read_bytes()
        pe = struct.unpack_from("<I", raw, 0x3C)[0]
        identities[rel] = {"sha256": hashlib.sha256(raw).hexdigest(),
            "machine": hex(struct.unpack_from("<H", raw, pe + 4)[0]),
            "timestamp": hex(struct.unpack_from("<I", raw, pe + 8)[0])}
    result["identities"] = identities
    if archive:
        with zipfile.ZipFile(archive) as z:
            names = z.namelist()
            model_names = [x for x in names if x.endswith(".vmdl_c")]
            result["archive"] = {"path": str(archive.resolve()), "entries": len(names),
                "size": archive.stat().st_size,
                "models": len(model_names), "models_not_installed": [x for x in model_names if not (csgo / x).is_file()],
                "readme": z.read("README.txt").decode("utf-8-sig")}
    return result, models


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--details", action="store_true", help="Include every model entry in JSON")
    args = parser.parse_args()
    result, models = inspect(args.game, args.archive)
    if args.details:
        result["models"] = models
    report = json.dumps(result, indent=2, ensure_ascii=True) + "\n"
    if args.out:
        destination = args.out.resolve()
        if not destination.is_relative_to(ROOT / "logs"):
            raise SystemExit("Diagnostics must stay under the workspace logs directory")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(report, encoding="utf-8")
    print(f"Model assets: {result['passing_direct_resource_validation']} pass AG2/direct-resource validation / {result['loose_model_candidates']} candidates")
    print("Animation kinds:", result["animation_counts"])
    print("Catalog candidates after helper names:", result["catalog_candidates_after_helper_filter"])
    print("Zenless Zone Zero hmn_ag2:", len(result["zenless_zone_zero_examples"]), "examples")
    print("Static resource inspection only; no engine load or live appearance observation")
    if args.out:
        print(f"Report: {destination}")
    return 0 if result["passing_direct_resource_validation"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
