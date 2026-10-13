"""Verify movement evidence against the installed PE, without opening a process.

No game files are changed. A different build requires a new investigation, not
automatic substitution of internet offsets. Standard-library Python only.
"""
import argparse
import hashlib
import json
from pathlib import Path
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

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=Path(
        r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"))
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
        "tick_quantum_from_engine_seconds": interval, "verified_code": list(CHECKS)}, indent=2))

if __name__ == "__main__":
    main()
