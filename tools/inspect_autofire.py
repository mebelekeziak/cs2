"""Verify native primary-request producer, event ownership and source constants.

Reads installed AMD64 client.dll only. Never launches a process or rewrites
source offsets. Regenerate evidence deliberately when the installed hash changes.
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
    "recorder": (0xD055C0, "4053574881ecc800000080b9bd0b0000008bda488bf90f8543060000"),
    "normal_recorder_call": (0xD02171, "e84a340000"),
    "slot_getter": (0xB70640, "4863c24869c0280900004805580200004803c1c3"),
    "snapshot_source": (0xB60792, "4b8b042f41ffc6488947304b8b442f08480947384b8b442f1048094740f2430f10442f18f20f114748438b442f20894750f2430f10842f30040000f20f118760040000438b842f38040000898768040000438b442f24894754"),
    "event_producer_capacity": (0xB609E5, "4863475c83f820750f488d0dcb4c1001ff1525cef400eb1b0f10034883c00348c1e0050f1104380f104b100f114c3810ff475c"),
    "event_consumer": (0xD05627, "e814b0e6ff4c6387f00b000032db4532f666c78424e000000000004533e44c8bc88b482c443bc17d62498bd048c1e2054883c2384803d06690488b024883f8017408483d0008000075204883f801498bcc0fb642080f95c1888439c80b00000fb6420808840ce0000000418b492c41ffc04883c220443bc17cbf440fb6b424e10000000fb69c24e0000000898ff00b0000"),
    "native_primary_index": (0xD059A0, "80bc1ce0000000000f840202000083bc9fcc0b0000ff0f85f4010000"),
    "native_index_store": (0xD05A34, "4489ac9fcc0b0000"),
    "native_history_append": (0xD05817, "ff87d80b00004a8d1c6d00000000488b8fe00b00004903dd48c1e3054803cbe89532fdff488b442464488bafe00b00004889042b488b44246c4889442b08c7442b54ffffffff"),
    "same_tick_history": (0xD0570F, "85d27e1f448d6aff4963c5488d0c4048c1e10548038fe00b00003969500f8459020000"),
    "normal_builder_index": (0xD02824, "8b0f498d45204d8d454c83f9ff7406830820418908"),
    "builder_compaction": (0xD021BD, "8b4108394411a87d444883c2a041b8600000004803d1e84848d500418b9ed80b00002bdf41899ed80b00004183becc0b0000ff74074589a6cc0b0000"),
}


def verify_source(image, header):
    def u32(site): return struct.unpack_from("<I", image, site)[0]
    def rel(site): return site + 5 + struct.unpack_from("<i", image, site + 1)[0]
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", header.read_text(encoding="utf-8-sig"), flags=re.S)
    values = {}
    for name, literal in re.findall(r"\b(?:ptrdiff_t|uint64_t|int)\s+(\w+)\s*=\s*(0[xX][0-9A-Fa-f]+|[0-9]+)\s*;", source):
        if name in values:
            raise SystemExit(f"Duplicate source constant: {name}")
        values[name] = int(literal, 0)
    native_buttons = u32(0xB7064C)
    # Slot state has native buttons at +0x30, count +0x5C and inline
    # events +(count+3)*0x20. The independent recorder views the same list
    # from native-buttons: count +0x2C, first button +0x38.
    slot = native_buttons - image[0xB6079C]
    stride = 1 << image[0xB60A07]
    event_start = slot + image[0xB60A03] * stride
    expected = {
        "RecordHistory": rel(0xD02171),
        "InputNativeButtons": native_buttons,
        "InputEventCount": slot + image[0xB609E8],
        "InputEvents": event_start,
        "InputEventStride": stride,
        "InputEventCapacity": image[0xB609EB],
        "EventButton": native_buttons + image[0xD0565A] - event_start,
        "EventPressed": native_buttons + image[0xD0565A] - event_start + image[0xD0567B],
        "InputPrimaryHeld": u32(0xD05682),
        "InputPrimaryIndex": u32(0xD05A38),
        "InputHistoryCount": u32(0xD05819),
        "InputEventsProcessed": u32(0xD056B4),
        "PrimaryAttack": 1,
    }
    if native_buttons + image[0xD0564A] != expected["InputEventCount"]:
        raise SystemExit("Event producer/consumer count layouts disagree")
    if stride != image[0xD0569B]:
        raise SystemExit("Event producer/consumer strides disagree")
    if set(values) != set(expected):
        raise SystemExit(f"Source inventory differs: {set(values) ^ set(expected)}")
    for key, value in expected.items():
        if values[key] != value:
            raise SystemExit(f"Source offset mismatch: {key}={values[key]:#x}; native operand requires {value:#x}")
    entry = re.search(r"RecordHistoryBytes\[\]\s*\{([^}]*)\}", source, re.S)
    if not entry:
        raise SystemExit("Missing RecordHistoryBytes")
    entry = bytes(int(value.strip(), 0) for value in entry.group(1).split(",") if value.strip())
    if entry != bytes.fromhex(CHECKS["recorder"][1])[:len(entry)] or len(entry) != 22:
        raise SystemExit("RecordHistoryBytes differs from independent frozen entry")
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=Path(
        r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"))
    parser.add_argument("--header", type=Path, default=ROOT / "include/aim_autofire_offsets.h")
    args = parser.parse_args()
    raw, image, base, nt = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH:
        raise SystemExit("Client changed: repeat native attack-request investigation.")
    if struct.unpack_from("<I", raw, nt + 8)[0] != 0x6AD01314 or len(image) != 0x299D000:
        raise SystemExit("Client PE identity differs")
    require_bytes(image, CHECKS)
    # The native named enumeration, independently of C++ or event constants.
    if (struct.unpack_from("<Q", image, 0x1B28D10)[0] - base != 0x1B28F78 or
        image[0x1B28F78:0x1B28F82] != b"IN_ATTACK\0" or
        struct.unpack_from("<Q", image, 0x1B28D18)[0] != 1):
        raise SystemExit("Named primary attack enum differs")
    values = verify_source(image, args.header)
    print(json.dumps({"client": str(args.client), "client_sha256": digest,
        "source_offsets": str(args.header), "verified_code": list(CHECKS),
        "source_constants": {key: hex(value) for key, value in values.items()},
        "live_observation": False}, indent=2))


if __name__ == "__main__":
    main()
