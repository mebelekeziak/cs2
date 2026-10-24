"""Verify installed CS2 skin item schemas and exact native attribute/material contracts.

Read-only installed-file audit. No process access, launches, injection or game-file
writes. Standard library only. Static agreement does not establish live appearance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from verify_sky_draw import mapped, primary_vtable

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
SHA256 = "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"
SCHEMA = {
    "C_EconEntity": (0x2254FE8, 0x2254D40, 20, {"m_AttributeManager": 0x1290, "m_nFallbackPaintKit": 0x18A8, "m_nFallbackSeed": 0x18AC, "m_flFallbackWear": 0x18B0, "m_nFallbackStatTrak": 0x18B4}),
    "C_AttributeContainer": (0x2254488, 0x2254410, 3, {"m_Item": 0x50}),
    "C_EconItemView": (0x2256918, 0x2256520, 31, {"m_iItemDefinitionIndex": 0x1BA, "m_iEntityQuality": 0x1BC, "m_iItemID": 0x1C8, "m_iItemIDHigh": 0x1D0, "m_iItemIDLow": 0x1D4, "m_iAccountID": 0x1D8, "m_bInitialized": 0x1E8, "m_AttributeList": 0x208, "m_szCustomName": 0x2F8}),
    "CAttributeList": (0x22563A8, 0x2256360, 2, {"m_Attributes": 8}),
    "CEconItemAttribute": (0x22564B8, 0x2256410, 5, {"m_iAttributeDefinitionIndex": 0x30, "m_flValue": 0x34, "m_flInitialValue": 0x38, "m_bSetBonus": 0x40}),
    "C_CSPlayerPawn": (0x223BAC8, 0x223AD40, 105, {"m_bNeedToReApplyGloves": 0x176D, "m_EconGloves": 0x1770, "m_nEconGlovesChanged": 0x1D20}),
    "C_BasePlayerPawn": (0x21F67E8, 0x21F6450, 28, {"m_pWeaponServices": 0x12F0}),
    "CPlayer_WeaponServices": (0x222DDC8, 0x222DD30, 4, {"m_hMyWeapons": 0x48}),
    "C_BaseEntity": (0x21F6E78, 0x21F5550, 82, {"m_hOwnerEntity": 0x520}),
}
WINDOWS = {
    "set_attribute": (0x11A3080, 0x22, "8c64b5699f1c9cfc027a38d7e41950f4f09c7726e8aefe7ed969cd80b3c99d8c"),
    "attribute_list_set": (0x11A2CB0, 0x3B5, "1feec4188c009c1d152bb0cd2830ea880732a7d96e033b1eb836a3c3082e542b"),
    "remove_attribute": (0x11A12C0, 0x7B, "2d2b635150bc02bd397e40d2d45747b6832d32154b50ec1c4ca7a9fec92e4789"),
    "cache_invalidation": (0x119EBB0, 0xCB, "6eeaa232e04e5312217d21a69e5a1bbb7b3870306d249d87bda9962840e3f10e"),
    "fallback_initialize": (0x1141720, 0x192, "4b1782aba697598a3a8b7e905d03fb8702551fe15d2546b87e7308d806e7e3a9"),
    "regenerate_registration": (0x9C0E0, 0x6A, "ee5077245d548f53f40c8cf3738093a523656f428cb572a034f3bf350b867c49"),
    "regenerate_command": (0x82AA40, 0x151, "6710b94ba494c68ce687ec489996684701997722a8bdb2cb0c85ee11f1e02927"),
    "composite_reset": (0x14E9D00, 0x137, "b0937eab589ece2852297ae4fe9880d3a698b19da199bc97943e19cddd2b6817"),
    "weapon_regenerate": (0x803780, 0x249, "ea33ea5383b896baa14150f5f832227e10eef768019d22a8a97397f92bed7064"),
    "composite_generate": (0x8004B0, 0x899, "78192fd312b54583ffdabf4709126e8f266fc22981cad820690da9ddbca4bd56"),
    "glove_consumer": (0xC662EB, 0x3B5, "4fafdd07b66694fcea2e8d58a6114c0cef7228ee3a639ca1c94d0f66f9009daa"),
    "glove_model_lookup": (0x11924E0, 0xB3, "f9a8aa3454104e9346adfda06800169fc9802d2dd280d90cfcdde133074da0a0"),
    "weapon_handles": (0x91514A, 0x12A, "13a4240c739c3b13f66aa3755f05a2db1fc578f57e253f0a00017886581ac438"),
    "legacy_paint": (0x8265A0, 0x41, "9052a2131a0e1091d5e6530326533c3698b42968d0f0663b8b11f75942fb58d0"),
}
CONSTANTS = {"Item": 0x12E0, "DefinitionIndex": 0x1BA, "Quality": 0x1BC, "ItemId": 0x1C8, "ItemIdHigh": 0x1D0, "ItemIdLow": 0x1D4, "AccountId": 0x1D8, "Initialized": 0x1E8, "CustomName": 0x2F8, "CustomNameSize": 161, "FallbackPaint": 0x18A8, "FallbackSeed": 0x18AC, "FallbackWear": 0x18B0, "FallbackStatTrak": 0x18B4, "AttributesCount": 0x210, "AttributesData": 0x218, "AttributeStride": 0x48, "AttributeIndex": 0x30, "AttributeValue": 0x34, "AttributeInitialValue": 0x38, "AttributeSetBonus": 0x40, "WeaponServices": 0x12F0, "WeaponHandlesCount": 0x48, "WeaponHandlesData": 0x50, "OwnerHandle": 0x520, "GlovesItem": 0x1770, "GloveReapply": 0x176D, "GloveChanged": 0x1D20, "CompositeOwner": 0x610, "CompositeMaterialsCount": 0xAB0, "CompositePendingCount": 0xAC8, "AttributeSet": 0x11A3080, "AttributeRemove": 0x11A12C0, "CompositeReset": 0x14E9D00, "WeaponRegenerate": 0x803780, "RegenerateCommand": 0x82AA40, "GloveMarkChanged": 0xC8FE40, "ItemModel": 0x1191AA0}
BYTES = {"AttributeSet": (0x11A3080, 0x22), "AttributeRemove": (0x11A12C0, 0x17), "CompositeReset": (0x14E9D00, 0x1F), "WeaponRegenerate": (0x803780, 0x28), "GloveMarkChanged": (0xC8FE40, 8), "ItemModel": (0x1191AA0, 0x17)}


def require(ok, reason):
    if not ok:
        raise SystemExit(reason)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=CLIENT)
    parser.add_argument("--header", type=Path, default=ROOT / "include/skin_offsets.h")
    args = parser.parse_args()
    raw, image, base, nt = mapped(args.client)
    require(hashlib.sha256(raw).hexdigest() == SHA256, "Changed installed client: repeat skin contract investigation")
    ptr = lambda at: struct.unpack_from("<Q", image, at)[0] - base
    u32 = lambda at: struct.unpack_from("<I", image, at)[0]

    def string(at):
        require(0 < at < len(image), "Invalid native string")
        end = image.find(0, at, min(len(image), at + 512))
        require(end >= at, "Unterminated native string")
        return image[at:end].decode("ascii")

    reports = {}
    for name, (registration, array, count, expected) in SCHEMA.items():
        require(string(ptr(registration)) == name and ptr(registration + 0x28) == array and struct.unpack_from("<H", image, registration + 0x1C)[0] == count, f"Changed schema registration: {name}")
        fields = {string(ptr(array + i * 32)): u32(array + i * 32 + 16) for i in range(count)}
        for member, offset in expected.items():
            require(fields.get(member) == offset, f"Changed schema field: {name}::{member}")
        reports[name] = {key: hex(value) for key, value in expected.items()}
    for label, (at, size, digest) in WINDOWS.items():
        require(hashlib.sha256(image[at:at + size]).hexdigest() == digest, f"Changed native skin window: {label}")
    call = lambda at: at + 5 + struct.unpack_from("<i", image, at + 1)[0]
    rip = lambda at: at + 7 + struct.unpack_from("<i", image, at + 3)[0]
    require(string(rip(0x9C118)) == "regenerate_weapon_skins" and rip(0x9C0E9) == CONSTANTS["RegenerateCommand"], "Changed named regenerate command")
    require(call(0x82AABB) == CONSTANTS["CompositeReset"] and call(0x82AAC5) == CONSTANTS["WeaponRegenerate"], "Changed native regeneration sequence")
    require(call(0x11A3090) == 0x11A2CB0, "Changed attribute setter delegation")
    for at, text in ((0x11417E1, "set item texture prefab"), (0x1141802, "set item texture seed"), (0x1141829, "set item texture wear"), (0x1141849, "kill eater"), (0x114185B, "kill eater score type")):
        require(string(rip(at)) == text, f"Changed native attribute name: {text}")
    # Integer attributes are bit-preserved through the float-typed ABI.
    require(image[0x1141845:0x1141849] == bytes.fromhex("660f6ed0"), "Changed StatTrak raw integer MOVD")
    require(u32(0x22564B8 + 0x18) == CONSTANTS["AttributeStride"], "Changed native attribute size")
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", args.header.read_text(encoding="utf-8-sig"), flags=re.S)
    for key, value in CONSTANTS.items():
        matches = re.findall(rf"\b{key}\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*;", source)
        require(len(matches) == 1 and int(matches[0], 0) == value, f"Skin header mismatch: {key}; native {value:#x}")
    for key, (at, size) in BYTES.items():
        matches = re.findall(rf"\b{key}Bytes\[\]\s*=\s*\{{([^}}]*)\}}", source)
        require(len(matches) == 1, f"Missing skin fingerprint: {key}")
        data = bytes(int(x.strip(), 0) for x in matches[0].split(",") if x.strip())
        require(data == image[at:at + size], f"Skin fingerprint mismatch: {key}")
    ids = re.findall(r"\bAttributeIds\[\]\s*=\s*\{([^}]*)\}", source)
    require(len(ids) == 1 and [int(x.strip(), 0) for x in ids[0].split(",")] == [6, 7, 8, 80, 81], "Skin attribute ID mismatch; installed items_game names are required")
    require(primary_vtable(image, base, b".?AVC_Knife@@") == 0x1C03720, "Changed knife RTTI")
    print(json.dumps({"status": "verified", "client": str(args.client), "sha256": SHA256, "native_windows": len(WINDOWS), "schema": reports, "limits": "Static native contracts only; actual first-person/third-person skins and legacy mesh appearance need offline/private observation."}, indent=2))


if __name__ == "__main__":
    main()
