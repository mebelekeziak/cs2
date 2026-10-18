"""Verify installed client native bullet-filter and direct ray contracts.

Read-only, standard-library checks; never launches or opens the game process.
The check establishes obstruction ABI, not live visibility or penetration.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from verify_sky_draw import mapped, require_bytes
from inspect_third_person import EXPECTED_HASH, CHECKS as SHARED_CHECKS, verify_source as verify_shared

ROOT = Path(__file__).resolve().parents[1]
WINDOWS = {
    "bullet_filter_producer": (0x887F56, 0x64, "76c373d706fdf7d001e05a61a4becb49c831c33ae1d57a5e3a2acf8c425cbfe8"),
    "filter_constructor": (0x352920, 0x136, "6d865d66564d6b89ee213a23e8e84fd713dd5a2c61086ab18112c915cd06dd75"),
    "ray_shape_initializer": (0x36DDB0, 0x55, "5fb55f27d925068b696009d304453ca831411627f83f431b2f249eae53f01cc9"),
    "result_initializer": (0x17794AE, 0xAA, "79a157576985c2e53bb042ae1a73152c9b6a35d31e0ae3e18207e68123c0c31f"),
    "result_didhit": (0x360720, 0x20, "df26c9b49ad9cda2b6bf3d6f9a8e0d1668382587696a998a373d2eec3966f1ef"),
    "result_entity_consumer": (0x9A1795, 0xA, "489025be08ab13334af2b658649c40be5522b77a65b0c0874a8264ac234e45b9"),
    "bullet_call_from_fx": (0xD23E90, 0x2B, "2f61aea6e1b62ee7071495e2689cfbc86ac48f5644b6c892be39a7ce1e078207"),
    "native_trace_camera_call": (0xB636C7, 0x30, "d7ba88b13a639f5386a44e24fe69d05594f74ce78ab3e5291ba9f29037fa2ea7"),
}
FILTER_ENTRY = "48895c24084889742410574883ec200fb6413933ff24c9c74134ffff000f"


def verify_source(image, header):
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", header.read_text(encoding="utf-8-sig"), flags=re.S)
    # Decode operands in the separately frozen native producer/consumer paths.
    expected = {
        "InitializeFilter": 0x887F71 + 5 + struct.unpack_from("<i", image, 0x887F72)[0],
        "ShotMask": struct.unpack_from("<I", image, 0x887F6D)[0],
        "ShotLayer": struct.unpack_from("<I", image, 0x887F62)[0],
        "ShotFlags": image[0x887F6A],
        "ShotFlags39": image[0x887F8A],
        "ShotExclude": struct.unpack_from("<Q", image, 0x887F92)[0],
        "TraceHitFlag": struct.unpack_from("<I", image, 0x360733)[0],
        # Result lives at rbp-0x20 (0x9A14FC); selected-entity compare reads rbp-0x18.
        "TraceEntity": struct.unpack_from("<b", image, 0x9A1798)[0] - (-0x20),
    }
    for name, value in expected.items():
        matches = re.findall(rf"\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|\d+)", source)
        if len(matches) != 1 or int(matches[0], 0) != value:
            raise SystemExit(f"Source constant {name} differs from native operand {value:#x}")
    match = re.search(r"\bInitializeFilterBytes\[\]\s*\{([^}]*)\}", source, re.S)
    array = bytes(int(item.strip(), 0) for item in match[1].split(",") if item.strip()) if match else b""
    if array != bytes.fromhex(FILTER_ENTRY):
        raise SystemExit("InitializeFilterBytes differs from native filter constructor")
    return {name: hex(value) for name, value in expected.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"))
    parser.add_argument("--header", type=Path, default=ROOT / "include/aim_occlusion_offsets.h")
    parser.add_argument("--shared-header", type=Path, default=ROOT / "include/third_person_offsets.h")
    args = parser.parse_args()
    raw, image, base, _ = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH:
        raise SystemExit("Client changed: repeat native aim-occlusion contract investigation")
    require_bytes(image, {name: SHARED_CHECKS[name] for name in ("shape_entry", "trace_entry", "trace_init_entry")})
    require_bytes(image, {"bullet_filter_entry": (0x352920, FILTER_ENTRY)})
    for name, (rva, size, expected) in WINDOWS.items():
        if hashlib.sha256(image[rva:rva+size]).hexdigest() != expected:
            raise SystemExit(f"Native occlusion contract window changed: {name}")
    # Shared layout/manager/shape/result constants have existing independent
    # camera call/constructor evidence; do not copy their runtime literals.
    shared = verify_shared(image, base, args.shared_header)
    constants = verify_source(image, args.header)
    # Complete zero-bounds shape branch ends in a return and selects ray kind 0.
    require_bytes(image, {"point_ray_branch": (0x36DDD6,
        "c6412800f20f1002f20f11018b4208894108c7410c00000000c3")})
    if struct.unpack_from("<f", image, 0x1AC6074)[0] != 1.0:
        raise SystemExit("DidHit fraction comparison constant changed")
    print(json.dumps({"client_sha256": digest, "constants": constants,
        "shared_constants": shared,
        "windows": {name: {"rva": hex(rva), "size": size, "sha256": digest}
                    for name, (rva, size, digest) in WINDOWS.items()},
        "abi": "native zero-bounds ray; six-argument synchronous TraceShape; native bullet filter constructor",
        "scope": "static ABI and direct obstruction only; no penetration simulation or live gameplay observation"}, indent=2))


if __name__ == "__main__":
    main()
