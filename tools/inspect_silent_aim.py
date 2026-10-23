"""Verify installed input-history producer/shot consumer and silent aim constants.

Standard library only; reads installed files without launching/opening a process.
Changed binaries require renewed investigation, never guessed offset replacement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, require_bytes

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_HASH = "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"
CHECKS = {
    "builder": (0xD03790, "488bc44c894018488948085553574155488da808ffffff4881ecd8010000"),
    "caller": (0xB67876, "498b078bd34c8b442430498bcfff90c8000000"),
    "finalizer_before_builder": (0xB677A8, "498b07498bcf488b542430ff5030"),
    "history_append": (0xCDF77A, "4c8b5138450fb6f00f297424300f28f3488bea488bf14d85d274174c634930453b0a7d0e4b8b7cca08418d4101894130eb18488b4928e8fb29adff488bd0488d4e28e8af5a5500488bf8"),
    "attack_indices": (0xD03E84, "8b0f498d45204d8d454c83f9ff7406830820418908418b0e83f9ff740783084041894d50418b1849637530418b7d50"),
    "angle_producer": (0xCFBD85, "83c901894a10488b4a184885c9751d488b4a084883e1fcf64208017403488b09e8167497ff488bc848894318f30f104710834910018b4110f30f11411883c802f30f104f1489411083c804f30f11491cf30f104718894110f30f114120"),
    "shot_index": (0x80E67B, "4585f6740c4183fe017519458b7d50eb04458b7d4c4585ff780a453b7d300f8ccd000000"),
    "shot_history_entry": (0x80E782, "4c8b02498b45384963cf458b40384c8b7cc808"),
    "shot_angles": (0x80E8C3, "498b47184885c0480f45d0e86d5dffff"),
    "fallback_ring": (0x80E6CE, "4d8ba424f012000041bfff000000450f57d2418b842484120000"),
    "fallback_ring_entry": (0x80E700, "418b8c248012000003cb81e11f0000807d07ffc983c9e0ffc14863c1488d4c24504c6bf0784981c6800300004d03f4f3410f10566c418b5668e85218de00"),
    "fallback_interpolated_angles": (0x80EB5C, "498b46184885c0480f45c8f30f105920f30f10511cf30f104918488d4dc0e8f1fcb5ff4a8b842398030000488d4c24704885c04c0f45f8f3410f105f20f3410f10571cf3410f104f18"),
    "fallback_retained_angles": (0x80ED55, "498b46184885c04c0f45f8f3410f105f20f3410f10571cf3410f104f18e8f9fab5ff"),
    "fallback_current_first": (0x80EE63, "4d85ed0f84b502000041837d3000488d5f200f8ea6020000498b4538488d15b2b49f01488d4f204c8b6808c7473c03000000498b45184885c0480f45d0e89b57ffff"),
    "history_copy_source": (0x912770, "498b4538488d8c24d80000004e8b743808f3410f10566c418b5668"),
    "history_copy_destination": (0x912833, "486387080f0000498bd6486bc8784883c1084803cfe85336eaff"),
    "history_copy_other_destination": (0x91288E, "488d4f08498bd64803cbe80336eaffff870c0f0000"),
    "history_assignment": (0x7B5EA0, "483bd1742a48895c2408574883ec20488bfa488bd9e8e6a5ffff488bd7488bcbe85ba1ffff"),
    "history_angle_copy": (0x7B0090, "488b4518498bd44885c0480f45d0e88d3ce9ff"),
    "angle_converter": (0x804640, "f30f105a20f30f10521cf30f104a18e91ca2b6ff"),
    "append_capacity": (0x1235270, "48895c2408574883ec20488bd9488bfa488b49104885c974078b013b430c7513"),
    "input_constructor_object": (0xEE574, "488d0d05f24802e8a0e6a600"),
    "input_constructor_vtable": (0xEE58C, "488d05c514bb0148890d8efd4802488905dff14802"),
    "native_command_buttons": (0xCEE707, "488b4b60"),
    "serialized_command_buttons": (0xB67C39, "488b57604885d2740d834810018b481048895018"),
}


def source_values(header):
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", header.read_text(encoding="utf-8-sig"), flags=re.S)
    values = {}
    for scope, text in [("", source.split("namespace command")[0]), *re.findall(
            r"namespace\s+(command|history|angle|repeated)\s*\{([^}]*)\}", source, re.S)]:
        for name, literal in re.findall(r"\b(?:ptrdiff_t|uint32_t)\s+(\w+)\s*=\s*(0[xX][0-9A-Fa-f]+|[0-9]+)\s*;", text):
            key = f"{scope}::{name}" if scope else name
            if key in values:
                raise SystemExit(f"Duplicate source constant: {key}")
            values[key] = int(literal, 0)
    prefix = re.search(r"HistoryBuilderBytes\[\]\s*\{([^}]*)\}", source, re.S)
    if not prefix:
        raise SystemExit("Missing HistoryBuilderBytes")
    return values, bytes(int(x.strip(), 0) for x in prefix.group(1).split(",") if x.strip())


def verify_source(image, base, header):
    # Operand positions are independently selected from the frozen instructions.
    def rip(site):
        return site + 7 + struct.unpack_from("<i", image, site + 3)[0]
    container = image[0xCDF7BB]
    expected = {
        "Input": rip(0xEE574), "InputVtable": rip(0xEE58C),
        "HistorySlot": struct.unpack_from("<I", image, 0xB67885)[0] // 8,
        "command::Presence": image[0xD03E89],
        "command::HistorySize": image[0xCDF798],
        "command::HistoryCapacity": container + image[0x123528D],
        "command::HistoryRep": image[0xCDF77D],
        "command::Attack1Index": image[0xD03E8D],
        "command::Attack1Present": image[0xD03E95],
        "history::Presence": image[0xCFBD8A],
        "history::Angles": image[0xCFBD8E],
        "history::AnglesPresent": image[0xCFBD87],
        "angle::Presence": image[0xCFBDB8],
        "angle::Pitch": image[0x80464E],
        "angle::Yaw": image[0x804649],
        "angle::Roll": image[0x804644],
        "angle::AllPresent": image[0xCFBDB8 + 1] | image[0xCFBDC4] | image[0xCFBDCF],
        "repeated::Allocated": 0,  # cmp r9d, dword ptr [r10], CDF799
        "repeated::Entries": image[0x80E794],
    }
    expected["HistoryBuilder"] = struct.unpack_from("<Q", image,
        expected["InputVtable"] + expected["HistorySlot"] * 8)[0] - base
    values, prefix = source_values(header)
    if set(values) != set(expected):
        raise SystemExit(f"Source inventory differs: {set(values) ^ set(expected)}")
    for key, value in expected.items():
        if values[key] != value:
            raise SystemExit(f"Source offset mismatch: {key}={values[key]:#x}; native operand requires {value:#x}")
    if prefix != bytes.fromhex(CHECKS["builder"][1]):
        raise SystemExit("HistoryBuilderBytes differs from independent frozen entry")
    # Exact input class RTTI establishes ownership of the selected vtable.
    if (struct.unpack_from("<Q", image, expected["InputVtable"] - 8)[0] - base != 0x1F256C8 or
            struct.unpack_from("<I", image, 0x1F256C8 + 12)[0] != 0x232A400 or
            image[0x232A410:0x232A421] != b".?AVCCSGOInput@@\0"):
        raise SystemExit("Input RTTI ownership differs")
    return {key: hex(value) for key, value in expected.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=Path(
        r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"))
    parser.add_argument("--header", type=Path, default=ROOT / "include/silent_aim_offsets.h")
    parser.add_argument("--movement-header", type=Path, default=ROOT / "include/movement_offsets.h")
    args = parser.parse_args()
    raw, image, base, nt = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH:
        raise SystemExit("Client changed: repeat input-history investigation.")
    timestamp = struct.unpack_from("<I", raw, nt + 8)[0]
    if timestamp != 0x6ADA7340 or len(image) != 0x29A0000:
        raise SystemExit("Client PE identity differs")
    require_bytes(image, CHECKS)
    # Native down state is the first uint64 at the shared movement command
    # Buttons offset. The engine's named enum registration supplies IN_ATTACK.
    movement_source = re.sub(r"//[^\n]*|/\*.*?\*/", "", args.movement_header.read_text(encoding="utf-8-sig"), flags=re.S)
    command_scope = re.search(r"namespace\s+command\s*\{([^}]*)\}", movement_source, re.S)
    native_buttons = re.search(r"ptrdiff_t\s+Buttons\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)", command_scope[1]) if command_scope else None
    if not native_buttons or int(native_buttons[1], 0) != image[0xCEE70A]:
        raise SystemExit("Shared movement native button offset differs")
    if (struct.unpack_from("<Q", image, 0x1B2ADB0)[0] - base != 0x1B2B018 or
            image[0x1B2B018:0x1B2B022] != b"IN_ATTACK\0" or
            struct.unpack_from("<Q", image, 0x1B2ADB8)[0] != 1):
        raise SystemExit("Named primary attack enum differs")
    descriptor = image[0x1BCE648:0x1BCE930]
    for field in (b"CSGOInputHistoryEntryPB", b"view_angles", b"input_history", b"attack1_start_history_index"):
        if field not in descriptor:
            raise SystemExit(f"Protobuf field descriptor differs: {field!r}")
    verified = verify_source(image, base, args.header)
    print(json.dumps({"path": str(args.client), "sha256": digest,
        "timestamp": hex(timestamp), "image_size": hex(len(image)),
        "verified_code": list(CHECKS), "source_offsets": str(args.header),
        "verified_source": verified, "movement_source": str(args.movement_header),
        "native_primary_attack": {"command_down": hex(image[0xCEE70A]), "mask": 1},
        "live_observation": False}, indent=2))


if __name__ == "__main__":
    main()
