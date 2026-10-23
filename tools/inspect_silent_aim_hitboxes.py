"""Verify dynamic named-bone aim points against the installed AMD64 CS2 client.

Read-only static evidence. Does not execute native callbacks or open the game.
"""

import argparse
import hashlib
import json
import pathlib
import re
import struct

from inspect_cs2 import VERIFIED_CLIENT_SHA256
from inspect_silent_aim_targets import CLIENT
from inspect_surface_tint import extract
from inspect_aim_geometry import ASSET, ASSET_SHA256
from verify_sky_draw import mapped, primary_vtable, require_bytes

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOOKUP_BYTES = "40534883ec20488b8930030000488bda488b01ff5068488bc8488bd34883c4205be9da8a1600"
CHECKS = {'pawn_named_bone_lookup': (0x953770, '40534883ec20488b8930030000488bda488b01ff5068488bc8488bd34883c4205be9da8a1600'),
 'skeleton_identity_query': (0x1C9E50, '488bc1c3'),
 'player_skeleton_constructor': (0x930AB5, '488d8f80000000488907e83ca31600488b5c2430488d0588f3190148898780000000'),
 'string_to_model_bone_lookup': (0xABC270,
                                 '40534883ec20488bc2488bd94885d27414803a00740fba26594131488bc8e8fda26affeb0233c0488b8be00100004885c97414488b094885c9740c8bd04883c4205be9996ccb00b8ffffffff4883c4205bc3'),
 'model_bone_count': (0xAB0760, '488b81e00100004885c0740c488b084885c90f854873cc0033c0c3'),
 'model_bone_count_member': (0x1777AC0, '8b8178010000c3'),
 'world_bone_transform': (0xAB1FF0,
                          '48895c24084889742410574883ec20488b81e0010000488bfa4963f0488bd94885c07405488b08eb0233c98bd6e80e64cc00ba00010000488bcb85c2b8ffff0f000f44d0e887fdfeff488b8bc00100004885c9751b90388b100100007408488bcbe88af7f1ff0f2843100f284b20eb10488bc648c1e0050f2804080f284c0810488b5c2430488bc7488b7424380f29070f294f104883c4205fc3'),
 'pawn_bone_transform_entry': (0x9402C0,
                               '48895c240848896c24104889742418574883ec20488be9418bf8488b8930030000488bda488b01ff5068488bf04885c0'),
 'native_named_pelvis_caller': (0xC7D817, '488d157a140101e84d5fcdff488bcf85c078e6448bc0488d542420e8892accff'),
 'native_bone_world_position_consumer': (0xC7D837, 'f20f10008b4008f20f1103894308')}
SCHEMA = {
    "CSkeletonInstance": (0x2201478, 0x2201380, 7, 0x490, {"m_modelState": 0x140}),
    "CModelState": (0x2200058, 0x21FFE90, 14, 0x2B0, {"m_hModel": 0xA0}),
    "CBodyComponentSkeletonInstance": (0x21C2A28, 0x21C29E8, 1, 0x510, {"m_skeletonInstance": 0x80}),
}
NAMES = {
    "HeadName": (0x1BA46A8, "head_0"),
    "ChestName": (0x1BA4698, "spine_3"),
    "StomachName": (0x1BA4680, "spine_0"),
    "PelvisName": (0x1BA4674, "pelvis"),
    "LowerChestName": (0x1BA4688, "spine_1"),
    "MiddleChestName": (0x1BA4690, "spine_2"),
    "LeftUpperArmName": (0x1C8EC40, "arm_upper_l"),
    "RightUpperArmName": (0x1C8EC50, "arm_upper_r"),
    "LeftForearmName": (0x1C8EC60, "arm_lower_l"),
    "RightForearmName": (0x1C8EC70, "arm_lower_r"),
    "LeftHandName": (0x1C8EC7C, "hand_l"),
    "RightHandName": (0x1C8EC84, "hand_r"),
    # Upper-leg names are established by the hash-matched authored SAS resource,
    # not a standalone client string. See CS2_AIM_MULTIHITGROUP_EVIDENCE.md.
    "LeftThighName": (None, "leg_upper_l"),
    "RightThighName": (None, "leg_upper_r"),
    "LeftCalfName": (0x1C8ECA0, "leg_lower_l"),
    "RightCalfName": (0x1C8ECB0, "leg_lower_r"),
    "LeftFootName": (0x1C8ECC0, "ankle_l"),
    "RightFootName": (0x1C8ECC8, "ankle_r"),
}


def evidence(image, base):
    def u32(at):
        return struct.unpack_from("<I", image, at)[0]

    def ptr(at):
        value = struct.unpack_from("<Q", image, at)[0] - base
        if not 0 < value < len(image):
            raise SystemExit(f"Invalid schema pointer at {at:#x}")
        return value

    def string(at):
        end = image.find(0, at, min(at + 256, len(image)))
        if end < 0:
            raise SystemExit(f"Invalid schema string at {at:#x}")
        return bytes(image[at:end]).decode("ascii")

    values = {}
    for name, (reg, array, count, size, expected) in SCHEMA.items():
        if (string(ptr(reg)) != name or ptr(reg + 0x28) != array or
                struct.unpack_from("<H", image, reg + 0x1C)[0] != count or u32(reg + 0x18) != size):
            raise SystemExit(f"Changed static schema registration: {name}")
        fields = {}
        for index in range(count):
            record = array + index * 32
            field = string(ptr(record))
            if field in fields:
                raise SystemExit(f"Duplicate schema field: {name}::{field}")
            fields[field] = u32(record + 0x10)
        for field, offset in expected.items():
            if fields.get(field) != offset:
                raise SystemExit(f"Changed named schema field: {name}::{field}")
        values.update(expected)
    require_bytes(image, CHECKS)
    table = primary_vtable(image, base, b".?AVCSkeletonInstance@@")
    slot = image[0x953785]
    query = ptr(table + slot)
    if table != 0x1C43C10 or query != 0x1C9E50:
        raise SystemExit("Changed skeleton RTTI/vtable identity-query contract")
    player_table = primary_vtable(image, base, b".?AVNetworkVar_m_skeletonInstance@CBodyComponentSkeletonInstance@@")
    if player_table != 0x1ACFE58 or ptr(player_table + slot) != query:
        raise SystemExit("Changed player networked skeleton vtable/query")
    locator = ptr(player_table - 8)
    hierarchy = u32(locator + 16)
    if u32(hierarchy + 8) != 4:
        raise SystemExit("Changed player skeleton RTTI base count")
    base_array = u32(hierarchy + 12)
    skeleton_base = u32(base_array + 4)
    if (string(u32(skeleton_base) + 16) != ".?AVCSkeletonInstance@@" or
            struct.unpack_from("<iii", image, skeleton_base + 8) != (0, -1, 0)):
        raise SystemExit("Player skeleton no longer inherits CSkeletonInstance at offset zero")
    constructor_table = 0x930AC9 + 7 + struct.unpack_from("<i", image, 0x930ACC)[0]
    constructor_base = 0x930ABF + 5 + struct.unpack_from("<i", image, 0x930AC0)[0]
    if (constructor_table != player_table or constructor_base != 0xA9AE00 or
            u32(0x930AB8) != values["m_skeletonInstance"] or
            u32(0x930AD3) != values["m_skeletonInstance"]):
        raise SystemExit("Player skeleton constructor disagrees with RTTI/schema ownership")
    # Resolve the count getter's conditional tail branch and the native named
    # caller independently, rather than treating matching leaf bodies as unique.
    count_target = 0xAB0778 + struct.unpack_from("<i", image, 0xAB0774)[0]
    named_bone = 0xC7D817 + 7 + struct.unpack_from("<i", image, 0xC7D81A)[0]
    lookup_target = 0xC7D81E + 5 + struct.unpack_from("<i", image, 0xC7D81F)[0]
    if count_target != 0x1777AC0 or string(named_bone) != "pelvis" or lookup_target != 0x953770:
        raise SystemExit("Independent native count/named-bone caller targets disagree")
    model_member = u32(0xABC29A)
    if model_member != values["m_modelState"] + values["m_hModel"]:
        raise SystemExit("Named schema model member and native handle consumer disagree")
    for label, (rva, name) in NAMES.items():
        if rva is not None and string(rva) != name:
            raise SystemExit(f"Changed native bone name: {label}")
    return {
        "LookupBone": 0x953770, "SkeletonVtable": table, "PlayerSkeletonVtable": player_table,
        "SkeletonQuerySlot": slot, "SkeletonQuery": query,
        "ModelState": values["m_modelState"], "ModelHandle": values["m_hModel"],
        "BoneArray": u32(0xAB203C), "BoneCount": u32(0x1777AC2),
        "BoneStride": 1 << image[0xAB2066], "Position": 0,
    }


def verify_header(path, constants):
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8-sig"), flags=re.S)
    for name, value in constants.items():
        matches = re.findall(rf"\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;", source)
        if len(matches) != 1 or int(matches[0], 0) != value:
            raise SystemExit(f"Header mismatch: {name}; decoded {value:#x}")
    for label, (_, name) in NAMES.items():
        matches = re.findall(rf'\b{label}\s*\[\]\s*=\s*"([^\"]*)"\s*;', source)
        if matches != [name]:
            raise SystemExit(f"Header bone-name mismatch: {label}")
    for name, expected in [("LookupBoneBytes", LOOKUP_BYTES), ("SkeletonQueryBytes", "488bc1c3")]:
        matches = re.findall(rf"\b{name}\s*\[\]\s*\{{([^}}]*)\}}", source, re.S)
        if len(matches) != 1:
            raise SystemExit(f"Missing or duplicate fingerprint: {name}")
        actual = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})\b", matches[0]))
        if actual != bytes.fromhex(expected):
            raise SystemExit(f"Header fingerprint mismatch: {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=pathlib.Path, default=CLIENT)
    parser.add_argument("--header", type=pathlib.Path, default=ROOT / "include/silent_aim_hitbox_offsets.h")
    parser.add_argument("--vpk", type=pathlib.Path, default=CLIENT.parents[2] / "pak01_dir.vpk")
    args = parser.parse_args()
    raw, image, base, pe = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != VERIFIED_CLIENT_SHA256:
        raise SystemExit("Installed client changed: independently reverify animated aim landmarks")
    asset_hash = hashlib.sha256(extract(args.vpk, ASSET)).hexdigest()
    if asset_hash != ASSET_SHA256:
        raise SystemExit("Installed authored landmark example changed: repeat asset inspection")
    constants = evidence(image, base)
    verify_header(args.header, constants)
    print(json.dumps({
        "component": "silent_aim_hitboxes", "client_path": str(args.client), "sha256": digest,
        "pe_timestamp": hex(struct.unpack_from("<I", raw, pe + 8)[0]),
        "size_of_image": hex(len(image)), "verified_header": str(args.header),
        "constants": {name: hex(value) for name, value in constants.items()},
        "authored_asset": ASSET, "authored_asset_sha256": asset_hash,
        "bone_names": {name: value for name, (_, value) in NAMES.items()},
        "verified_code": list(CHECKS), "live_observation": False,
        "scope": "dynamic named cached animated bone landmarks; no authored hitbox volume center claims",
    }, indent=2))


if __name__ == "__main__":
    main()
