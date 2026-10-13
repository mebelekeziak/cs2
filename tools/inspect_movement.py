"""Verify movement evidence against the installed PE, without opening a process.

No game files are changed. A different build requires a new investigation, not
automatic substitution of internet offsets. Standard-library Python only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

EXPECTED_HASH = "44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008"
CHECKS = {
    "command_finalizer": (0xCEC510, "48895c2408488974241048897c24184c8974242055488d6c24e04881ec200100"),
    "command_finalizer_call": (0xB659E8, "498b07498bcf488b542430ff5030"),
    "effective_maxspeed": (0x8CD030, "48895c24084889742420574883ec30488bf9"),
    "float_cvar_getter": (0x1607A0, "488b4108f30f104058c3"),
    "surface_speed_row": (0x9E26A0, "48634110488b0d5527b60148c1e00548034128c3"),
    "command_ingestion": (0xA764D0, "48895424105355565741544155415641574883ec68"),
    "subtick_allocator": (0x4F7120, "48895c2410574883ec20"),
    "subtick_append": (0x122D140, "48895c2408574883ec20"),
    "controller_simulation_tick": (0x9433E0, "8b81b8060000c3"),
}

# These are instruction sites, not copies of the source offsets: resolving
# their operands independently catches wrapper-start/value-pointer confusion.
CVAR_REGISTRATIONS = {
    "LegacyJump": (0xB2377, 0xB2370, "sv_legacy_jump"),
    "BhopWindow": (0xB1B12, 0xB1B06, "sv_bhop_time_window"),
    "AutoBunnyhop": (0xB1A77, 0xB1A70, "sv_autobunnyhopping"),
    "JumpPenalty": (0xB202B, 0xB2022, "sv_jump_spam_penalty_time"),
}


def rip_target(image, rva, opcode):
    if bytes(image[rva:rva + 3]) != bytes.fromhex(opcode):
        raise SystemExit(f"Cvar operand evidence differs at 0x{rva:X}")
    return rva + 7 + struct.unpack_from("<i", image, rva + 3)[0]


def verify_cvar_offsets(image, header_path):
    header = header_path.read_text(encoding="utf-8-sig")
    header = re.sub(r"//[^\n]*|/\*.*?\*/", "", header, flags=re.S)
    values = {}
    for name in (*CVAR_REGISTRATIONS, "CvarValuePointer", "CvarValue"):
        matches = re.findall(
            rf"\bptrdiff_t\s+{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;", header)
        if len(matches) != 1:
            raise SystemExit(f"Expected one literal source offset {name} in {header_path}")
        values[name] = int(matches[0], 16 if matches[0].lower().startswith("0x") else 10)

    getter = bytes(image[0x1607A0:0x1607AA])
    if getter[:3] != bytes.fromhex("488b41") or getter[4:8] != bytes.fromhex("f30f1040") or getter[9] != 0xC3:
        raise SystemExit("Cvar getter addressing evidence differs")
    for name, observed in (("CvarValuePointer", getter[3]), ("CvarValue", getter[8])):
        if values[name] != observed:
            raise SystemExit(f"Source offset mismatch: {name}=0x{values[name]:X}; getter requires 0x{observed:X}")

    verified = {}
    for name, (wrapper_site, name_site, cvar_name) in CVAR_REGISTRATIONS.items():
        wrapper = rip_target(image, wrapper_site, "488d0d")  # LEA RCX
        name_rva = rip_target(image, name_site, "488d15")  # LEA RDX
        expected_name = cvar_name.encode("ascii") + b"\0"
        if bytes(image[name_rva:name_rva + len(expected_name)]) != expected_name:
            raise SystemExit(f"Registration name differs: {name} at 0x{name_rva:X}")
        if values[name] != wrapper:
            raise SystemExit(
                f"Source offset mismatch: {name}=0x{values[name]:X}; "
                f"{cvar_name} registration requires wrapper start 0x{wrapper:X}")
        verified[name] = {"name": cvar_name, "wrapper_start": hex(wrapper)}

    # Both selectors load the pointer slot directly, unlike Cvar(), which
    # receives the wrapper start and then adds the getter's pointer offset.
    for load_site, compare_site in ((0x8DE86D, 0x8DE877), (0x8BF476, 0x8BF480)):
        slot = rip_target(image, load_site, "488b05")  # MOV RAX
        if slot != values["LegacyJump"] + values["CvarValuePointer"]:
            raise SystemExit(f"Legacy selector pointer slot differs at 0x{load_site:X}")
        if bytes(image[compare_site:compare_site + 4]) != bytes((0x80, 0x78, values["CvarValue"], 0)):
            raise SystemExit(f"Legacy selector value addressing differs at 0x{compare_site:X}")
    return verified

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=Path(
        r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"))
    parser.add_argument("--header", type=Path,
        default=Path(__file__).resolve().parents[1] / "include" / "movement_offsets.h",
        help="Source offsets to compare with independent installed registration evidence")
    args = parser.parse_args()
    raw = args.client.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH:
        raise SystemExit("Client changed: re-investigate movement before using these layouts.")
    nt = struct.unpack_from("<I", raw, 0x3C)[0]
    machine, sections = struct.unpack_from("<HH", raw, nt + 4)
    timestamp = struct.unpack_from("<I", raw, nt + 8)[0]
    optional_size = struct.unpack_from("<H", raw, nt + 20)[0]
    optional = nt + 24
    image_base = struct.unpack_from("<Q", raw, optional + 24)[0]
    image_size = struct.unpack_from("<I", raw, optional + 56)[0]
    image = bytearray(image_size)
    for index in range(sections):
        section = optional + optional_size + index * 40
        rva, size, start = struct.unpack_from("<III", raw, section + 12)
        image[rva:rva + size] = raw[start:start + size]
    assert (machine, timestamp, image_size) == (0x8664, 0x6AC7FFFA, 0x2994000)
    for name, (rva, expected) in CHECKS.items():
        want = bytes.fromhex(expected)
        if bytes(image[rva:rva + len(want)]) != want:
            raise SystemExit(f"Evidence differs: {name} at 0x{rva:X}")
    verified_cvars = verify_cvar_offsets(image, args.header)
    assert struct.unpack_from("<Q", image, 0x1C97B28 + 6 * 8)[0] - image_base == 0xCEC510
    interval = struct.unpack_from("<f", image, 0x1AC0064)[0]
    assert interval > 0
    assert struct.unpack_from("<f", image, 0x1AC0098)[0] == 100.0
    # Embedded installed descriptors contain the actual field names. They do
    # not establish C++ offsets by themselves; reflection/machine code does.
    descriptor = bytes(image[0x1B26ED0:0x1B26ED0 + 1400])
    for field in (b"CSubtickMoveStep", b"analog_forward_delta", b"analog_left_delta", b"CBaseUserCmdPB"):
        assert field in descriptor
    print(json.dumps({"path": str(args.client), "sha256": digest,
        "machine": hex(machine), "timestamp": hex(timestamp), "image_size": hex(image_size),
        "input": "embedded object RVA 0x2572460", "finalize_slot": 6,
        "tick_quantum_from_engine_seconds": interval, "verified_code": list(CHECKS),
        "source_offsets": str(args.header), "verified_cvars": verified_cvars}, indent=2))

if __name__ == "__main__":
    main()
