"""Verify installed landing-hull and crouch-input contracts without launching CS2."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, DEFAULT

ROOT = Path(__file__).resolve().parents[1]


def verify(client, header):
    contract = json.loads((ROOT / "tools/jumpbug_contract.json").read_text())
    raw, image, base, _ = mapped(client)
    if hashlib.sha256(raw).hexdigest() != contract["client_sha256"]:
        raise SystemExit("Client changed: repeat jumpbug landing investigation")
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", header.read_text(encoding="utf-8-sig"), flags=re.S)
    for name, expected in contract["constants"].items():
        matches = re.findall(rf"\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;", source)
        if len(matches) != 1 or int(matches[0], 0) != expected:
            raise SystemExit(f"Jumpbug source constant differs: {name}")
    for name, expected in contract["prefixes"].items():
        match = re.search(rf"\b{name}Bytes\[\]\s*\{{([^}}]*)\}}", source, re.S)
        got = bytes(int(x.strip(), 0) for x in match[1].split(",") if x.strip()) if match else b""
        if got.hex() != expected:
            raise SystemExit(f"Jumpbug source fingerprint differs: {name}")
    for name, window in contract["windows"].items():
        start, size = window["rva"], window["size"]
        if hashlib.sha256(image[start:start + size]).hexdigest() != window["sha256"]:
            raise SystemExit(f"Jumpbug native window differs: {name}")
    # Independent schema registration: actual gravity is the native integration operand.
    registration = 0x21F6E78
    fields = struct.unpack_from("<Q", image, registration + 0x28)[0] - base
    found = {}
    for index in range(struct.unpack_from("<H", image, registration + 0x1C)[0]):
        record = fields + index * 32
        pointer = struct.unpack_from("<Q", image, record)[0] - base
        name = bytes(image[pointer:image.find(0, pointer)]).decode("ascii")
        found[name] = struct.unpack_from("<I", image, record + 0x10)[0]
    if found["m_flActualGravityScale"] != contract["constants"]["ActualGravityScale"]:
        raise SystemExit("Actual gravity schema differs")
    def rip(site): return site + 7 + struct.unpack_from("<i", image, site + 3)[0]
    if rip(0x6BB0B) != contract["constants"]["Gravity"] or rip(0xB2C48) != contract["constants"]["StandableNormal"]:
        raise SystemExit("Gravity/normal cvar registration differs")
    if image[0x8C9DA9:0x8C9DAC] != bytes.fromhex("c0eb02") or contract["constants"]["Duck"] != 1 << image[0x8C9DAB]:
        raise SystemExit("Native crouch input bit differs")
    # CategorizePosition copies XY and Z from the move-data origin. This is
    # the movement position, unlike the separately sampled scene-node origin.
    if (image[0x8C3731:0x8C3735] != bytes.fromhex("f20f1086") or
            image[0x8C3739:0x8C373B] != bytes.fromhex("8b86") or
            struct.unpack_from("<I", image, 0x8C3735)[0] != contract["constants"]["MoveOrigin"] or
            struct.unpack_from("<I", image, 0x8C373B)[0] != contract["constants"]["MoveOrigin"] + 8):
        raise SystemExit("Native movement origin operands differ")
    # Ground-consumer stack frame: RBP=entry_RSP-0x80, RSP=entry_RSP-0x180;
    # trace at [RSP+0x70] is [RBP-0x90]. Decode each consumer member anew.
    # LEA RDX,[RSP+70] at 0x8E704B establishes the result pointer.
    if image[0x8E704B:0x8E7050] != bytes.fromhex("488d542470"):
        raise SystemExit("Ground trace result stack location differs")
    trace_rbp_offset = -0x100 + image[0x8E704F]
    native_fraction = image[0x8E70EC] - trace_rbp_offset
    native_normal = image[0x8E7118] - trace_rbp_offset - 8
    if native_fraction != contract["constants"]["TraceFraction"] or native_normal != contract["constants"]["TraceNormal"]:
        raise SystemExit("Ground trace fraction/normal operands differ")
    return {"client_sha256": contract["client_sha256"], "windows": contract["windows"],
            "schema_actual_gravity": hex(found["m_flActualGravityScale"]),
            "abi": "void(cache*, trace*, start*, end*, bbox*, movement_filter*)",
            "duck_input_bit": 4, "live_observation": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=DEFAULT / "csgo/bin/win64/client.dll")
    parser.add_argument("--header", type=Path, default=ROOT / "include/movement_jumpbug_offsets.h")
    args = parser.parse_args()
    print(json.dumps(verify(args.client, args.header), indent=2))


if __name__ == "__main__":
    main()
