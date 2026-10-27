"""Verify read-only held-weapon and C4 ESP contracts against installed client files.

No process access, launches or game-file changes. Supports alternate source headers
for negative checks. Static contracts do not establish in-game rendering.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from verify_sky_draw import mapped, primary_vtable, require_bytes

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
SHA256 = "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"
SCHEMA = {
    "C_BasePlayerPawn": (0x21F67E8, 0x21F6450, 28, {"m_pWeaponServices": 0x12F0}),
    "CPlayer_WeaponServices": (0x222DDC8, 0x222DD30, 4, {"m_hActiveWeapon": 0x60}),
    "C_EconEntity": (0x2254FE8, 0x2254D40, 20, {"m_AttributeManager": 0x1290}),
    "C_AttributeContainer": (0x2254488, 0x2254410, 3, {"m_Item": 0x50}),
    "C_EconItemView": (0x2256918, 0x2256520, 31, {"m_iItemDefinitionIndex": 0x1BA}),
    "C_BaseEntity": (0x21F6E78, 0x21F5550, 82, {"m_hOwnerEntity": 0x520}),
    "C_C4": (0x221DCB8, 0x221DB60, 10, {"m_bBombPlanted": 0x1F5B}),
    "C_PlantedC4": (0x223E778, 0x223E3B0, 29, {
        "m_bBombTicking": 0x1288, "m_bHasExploded": 0x12BD, "m_bBombDefused": 0x12DC}),
}
WINDOWS = {
    "c4_plant_latch_read": (0x80A34C, "80bb5b1f0000007408488bcbe843a72e00"),
    "c4_plant_latch_write": (0x823347, "c6875b1f0000014533c9488b8bf0120000"),
    "planted_ticking_and_exploded": (0x82A34B, "80b988120000000f84b905000080b9bd120000000f85ac050000"),
    "defuse_stops_ticking": (0x8043AD, "80bf88120000007407c687881200000080bfdc120000010f844f020000c687dc12000001"),
    "planted_defused_and_exploded": (0xCBE62F, "80b9dc120000000f85bb00000080b9bd120000000f84ae000000"),
}


def require(ok, reason):
    if not ok:
        raise SystemExit(reason)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=CLIENT)
    parser.add_argument("--header", type=Path, default=ROOT / "include/esp_item_offsets.h")
    args = parser.parse_args()
    raw, image, base, _ = mapped(args.client)
    require(hashlib.sha256(raw).hexdigest() == SHA256, "Changed client; repeat ESP item contract investigation")
    ptr = lambda at: struct.unpack_from("<Q", image, at)[0] - base
    u32 = lambda at: struct.unpack_from("<I", image, at)[0]

    def string(at):
        require(0 < at < len(image), "Invalid schema string")
        end = image.find(0, at, min(len(image), at + 256))
        require(end >= at, "Unterminated schema string")
        return image[at:end].decode("ascii")

    fields = {}
    for name, (registration, array, count, expected) in SCHEMA.items():
        require(string(ptr(registration)) == name and ptr(registration + 0x28) == array and
                struct.unpack_from("<H", image, registration + 0x1C)[0] == count,
                f"Changed schema registration: {name}")
        actual = {string(ptr(array + i * 32)): u32(array + i * 32 + 16) for i in range(count)}
        for member, offset in expected.items():
            require(actual.get(member) == offset, f"Changed schema field: {name}::{member}")
        fields[name] = actual
    require_bytes(image, WINDOWS)
    values = {
        "WeaponServices": fields["C_BasePlayerPawn"]["m_pWeaponServices"],
        "ActiveWeapon": fields["CPlayer_WeaponServices"]["m_hActiveWeapon"],
        "Item": fields["C_EconEntity"]["m_AttributeManager"] + fields["C_AttributeContainer"]["m_Item"],
        "DefinitionIndex": fields["C_EconItemView"]["m_iItemDefinitionIndex"],
        "OwnerHandle": fields["C_BaseEntity"]["m_hOwnerEntity"],
        "C4Vtable": primary_vtable(image, base, b".?AVC_C4@@"),
        "PlantedC4Vtable": primary_vtable(image, base, b".?AVC_PlantedC4@@"),
        "C4BombPlanted": fields["C_C4"]["m_bBombPlanted"],
        "BombTicking": fields["C_PlantedC4"]["m_bBombTicking"],
        "BombExploded": fields["C_PlantedC4"]["m_bHasExploded"],
        "BombDefused": fields["C_PlantedC4"]["m_bBombDefused"],
    }
    # Schema names plus independently frozen byte-width native consumers.
    require(u32(0x80A34E) == values["C4BombPlanted"] and
            u32(0x82A34D) == values["BombTicking"] and
            u32(0x82A35A) == values["BombExploded"] and
            u32(0xCBE631) == values["BombDefused"], "Changed native C4 flag operand")
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", args.header.read_text(encoding="utf-8-sig"), flags=re.S)
    for name, value in values.items():
        matches = re.findall(rf"\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*;", source)
        require(len(matches) == 1 and int(matches[0], 0) == value, f"ESP item header mismatch: {name}; decoded {value:#x}")
    print(json.dumps({"status": "verified", "client": str(args.client), "sha256": SHA256,
                      "schema_classes": len(SCHEMA), "native_windows": len(WINDOWS),
                      "constants": {name: hex(value) for name, value in values.items()},
                      "limits": "Static contracts only; no live client or server observation."}, indent=2))


if __name__ == "__main__":
    main()
