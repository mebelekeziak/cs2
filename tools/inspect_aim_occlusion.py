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
WINDOWS = {'bullet_filter_producer': (8949126, 100, '792c891338fa2199883159a342b2b05643e555e5383d074be08709a7194adc0a'),
 'filter_constructor': (3484864, 310, '9359ea4e91014bf5977812ee098c92b553e269b203ee4ae1ca013bfd2ca04324'),
 'ray_shape_initializer': (3598464, 85, '5fb55f27d925068b696009d304453ca831411627f83f431b2f249eae53f01cc9'),
 'result_initializer': (24622862, 170, '88fc93903c40d8ca8aad2eac36f262163b8473461832cef2857d271b461ddd8c'),
 'result_didhit': (3542272, 32, '10dcd6f8eb61a9a4b2954bfc29d075ab1f4a5bb72fddd41fb5fe1e622735092d'),
 'result_entity_consumer': (10102213, 10, '489025be08ab13334af2b658649c40be5522b77a65b0c0874a8264ac234e45b9'),
 'bullet_call_from_fx': (13784288, 43, '442033ce6a23b5b46e51801f81455318c44f923cfa5a99f080876d26b8640b5c'),
 'native_trace_camera_call': (11945223, 48, '7bc5ea653e84da54b73915cda37b471f0a9324cc1af96c52cfcd93fdf75e7400')}
WINDOWS['trace_complete'] = (0xa1ae30, 1294, '82299fc0b20a29839fab528832d9669b15725ba4b074d6b34b37ca567f9695b6')
WINDOWS['result_constructor_complete'] = (0x177b560, 600, 'a1f6a950d807695ccec68cacba93b8d5544e68bf99916eb26327e0da931b7546')
FILTER_ENTRY = "48895c24084889742410574883ec200fb6413933ff24c9c74134ffff000f"


def verify_source(image, header):
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", header.read_text(encoding="utf-8-sig"), flags=re.S)
    # Decode operands in the separately frozen native producer/consumer paths.
    expected = {
        "InitializeFilter": 0x888da1 + 5 + struct.unpack_from("<i", image, 0x888da2)[0],
        "ShotMask": struct.unpack_from("<I", image, 0x888d9d)[0],
        "ShotLayer": struct.unpack_from("<I", image, 0x888d92)[0],
        "ShotFlags": image[0x888d9a],
        "ShotFlags39": image[0x888dba],
        "ShotExclude": struct.unpack_from("<Q", image, 0x888dc2)[0],
        "TraceHitFlag": struct.unpack_from("<I", image, 0x360d13)[0],
        # Result lives at rbp-0x20 (0x9a232c); selected-entity compare reads rbp-0x18.
        "TraceEntity": struct.unpack_from("<b", image, 0x9a25c8)[0] - (-0x20),
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
    require_bytes(image, {"bullet_filter_entry": (0x352cc0, FILTER_ENTRY)})
    for name, (rva, size, expected) in WINDOWS.items():
        if hashlib.sha256(image[rva:rva+size]).hexdigest() != expected:
            raise SystemExit(f"Native occlusion contract window changed: {name}")
    # Shared layout/manager/shape/result constants have existing independent
    # camera call/constructor evidence; do not copy their runtime literals.
    shared = verify_shared(image, base, args.shared_header)
    constants = verify_source(image, args.header)
    # Complete zero-bounds shape branch ends in a return and selects ray kind 0.
    require_bytes(image, {"point_ray_branch": (0x36e8a6,
        "c6412800f20f1002f20f11018b4208894108c7410c00000000c3")})
    if struct.unpack_from("<f", image, 0x1ac8084)[0] != 1.0:
        raise SystemExit("DidHit fraction comparison constant changed")
    print(json.dumps({"client_sha256": digest, "constants": constants,
        "shared_constants": shared,
        "windows": {name: {"rva": hex(rva), "size": size, "sha256": digest}
                    for name, (rva, size, digest) in WINDOWS.items()},
        "abi": "native zero-bounds ray; six-argument synchronous TraceShape; native bullet filter constructor",
        "scope": "static ABI and direct obstruction only; no penetration simulation or live gameplay observation"}, indent=2))


if __name__ == "__main__":
    main()
