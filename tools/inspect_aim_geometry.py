"""Read-only verification of current authored player capsule resource layouts.

Does not launch CS2, call animation functions, or modify installed assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from inspect_cs2 import VERIFIED_CLIENT_SHA256
from inspect_silent_aim_targets import CLIENT
from inspect_surface_tint import extract
from verify_sky_draw import mapped, require_bytes

ROOT = Path(__file__).resolve().parents[1]
ANIMATION = CLIENT.parents[3] / "bin/win64/animationsystem.dll"
ANIMATION_SHA256 = "8aa57d0c5b90cb49ad5fd9f141004710180a601b08159e50e93c4470142ea132"
ASSET = "agents/models/ctm_sas/ctm_sas.vmdl_c"
ASSET_SHA256 = "7312ab88d05bea67b06f3a945d06dc1e5fc5511d4849bc05bc05aec25380eb22"
CHECKS = {'model_mesh_and_hitbox_set_lookup': (0x1775290,
                                      '4585c07858443b41707d52488b41784d63c04a8b0cc04885c9744285d2783e3b91740100007d36f78164010000ffffff7f75134863c24533c0488d04c0488d4003498d04c0c34c8b81680100004863c2488d04c0488d4003498d04c0c333c0c3'),
 'enabled_mesh_mask_consumer': (0x1775A50,
                                '498b80900000004c850cc80f84de050000413b50707d0a498b40784c8b04c8eb034c8bc749638074010000'),
 'hitbox_set_box_count': (0x1775DC0,
                          '4c8b45bf488bcf41f78064010000ffffff7f498d8068010000488945b77403488b084a8d04ed000000004903c5488d14c5000000004863440a28488955ff488945ef'),
 'hitbox_box_array_and_crc': (0x1775E2E, '7403488b03488b4402304903c58b70404c8d784048'),
 'hitbox_stride': (0x1775FE8, '488b55ff4983c57048836def01'),
 'current_skeleton_hitbox_set': (0xAAEDE0, '488b81e00100000fb691fc0300004885c07408488b08e95517cd0033c9e94e17cd00'),
 'hitbox_transform_space': (0xAB1B6F, '498b4c2408498b140e807a4a000f84d3000000807a3d00488d4250'),
 'capsule_type_endpoint_transform': (0xCA29D8,
                                     '807f3c020f8537050000460f10542a10f30f105718450f28caf30f10471c450f28c2f3420f102c2af3420f107c2a08f3420f10742a04f3460f105c2a0c0f16d0f30f1047200fc6d088'),
 'capsule_radius_consumer': (0xCA2C4F, 'f30f107f30f3440f10530c')}


def schema(image, base, name, registration, array, count, size):
    def pointer(at):
        value = struct.unpack_from("<Q", image, at)[0] - base
        if not 0 < value < len(image):
            raise SystemExit(f"Invalid schema pointer: {name}")
        return value

    def string(at):
        end = image.find(0, at, min(at + 256, len(image)))
        if end < 0:
            raise SystemExit(f"Unterminated schema name: {name}")
        return bytes(image[at:end]).decode("ascii")

    if (string(pointer(registration)) != name or pointer(registration + 0x28) != array or
            struct.unpack_from("<H", image, registration + 0x1C)[0] != count or
            struct.unpack_from("<I", image, registration + 0x18)[0] != size):
        raise SystemExit(f"Changed schema registration: {name}")
    result = {}
    for index in range(count):
        at = array + index * 32
        field = string(pointer(at))
        if field in result:
            raise SystemExit(f"Duplicate field in {name}")
        result[field] = struct.unpack_from("<I", image, at + 16)[0]
    return result


def evidence(client, animation):
    _, image, base, _ = mapped(client)
    _, anim, anim_base, _ = mapped(animation)
    require_bytes(image, CHECKS)
    skeleton = schema(image, base, "CSkeletonInstance", 0x2201478, 0x2201380, 7, 0x490)
    state = schema(image, base, "CModelState", 0x2200058, 0x21FFE90, 14, 0x2B0)
    box = schema(anim, anim_base, "CHitBox", 0x81D6B8, 0x81D510, 13, 0x70)
    hitset = schema(anim, anim_base, "CHitBoxSet", 0x81D7B8, 0x81D730, 4, 0x30)
    u32 = lambda at: struct.unpack_from("<I", image, at)[0]
    if skeleton["m_nHitboxSet"] != u32(0xAAEDEA):
        raise SystemExit("Current hitbox-set schema/native consumer disagree")
    # Frozen LEA sequence multiplies set index by 9, then by 8; value is +3*8.
    stride, value = 9 * 8, 3 * 8
    constants = {
        "HitboxSet": skeleton["m_nHitboxSet"],
        "MeshGroup": skeleton["m_modelState"] + state["m_MeshGroupMask"],
        "MeshCount": image[0x1775298], "MeshArray": image[0x177529E],
        "MeshMasks": u32(0x1775A53), "SetCount": u32(0x17752B1),
        "SetStorage": u32(0x17752B9), "SetArray": u32(0x17752D9),
        "SetStride": stride, "SetValue": value, "SetName": hitset["m_name"],
        "BoxCount": hitset["m_HitBoxes"], "BoxArray": image[0x1775E37] - value,
        "BoxStride": image[0x1775FEF], "BoneName": box["m_sBoneName"],
        "Minimum": box["m_vMinBounds"], "Maximum": box["m_vMaxBounds"],
        "Radius": box["m_flShapeRadius"], "Shape": box["m_nShapeType"],
        "TranslationOnly": box["m_bTranslationOnly"], "AlternateTransform": image[0xAB1B7A],
        "CapsuleShape": image[0xCA29DB],
    }
    if constants["BoxCount"] != image[0x1775DF9] - value:
        raise SystemExit("Hitbox count schema/native consumer disagree")
    return constants


def verify_header(path, constants):
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8-sig"), flags=re.S)
    for name, value in constants.items():
        matches = re.findall(rf"\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;", text)
        if len(matches) != 1 or int(matches[0], 0) != value:
            raise SystemExit(f"Header mismatch: {name}; decoded {value:#x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=CLIENT)
    parser.add_argument("--animation", type=Path, default=ANIMATION)
    parser.add_argument("--header", type=Path, default=ROOT / "include/aim_geometry_offsets.h")
    parser.add_argument("--vpk", type=Path, default=CLIENT.parents[2] / "pak01_dir.vpk")
    args = parser.parse_args()
    client_hash = hashlib.sha256(args.client.read_bytes()).hexdigest()
    animation_hash = hashlib.sha256(args.animation.read_bytes()).hexdigest()
    if client_hash != VERIFIED_CLIENT_SHA256 or animation_hash != ANIMATION_SHA256:
        raise SystemExit("Installed client/animation changed: independently reverify authored hitbox resources")
    constants = evidence(args.client, args.animation)
    verify_header(args.header, constants)
    asset_hash = hashlib.sha256(extract(args.vpk, ASSET)).hexdigest()
    if asset_hash != ASSET_SHA256:
        raise SystemExit("Installed authored capsule example changed: repeat asset inspection")
    print(json.dumps({
        "component": "aim_geometry", "client": str(args.client), "client_sha256": client_hash,
        "animation": str(args.animation), "animation_sha256": animation_hash,
        "asset": ASSET, "asset_sha256": asset_hash,
        "constants": {key: hex(value) for key, value in constants.items()},
        "verified_code": list(CHECKS), "verified_header": str(args.header),
        "live_observation": False,
        "scope": "selected authored cstrike bone-space capsule, unit pose scale; cached pose without refresh",
    }, indent=2))


if __name__ == "__main__":
    main()
