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
EXPECTED_HASH = "659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0"
CHECKS = {
    "builder": (0xD02130, "488bc44c894018488948085553574155488da808ffffff4881ecd8010000"),
    "caller": (0xB66A36, "498b078bd34c8b442430498bcfff90c8000000"),
    "finalizer_before_builder": (0xB66968, "498b07498bcf488b542430ff5030"),
    "history_append": (0xCDE11A, "4c8b5138450fb6f00f297424300f28f3488bea488bf14d85d274174c634930453b0a7d0e4b8b7cca08418d4101894130eb18488b4928e86b2fadff488bd0488d4e28e8af4e5500488bf8"),
    "attack_indices": (0xD02824, "8b0f498d45204d8d454c83f9ff7406830820418908418b0e83f9ff740783084041894d50418b1849637530418b7d50"),
    "angle_producer": (0xCFA725, "83c901894a10488b4a184885c9751d488b4a084883e1fcf64208017403488b09e8767c97ff488bc848894318f30f104710834910018b4110f30f11411883c802f30f104f1489411083c804f30f11491cf30f104718894110f30f114120"),
    "shot_index": (0x80D84B, "4585f6740c4183fe017519458b7d50eb04458b7d4c4585ff780a453b7d300f8ccd000000"),
    "shot_history_entry": (0x80D952, "4c8b02498b45384963cf458b40384c8b7cc808"),
    "shot_angles": (0x80DA93, "498b47184885c0480f45d0e86d5dffff"),
    "fallback_ring": (0x80D89E, "4d8ba424f012000041bfff000000450f57d2418b842484120000"),
    "fallback_ring_entry": (0x80D8D0, "418b8c248012000003cb81e11f0000807d07ffc983c9e0ffc14863c1488d4c24504c6bf0784981c6800300004d03f4f3410f10566c418b5668e82204de00"),
    "fallback_interpolated_angles": (0x80DD2C, "498b46184885c0480f45c8f30f105920f30f10511cf30f104918488d4dc0e85100b6ff4a8b842398030000488d4c24704885c04c0f45f8f3410f105f20f3410f10571cf3410f104f18"),
    "fallback_retained_angles": (0x80DF25, "498b46184885c04c0f45f8f3410f105f20f3410f10571cf3410f104f18e859feb5ff"),
    "fallback_current_first": (0x80E033, "4d85ed0f84b502000041837d3000488d5f200f8ea6020000498b4538488d1592a29f01488d4f204c8b6808c7473c03000000498b45184885c0480f45d0e89b57ffff"),
    "history_copy_source": (0x911940, "498b4538488d8c24d80000004e8b743808f3410f10566c418b5668"),
    "history_copy_destination": (0x911A03, "486387080f0000498bd6486bc8784883c1084803cfe89333eaff"),
    "history_copy_other_destination": (0x911A5E, "488d4f08498bd64803cbe84333eaffff870c0f0000"),
    "history_assignment": (0x7B4DB0, "483bd1742a48895c2408574883ec20488bfa488bd9e8e6a5ffff488bd7488bcbe85ba1ffff"),
    "history_angle_copy": (0x7AEFA0, "488b4518498bd44885c0480f45d0e80d59e9ff"),
    "angle_converter": (0x803810, "f30f105a20f30f10521cf30f104a18e97ca5b6ff"),
    "append_capacity": (0x1233010, "48895c2408574883ec20488bd9488bfa488b49104885c974078b013b430c7513"),
    "input_constructor_object": (0xEE574, "488d0dc5d04802e860d8a600"),
    "input_constructor_vtable": (0xEE58C, "488d058df5ba0148890d4edc48024889059fd04802"),
    "native_command_buttons": (0xCED0A7, "488b4b60"),
    "serialized_command_buttons": (0xB66DF9, "488b57604885d2740d834810018b481048895018"),
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
    container = image[0xCDE15B]
    expected = {
        "Input": rip(0xEE574), "InputVtable": rip(0xEE58C),
        "HistorySlot": struct.unpack_from("<I", image, 0xB66A45)[0] // 8,
        "command::Presence": image[0xD02829],
        "command::HistorySize": image[0xCDE138],
        "command::HistoryCapacity": container + image[0x123302D],
        "command::HistoryRep": image[0xCDE11D],
        "command::Attack1Index": image[0xD0282D],
        "command::Attack1Present": image[0xD02835],
        "history::Presence": image[0xCFA72A],
        "history::Angles": image[0xCFA72E],
        "history::AnglesPresent": image[0xCFA727],
        "angle::Presence": image[0xCFA758],
        "angle::Pitch": image[0x80381E],
        "angle::Yaw": image[0x803819],
        "angle::Roll": image[0x803814],
        "angle::AllPresent": image[0xCFA758 + 1] | image[0xCFA764] | image[0xCFA76F],
        "repeated::Allocated": 0,  # cmp r9d, dword ptr [r10], CDE139
        "repeated::Entries": image[0x80D964],
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
    if (struct.unpack_from("<Q", image, expected["InputVtable"] - 8)[0] - base != 0x1F236C8 or
            struct.unpack_from("<I", image, 0x1F236C8 + 12)[0] != 0x23283D0 or
            image[0x23283E0:0x23283F1] != b".?AVCCSGOInput@@\0"):
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
    if timestamp != 0x6AD01314 or len(image) != 0x299D000:
        raise SystemExit("Client PE identity differs")
    require_bytes(image, CHECKS)
    # Native down state is the first uint64 at the shared movement command
    # Buttons offset. The engine's named enum registration supplies IN_ATTACK.
    movement_source = re.sub(r"//[^\n]*|/\*.*?\*/", "", args.movement_header.read_text(encoding="utf-8-sig"), flags=re.S)
    command_scope = re.search(r"namespace\s+command\s*\{([^}]*)\}", movement_source, re.S)
    native_buttons = re.search(r"ptrdiff_t\s+Buttons\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)", command_scope[1]) if command_scope else None
    if not native_buttons or int(native_buttons[1], 0) != image[0xCED0AA]:
        raise SystemExit("Shared movement native button offset differs")
    if (struct.unpack_from("<Q", image, 0x1B28D10)[0] - base != 0x1B28F78 or
            image[0x1B28F78:0x1B28F82] != b"IN_ATTACK\0" or
            struct.unpack_from("<Q", image, 0x1B28D18)[0] != 1):
        raise SystemExit("Named primary attack enum differs")
    descriptor = image[0x1BCC6B8:0x1BCC9A0]
    for field in (b"CSGOInputHistoryEntryPB", b"view_angles", b"input_history", b"attack1_start_history_index"):
        if field not in descriptor:
            raise SystemExit(f"Protobuf field descriptor differs: {field!r}")
    verified = verify_source(image, base, args.header)
    print(json.dumps({"path": str(args.client), "sha256": digest,
        "timestamp": hex(timestamp), "image_size": hex(len(image)),
        "verified_code": list(CHECKS), "source_offsets": str(args.header),
        "verified_source": verified, "movement_source": str(args.movement_header),
        "native_primary_attack": {"command_down": hex(image[0xCED0AA]), "mask": 1},
        "live_observation": False}, indent=2))


if __name__ == "__main__":
    main()
