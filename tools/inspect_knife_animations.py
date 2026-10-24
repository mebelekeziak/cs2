"""Read-only installed client knife subclass/VData/animation contract verification.

Does not launch CS2, access processes or modify installed files. Checks native
item-to-subclass lookup, the named schema callback, its VData consumer, and the
shipped weapon data resource. Matching files do not prove observed animations.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from material_vpk_probe import entries
from verify_sky_draw import mapped, primary_vtable

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
SHA256 = "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"
VDATA_SHA256 = "879900e6af45204508b3c20a9d781b0e960684111b848b7ae5c26b438fc6ff19"
CONSTANTS = {
    "ItemSubclass": 0x7CC8B0, "SubclassChanged": 0xB07310,
    "SubclassId": 0x380, "VData": 0x388, "VDataVtable": 0x1BDB390,
    "VDataScope": 8, "VDataSubclassId": 0xC, "VDataSubclassName": 0x10,
    "VDataWeaponName": 0x720, "VDataAnimClass": 0x868,
    "VDataAnimSkeleton": 0x528, "WeaponScope": 1, "KnifeVtable": 0x1C03720,
    "ScopeGetterSlot": 0x638, "VDataChangedSlot": 0x640,
    "ScopeGetter": 0x7CEB40, "WeaponVDataChanged": 0x820E40,
    "HudModelHandle": 0x1990,
}
BYTES = {"ItemSubclass": (0x7CC8B0, 0x2F), "SubclassChanged": (0xB07310, 0x35),
         "WeaponVDataChanged": (0x820E40, 0x20), "ScopeGetter": (0x7CEB40, 6)}
WINDOWS = {
    "item_subclass": (0x7CC8B0, 0x2F, "e9171747d32da4b30937a2ec7a5aebc292b41beb97d4802d35262c3aa7562027"),
    "item_static_definition": (0x1192390, 0xA4, "449d2bfa8ceaa4ce59b64d18d8980687117171dd74100ae1cf762a35f0c99623"),
    "subclass_name_lookup": (0x378D80, 0x105, "e8b567b770727ebe8f53baaa83a81ee27845d92413ee1d48208ccf35ef9cfb35"),
    "subclass_id_lookup": (0x378FB0, 0xE6, "60d11a4fade6ce2a62d92f4f6e2af44c47f07cd762d8f6536e723ce8888a44e1"),
    "subclass_set_native": (0x210880, 0xBF, "3348d342ee22d3d04f44a2404096b60f1a3ced8239f90018c4e15a33e4c58c3b"),
    "schema_callback_binding": (0x42D9BF, 0x20, "e3370db3a46213c3377e74071dcda9a079dd6966712a152d660e4e2f67fe36fa"),
    "subclass_changed": (0xB07310, 0x35, "2c74bea1f089ab210ea3dbc675c729ed5555c20aa69f9cfcbc6764eadff5e61d"),
    "vdata_resolve": (0x208EA0, 0x299, "c88a01b9b6c7b5c2ee584f4d483c3a88c19390e9b38c63e53b2a954f2a32dad6"),
    "weapon_vdata_changed": (0x820E40, 0x180, "d434128b861325b38890f68d0d2d8dcbf7d326fd14817660adfe2170bf4ea646"),
    "base_vdata_changed": (0x9581E0, 0x8E, "184d61d42a6ae7cc59bb85bae7167746b2eeff9994704f1284ed7e89e53f6de3"),
    "ui_native_producer": (0x8269D0, 0xFD, "2e294c642920de1e3549d3fa8de7ab79b365a0dc03366e23d220dfa67a769fbc"),
    "weapon_type_graph": (0x91310A, 0x6E, "86a7eee522aa0519fd49edc316c6e37387c6084435d39b14c581ad1d796f461c"),
}


def require(ok, message):
    if not ok:
        raise SystemExit(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=CLIENT)
    parser.add_argument("--header", type=Path, default=ROOT / "include/knife_animation_offsets.h")
    parser.add_argument("--game", type=Path)
    args = parser.parse_args()
    raw, image, base, nt = mapped(args.client)
    require(hashlib.sha256(raw).hexdigest() == SHA256, "Changed installed client: repeat knife animation investigation")
    u32 = lambda at: struct.unpack_from("<I", image, at)[0]
    ptr = lambda at: struct.unpack_from("<Q", image, at)[0] - base
    call = lambda at: at + 5 + struct.unpack_from("<i", image, at + 1)[0]
    rip = lambda at: at + 7 + struct.unpack_from("<i", image, at + 3)[0]

    def string(at):
        require(0 < at < len(image), "Invalid native string")
        end = image.find(0, at, min(len(image), at + 512))
        require(end >= at, "Unterminated native string")
        return image[at:end].decode("ascii")

    def schema(name, registration, array, count, wanted):
        require(string(ptr(registration)) == name and ptr(registration + 0x28) == array and struct.unpack_from("<H", image, registration + 0x1C)[0] == count, f"Changed knife schema: {name}")
        fields = {string(ptr(array + i * 32)): u32(array + i * 32 + 16) for i in range(count)}
        for key, value in wanted.items():
            require(fields.get(key) == value, f"Changed knife schema field: {name}::{key}")

    schema("C_BaseEntity", 0x21F6E78, 0x21F5550, 82, {"m_nSubclassID": CONSTANTS["SubclassId"]})
    schema("CCSWeaponBaseVData", 0x221CD08, 0x221C1A0, 83, {"m_szName": CONSTANTS["VDataWeaponName"], "m_szAnimClass": CONSTANTS["VDataAnimClass"], "m_szAnimSkeleton": CONSTANTS["VDataAnimSkeleton"]})
    schema("CCS2WeaponGraphController", 0x2224FC8, 0x2224D40, 20, {"m_weaponType": 0x120})
    for label, (at, size, digest) in WINDOWS.items():
        require(hashlib.sha256(image[at:at + size]).hexdigest() == digest, f"Changed native knife window: {label}")
    require(primary_vtable(image, base, b".?AVCCSWeaponBaseVData@@") == CONSTANTS["VDataVtable"], "Changed weapon VData RTTI")
    knife = primary_vtable(image, base, b".?AVC_Knife@@")
    require(knife == CONSTANTS["KnifeVtable"] and ptr(knife + CONSTANTS["ScopeGetterSlot"]) == CONSTANTS["ScopeGetter"] and ptr(knife + CONSTANTS["VDataChangedSlot"]) == CONSTANTS["WeaponVDataChanged"], "Changed knife scope/change vtable contract")
    require(string(rip(0x42D9BF)) == "OnSubclassIDChanged" and rip(0x42D9CD) == CONSTANTS["SubclassChanged"], "Changed named subclass schema callback")
    require(call(0x7CC8B9) == 0x1192390 and call(0x7CC8DA) == 0x378D80 and u32(0x7CC8D2) == CONSTANTS["WeaponScope"], "Changed item-to-subclass lookup")
    require(call(0x826A40) == CONSTANTS["ItemSubclass"] and call(0x826A5F) == 0x210880 and call(0x826A7E) == 0x9651F0, "Changed native subclass-before-model producer")
    require(call(0xB07319) == 0x208EA0 and call(0x2090AA) == 0x378FB0 and u32(0x2090B2) == CONSTANTS["VData"], "Changed native VData refresh")
    require(u32(0x208ED5) == CONSTANTS["SubclassId"] and u32(0x91310D) == CONSTANTS["VData"] and u32(0x913114) == CONSTANTS["VDataAnimClass"] and u32(0x913120) == CONSTANTS["VDataWeaponName"], "Changed animation graph VData input")
    require(u32(0x820E58) == CONSTANTS["HudModelHandle"] and string(rip(0x820F41)) == "Re-creating weapon hudmodel due to vdata subclass model change.\n", "Changed native hudmodel invalidation")
    header = re.sub(r"//[^\n]*|/\*.*?\*/", "", args.header.read_text(encoding="utf-8-sig"), flags=re.S)
    for key, value in CONSTANTS.items():
        matches = re.findall(rf"\b{key}\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*;", header)
        require(len(matches) == 1 and int(matches[0], 0) == value, f"Knife animation header mismatch: {key}")
    for key, (at, size) in BYTES.items():
        matches = re.findall(rf"\b{key}Bytes\[\]\s*=\s*\{{([^}}]*)\}}", header)
        require(len(matches) == 1, f"Missing knife animation fingerprint: {key}")
        data = bytes(int(x.strip(), 0) for x in matches[0].split(",") if x.strip())
        require(data == image[at:at + size], f"Knife animation fingerprint mismatch: {key}")
    game = args.game or args.client.parents[3]
    index = game / "csgo/pak01_dir.vpk"
    hits = [entry for entry in entries(index) if entry[0] == "scripts/weapons.vdata_c"]
    require(len(hits) == 1, "Missing or duplicate installed weapon VData resource")
    _, archive, offset, length, preload, start = hits[0]
    path = index if archive == 0x7FFF else index.with_name(index.name.replace("_dir.vpk", f"_{archive:03}.vpk"))
    with path.open("rb") as stream:
        stream.seek(offset + (start if archive == 0x7FFF else 0))
        data = preload + stream.read(length)
    require(len(data) == len(preload) + length and hashlib.sha256(data).hexdigest() == VDATA_SHA256, "Changed installed weapons.vdata_c: repeat knife subtype resource investigation")
    print(json.dumps({"status": "verified", "client": str(args.client), "sha256": SHA256, "native_windows": len(WINDOWS), "item_subclass": hex(CONSTANTS["ItemSubclass"]), "subclass_changed": hex(CONSTANTS["SubclassChanged"]), "vdata_sha256": VDATA_SHA256, "limit": "Static contracts; live knife-specific animation behavior still requires offline/private observation."}, indent=2))


if __name__ == "__main__":
    main()
