"""Generate immutable cosmetic metadata from the installed CS2 VPK, read-only.

Uses Valve's scripts/items/items_game.txt, English localization, and generated
inventory-icon resource names. No web list, memory access, or game modification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from material_vpk_probe import entries

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game")


def decode_text(data):
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    return data.decode("utf-8-sig")


def keyvalues(text):
    """Read the quoted/unquoted, comment-containing KeyValues text dialect.

    Preserve duplicate keys as pairs; Valve loot lists contain repeated keys.
    Escape handling retains path separators and only consumes known escapes.
    """
    tokens = []
    pattern = re.compile(r'\s+|//[^\n]*|/\*.*?\*/|"((?:\\.|[^"\\])*)"|([{}])|([^\s{}"]+)', re.S)
    at = 0
    for match in pattern.finditer(text):
        if match.start() != at:
            raise ValueError("Unrecognized KeyValues text")
        at = match.end()
        if match[1] is not None:
            value = re.sub(r'\\([\\"nt])', lambda m: {"n": "\n", "t": "\t"}.get(m[1], m[1]), match[1])
            tokens.append((value, False))
        elif match[2] is not None:
            tokens.append((match[2], True))
        elif match[3] is not None:
            tokens.append((match[3], False))
    if at != len(text):
        raise ValueError("Truncated KeyValues token")
    pos = 0

    def block(nested):
        nonlocal pos
        result = []
        while pos < len(tokens):
            key, structural = tokens[pos]
            pos += 1
            if structural and key == "}":
                if not nested:
                    raise ValueError("Unexpected closing brace")
                return result
            if structural or pos == len(tokens):
                raise ValueError("Missing KeyValues value")
            value, structural = tokens[pos]
            pos += 1
            if structural:
                if value != "{":
                    raise ValueError("Unexpected closing value brace")
                value = block(True)
            result.append((key, value))
        if nested:
            raise ValueError("Unclosed KeyValues block")
        return result

    return block(False)


def fields(pairs):
    result = {}
    for key, value in pairs:
        key = key.lower()
        if isinstance(value, list) and isinstance(result.get(key), list):
            result[key] = result[key] + value
        else:
            result[key] = value
    return result


def load_resources(game):
    index = game / "csgo/pak01_dir.vpk"
    wanted = {"scripts/items/items_game.txt", "resource/csgo_english.txt"}
    resources, names = {}, set()
    for name, archive, offset, length, preload, start in entries(index):
        names.add(name)
        if name not in wanted:
            continue
        path = index if archive == 0x7fff else index.with_name(index.name.replace("_dir.vpk", f"_{archive:03}.vpk"))
        with path.open("rb") as stream:
            stream.seek(offset + (start if archive == 0x7fff else 0))
            tail = stream.read(length)
        if len(tail) != length:
            raise ValueError(f"Truncated VPK resource: {name}")
        resources[name] = preload + tail
    if resources.keys() != wanted:
        raise ValueError("Installed VPK lacks required item/localization resources")
    return resources, names


def build_catalog(item_bytes, localization_bytes, resource_names):
    schema = fields(fields(keyvalues(decode_text(item_bytes)))["items_game"])
    language = fields(fields(keyvalues(decode_text(localization_bytes)))["lang"])
    labels = fields(language["tokens"])
    prefabs = fields(schema["prefabs"])
    resolved = {}

    def resolve(raw, stack=()):
        merged = {}
        raw = fields(raw)
        for prefab in raw.get("prefab", "").split():
            if prefab in stack:
                raise ValueError("Cyclic item prefab")
            if prefab not in resolved:
                resolved[prefab] = resolve(prefabs[prefab], stack + (prefab,))
            # Nested KeyValues inherit independently, as item definitions do.
            merge(merged, resolved[prefab])
        merge(merged, raw)
        return merged

    def merge(dest, source):
        for key, value in source.items():
            if isinstance(value, list):
                nested = fields(value)
                if isinstance(dest.get(key), list):
                    parent = fields(dest[key])
                    merge(parent, nested)
                    nested = parent
                dest[key] = list(nested.items())
            else:
                dest[key] = value

    def label(token, fallback):
        return labels.get(token.lstrip("#").lower(), fallback)

    items = []
    for definition, raw in fields(schema["items"]).items():
        if not definition.isdigit():
            continue
        raw_name = fields(raw).get("name", "")
        if not (raw_name.startswith("weapon_") or "glove" in raw_name or "handwrap" in raw_name):
            continue
        entry = resolve(raw)
        name = entry.get("name", "")
        slot = entry.get("item_slot", "")
        category = entry.get("flexible_loadout_category", "")
        if slot == "hands" or category == "hands" or entry.get("flexible_loadout_slot") == "clothing_hands":
            kind = "Gloves"
        elif category == "melee" or slot == "melee":
            kind = "Knife"
        elif name.startswith("weapon_") and (category in {"secondary", "smg", "rifle", "heavy"} or
                                           fields(entry.get("capabilities", [])).get("paintable") == "1"):
            kind = "Weapon"
        elif name.startswith("weapon_") and entry.get("item_type_name", "").lower() in {"#csgo_type_pistol", "#csgo_type_rifle", "#csgo_type_sniperrifle", "#csgo_type_smg", "#csgo_type_shotgun", "#csgo_type_machinegun"}:
            kind = "Weapon"
        else:
            continue
        if entry.get("enabled", "1") == "0":
            continue
        classes = fields(entry.get("used_by_classes", []))
        mask = (1 if classes.get("terrorists") == "1" else 0) | (2 if classes.get("counter-terrorists") == "1" else 0)
        items.append({"id": int(definition), "name": name, "label": label(entry.get("item_name", ""), name),
                      "model": entry.get("model_player", ""), "kind": kind, "team_mask": mask,
                      "compatibility": set()})
    paints = []
    for number, raw in fields(schema["paint_kits"]).items():
        if not number.isdigit():
            continue
        entry = fields(raw)
        # 9001 is Valve's editor-only workshop template, not an actual finish.
        if entry.get("name") == "workshop_default":
            continue
        paints.append({"id": int(number), "name": entry["name"],
                       "label": "Default finish" if int(number) == 0 else label(entry.get("description_tag", ""), entry["name"]),
                       "min": float(entry.get("wear_remap_min", "0.06")),
                       "max": float(entry.get("wear_remap_max", "0.8")),
                       "legacy": entry.get("use_legacy_model", "0") == "1"})
    by_paint = {paint["name"]: paint["id"] for paint in paints}
    by_item = {item["name"]: item for item in items}
    # Longest definition name first prevents weapon_m4a1 from swallowing
    # weapon_m4a1_silencer icon names. Exact suffix lookup avoids phase ambiguity.
    item_names = sorted(by_item, key=len, reverse=True)
    prefix = "panorama/images/econ/default_generated/"
    for resource in sorted(resource_names):
        if not resource.startswith(prefix):
            continue
        basename = resource[len(prefix):]
        for item_name in item_names:
            if not basename.startswith(item_name + "_"):
                continue
            paint_name = re.sub(r"_(?:light|medium|heavy)_png\.vtex_c$", "", basename[len(item_name) + 1:])
            if paint_name in by_paint:
                by_item[item_name]["compatibility"].add(by_paint[paint_name])
            break

    def reference_walk(pairs):
        for key, value in pairs:
            if isinstance(value, list):
                reference_walk(value)
            match = re.fullmatch(r"\[([^]]+)\](.+)", key)
            if match and match[1] in by_paint and match[2] in by_item:
                by_item[match[2]]["compatibility"].add(by_paint[match[1]])
    reference_walk(schema.get("client_loot_lists", []))
    reference_walk(schema.get("item_sets", []))
    for item in items:
        item["compatibility"] = sorted({0} | item["compatibility"])
    return sorted(items, key=lambda item: item["id"]), sorted(paints, key=lambda paint: paint["id"])


def cpp_string(value):
    # Escaping non-ASCII as UTF-8 bytes avoids dependence on the compiler's code
    # page. Octal escapes cannot consume following hexadecimal label characters.
    return '"' + ''.join(chr(x) if 32 <= x < 127 and chr(x) not in '\\"' else
                         '\\' + chr(x) if chr(x) in '\\"' else f"\\{x:03o}"
                         for x in value.encode("utf-8")) + '"'


def render(items, paints, provenance):
    output = ["// Generated by tools/generate_skin_catalog.py; do not edit.",
              "// Source: installed Valve CS2 item schema, English localization and icon index."]
    for key, value in provenance.items():
        output.append(f"// {key}: {value}")
    output.append("constexpr const char* kSourceDescription = " + cpp_string("Installed CS2 " + str(provenance["steam.inf"]).split("PatchVersion=")[1].split(";")[0] +
                  f" | {len(items)} item definitions | {len(paints) - 1} finishes") + ";")
    output.append("constexpr Item kItems[] = {")
    for item in items:
        strings = ", ".join(cpp_string(item[key]) for key in ("name", "label", "model"))
        output.append(f'    {{{item["id"]}, {strings}, ItemKind::{item["kind"]}, {item["team_mask"]}}},')
    output += ["};", "constexpr Paint kPaints[] = {"]
    for paint in paints:
        strings = ", ".join(cpp_string(paint[key]) for key in ("name", "label"))
        output.append(f'    {{{paint["id"]}, {strings}, {paint["min"]:.6f}f, {paint["max"]:.6f}f, {str(paint["legacy"]).lower()}}},')
    output += ["};", "constexpr int kCompatiblePaints[] = {"]
    ranges = []
    offset = 0
    for item in items:
        paints_for_item = item["compatibility"]
        output.append("    " + ", ".join(map(str, paints_for_item)) + ",")
        ranges.append(f'    {{{item["id"]}, {offset}, {len(paints_for_item)}}},')
        offset += len(paints_for_item)
    output += ["};", "constexpr PaintRange kPaintRanges[] = {", *ranges, "};", ""]
    return "\n".join(output)


def generate(game):
    resources, names = load_resources(game)
    items, paints = build_catalog(resources["scripts/items/items_game.txt"], resources["resource/csgo_english.txt"], names)
    provenance = {key + " SHA-256": hashlib.sha256(value).hexdigest() for key, value in sorted(resources.items())}
    provenance["steam.inf"] = (game / "csgo/steam.inf").read_text(encoding="utf-8-sig").replace("\n", "; ").strip()
    provenance["item count"] = len(items)
    provenance["paint count (including default)"] = len(paints)
    return render(items, paints, provenance), items, paints, provenance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--output", type=Path, default=ROOT / "src/skin_catalog_data.inc")
    parser.add_argument("--check", action="store_true", help="Verify the checked-in catalog without replacing it")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    generated, items, paints, provenance = generate(args.game)
    if args.check:
        if args.output.read_text(encoding="utf-8") != generated:
            raise SystemExit("Catalog differs from installed data; regenerate and review")
    else:
        args.output.write_text(generated, encoding="utf-8")
    summary = {"provenance": provenance, "items": len(items), "paints_including_default": len(paints),
               "kinds": {kind: sum(item["kind"] == kind for item in items) for kind in ("Weapon", "Knife", "Gloves")},
               "nondefault_pairs": sum(len(item["compatibility"]) - 1 for item in items),
               "definitions_without_nondefault_finish": [item["name"] for item in items if len(item["compatibility"]) == 1]}
    if args.report:
        args.report.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
