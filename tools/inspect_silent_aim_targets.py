"""Verify the basic aim reader's additional eye-offset field against installed CS2.

Reads installed files only; never launches, injects, or modifies the game.
Existing entity/scene/collision fields remain owned by inspect_cs2.py.
"""

import argparse
import hashlib
import json
import pathlib
import re
import struct

from inspect_cs2 import VERIFIED_CLIENT_SHA256
from verify_sky_draw import mapped, require_bytes

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLIENT = pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
REGISTRATION = 0x21F3F48
FIELD_ARRAY = 0x21F4600
FIELD_COUNT = 47

CHECKS = {
    # RCX=pawn; native origin getter; R8=pawn+m_vecViewOffset.
    "eye_origin_and_view_offset": (0x8CD2CF, "e8acc794ff80bf20100000004c8d87600f0000488bd8"),
    # R9B=true applies scene scale; RCX=pawn, RDX=output, R8=view vector.
    "eye_transform_call": (0x8CD35C, "41b101488d542424488bcfe864d089ff"),
    # The native transform reads contiguous floats at vector+0,+4,+8.
    "live_vector_components": (0x16A423, "0f28c20f28caf30f5907f30f594f04f30f595708f30f1103f30f114b04f30f115308"),
    # Origin getter uses pawn scene+0x330, then scene absolute origin+0xC8.
    "origin_getter": (0x219A86, "488b99300300009080bb10010000007408488bcbe8116f7b00488d83c8000000"),
    # Origin components are added after transforming the offset.
    "eye_origin_sum": (0x8CD36C, "f30f10442424488d542424f30f5803f30f104c2428488bcff30f584b04f30f1054242cf30f585308"),
}


def evidence(image, base):
    def pointer(at):
        value = struct.unpack_from("<Q", image, at)[0] - base
        if not 0 < value < len(image):
            raise SystemExit(f"Invalid schema pointer at {at:#x}")
        return value

    def string(at):
        end = image.find(0, at, min(at + 256, len(image)))
        if end < 0:
            raise SystemExit(f"Unterminated schema string at {at:#x}")
        return bytes(image[at:end]).decode("ascii")

    if (string(pointer(REGISTRATION)) != "C_BaseModelEntity" or
            struct.unpack_from("<I", image, REGISTRATION + 0x18)[0] != 0x1098 or
            struct.unpack_from("<H", image, REGISTRATION + 0x1C)[0] != FIELD_COUNT or
            pointer(REGISTRATION + 0x28) != FIELD_ARRAY):
        raise SystemExit("Changed C_BaseModelEntity static schema registration")
    fields = {}
    for index in range(FIELD_COUNT):
        record = FIELD_ARRAY + index * 0x20
        name = string(pointer(record))
        if name in fields:
            raise SystemExit(f"Duplicate C_BaseModelEntity field: {name}")
        fields[name] = struct.unpack_from("<I", image, record + 0x10)[0]
    if "m_vecViewOffset" not in fields:
        raise SystemExit("Missing C_BaseModelEntity::m_vecViewOffset")
    require_bytes(image, CHECKS)
    # Decode the LEA operand rather than relying on the field name alone.
    view_offset = struct.unpack_from("<I", image, 0x8CD2DE)[0]
    if fields["m_vecViewOffset"] != view_offset:
        raise SystemExit("Schema and native live view-offset operand disagree")
    call = 0x8CD367
    transform = call + 5 + struct.unpack_from("<i", image, call + 1)[0]
    if transform != 0x16A3D0:
        raise SystemExit("Native eye transform target changed")
    return view_offset


def verify_header(path, view_offset):
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8-sig"), flags=re.S)
    scopes = re.findall(r"namespace\s+silent_aim_target_offsets\s*\{([^}]*)\}", source, re.S)
    if len(scopes) != 1:
        raise SystemExit("Expected one silent_aim_target_offsets namespace")
    values = re.findall(r"\bViewOffset\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;", scopes[0])
    if len(values) != 1 or int(values[0], 0) != view_offset:
        raise SystemExit(f"Header mismatch: silent_aim_target_offsets::ViewOffset; decoded {view_offset:#x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=pathlib.Path, default=CLIENT)
    parser.add_argument("--header", type=pathlib.Path, default=ROOT / "include/silent_aim_target_offsets.h")
    args = parser.parse_args()
    raw, image, base, pe = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != VERIFIED_CLIENT_SHA256:
        raise SystemExit("Installed client changed: independently reverify aim target evidence")
    machine = struct.unpack_from("<H", raw, pe + 4)[0]
    if machine != 0x8664:
        raise SystemExit("Aim target evidence requires the installed AMD64 client")
    view_offset = evidence(image, base)
    verify_header(args.header, view_offset)
    print(json.dumps({
        "component": "silent_aim_targets", "client_path": str(args.client), "sha256": digest,
        "machine": hex(machine), "pe_timestamp": hex(struct.unpack_from("<I", raw, pe + 8)[0]),
        "size_of_image": hex(len(image)), "verified_header": str(args.header),
        "schema_class": "C_BaseModelEntity", "schema_registration": hex(REGISTRATION),
        "schema_field_array": hex(FIELD_ARRAY), "schema_field_count": FIELD_COUNT,
        "fields": {"m_vecViewOffset": hex(view_offset)}, "verified_code": list(CHECKS),
        "live_observation": False,
        "scope": "live copied view offset, scene transform and origin; no visibility or bone claims",
    }, indent=2))


if __name__ == "__main__":
    main()
