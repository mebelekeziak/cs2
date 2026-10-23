"""Verify installed native third-person view/query and camera-hull contracts.

Reads installed files only. Frozen installed-binary evidence, no guessed offset
substitution, no game launch, standard-library Python only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable, require_bytes

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_HASH = "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"
CHECKS = {
    "override_entry": (0xb9a090, "48895c241055488bec4881ec80000000488b01488bda"),
    "query_complete": (0xb635e0, "4863c24869d0280900000fb6840a29020000c3"),
    "mode_slot_getter": (0xb973b0, "8b41288902488bc2c3"),
    "camera_query_call": (0xb9a0da, "8b5510488b0d1c3b6a01e8f794fcff"),
    "shape_entry": (0x36e880, "f30f10420c0f2e027a367534f30f1042100f2e42047a297527f30f1042140f2e"),
    "trace_entry": (0xa1ae30, "488954241048894c240855535657415441564157488dac24f0dbffffb8102500"),
    "trace_init_entry": (0x177b560, "4055415541574883ec304c8b3defa7fb004533ed488be94d85ff0f858e010000"),
    "filter_populate_entry": (0xb730e0, "48895c2408574883ec200fb6442450488bf9884137488bda448849384c894108"),
}
WINDOWS = {
    "override_view": (0xb9a090, 0x2E0),
    "query_local_model": (0xb1cc50, 0x7C),
    "query_local_wrapper": (0xb1cd20, 0x44),
    "camera_thinker": (0xb63860, 0xED0),
    "shape_initializer": (0x36e880, 0x55),
    "filter_population": (0xb730e0, 0xE1),
    "local_pawn_resolver": (0x96b720, 0x97),
    "trace_shape_complete": (0xA1AE30, 0x50E),
    "trace_initializer_complete": (0x177B560, 0x258),
}
WINDOW_HASHES = {
    "override_view": "9fdc54a1b6236004af493be2df9b01027d529a28b28f6c49a52d1a04e6e8e91f",
    "query_local_model": "346e87950583e18e4329be01d9d7a984669589d32805221d6ed550293bb50f2f",
    "query_local_wrapper": "f758941787e4481958843abd0a526daffbb86c35646f7c0c6e55d4b5e2c47f43",
    "camera_thinker": "a3debf5bae6e3519b08344592d2a057e29fd3ac1446d62b4d8c4e3ffa4bc8704",
    "shape_initializer": "5fb55f27d925068b696009d304453ca831411627f83f431b2f249eae53f01cc9",
    "filter_population": "4a45d0f918b47919801a83019f61b9e3e005c5ff085a6d5f655148a29bd39928",
    "local_pawn_resolver": "d4e70d0cf70c604f2984cbd72c7be2681a2b421fcb951bc5e092c3cf5c5df456",
    "trace_shape_complete": "82299fc0b20a29839fab528832d9669b15725ba4b074d6b34b37ca567f9695b6",
    "trace_initializer_complete": "a1f6a950d807695ccec68cacba93b8d5544e68bf99916eb26327e0da931b7546",
}


def verify_source(image, base, path):
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8-sig"), flags=re.S)
    expected = {
        "OverrideView": 0xb9a090, "IsThirdPerson": 0xb635e0,
        "LocalPawn": 0x96b720, "ModeSharedVtable": 0x1c6a2a8,
        "ModeNormalVtable": 0x1ca1b50, "ModeSlot": image[0xb973b2],
        "ModeSlotGetter": 0xb973b0,
        "ViewOrigin": struct.unpack_from("<I", image, 0xb9a15e)[0],
        "ViewAngles": struct.unpack_from("<I", image, 0xb9a2d0)[0],
        "IsThirdPersonCameraReturn": 0xb9a0e9,
        "InputPointer": 0xb9a0dd + 7 + struct.unpack_from("<i", image, 0xb9a0e0)[0],
        "TraceManager": 0xb64507 + 7 + struct.unpack_from("<i", image, 0xb6450a)[0],
        "TraceShape": 0xa1ae30, "InitializeShape": 0x36e880,
        "InitializeTrace": 0x177b560, "PopulateFilter": 0xb730e0,
        "FilterVtable": 0xb64397 + 7 + struct.unpack_from("<i", image, 0xb6439a)[0],
        "ShapeSize": 0x30, "TraceSize": 0xC0, "FilterSize": 0x48,
        "TraceFraction": 0xAC, "CameraMask": 0x80,
        "CameraExclude": 0xC0000, "CameraLayer": 3,
    }
    for name, native in expected.items():
        matches = re.findall(rf"\b{name}\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)", source)
        if len(matches) != 1 or int(matches[0], 0) != native:
            raise SystemExit(f"Source constant {name} differs from native evidence {native:#x}")
    extent = re.findall(r"\bHullExtent\s*=\s*([0-9.]+)f", source)
    native_extent = struct.unpack_from("<f", image, 0x2236550)[0]
    if len(extent) != 1 or float(extent[0]) != native_extent or native_extent != 9.0:
        raise SystemExit("Camera hull extent differs")
    for array, key in (("OverrideViewBytes", "override_entry"), ("IsThirdPersonBytes", "query_complete"),
                       ("ModeSlotGetterBytes", "mode_slot_getter"),
                       ("InitializeShapeBytes", "shape_entry"), ("TraceShapeBytes", "trace_entry"),
                       ("InitializeTraceBytes", "trace_init_entry"), ("PopulateFilterBytes", "filter_populate_entry")):
        match = re.search(rf"\b{array}\[\]\s*\{{([^}}]*)\}}", source, re.S)
        values = bytes(int(x.strip(), 0) for x in match[1].split(",") if x.strip()) if match else b""
        if values != bytes.fromhex(CHECKS[key][1]):
            raise SystemExit(f"{array} differs from frozen native entry")
    return {name: hex(value) for name, value in expected.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"))
    parser.add_argument("--header", type=Path, default=ROOT / "include/third_person_offsets.h")
    args = parser.parse_args()
    raw, image, base, nt = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH:
        raise SystemExit("Client changed: repeat third-person camera investigation")
    require_bytes(image, CHECKS)
    for name, (rva, size) in WINDOWS.items():
        if hashlib.sha256(image[rva:rva + size]).hexdigest() != WINDOW_HASHES[name]:
            raise SystemExit(f"Native function/consumer window changed: {name}")
    input_slot = 0xb9a0dd + 7 + struct.unpack_from("<i", image, 0xb9a0e0)[0]
    input_object = 0xee574 + 7 + struct.unpack_from("<i", image, 0xee577)[0]
    if struct.unpack_from("<Q", image, input_slot)[0] - base != input_object or input_object != 0x257d780:
        raise SystemExit("Native camera input pointer differs from constructed CCSGOInput")
    for name, rva, slot, target in (
        (b".?AVClientModeShared@@", 0x1c6a2a8, 16, 0xb9a090),
        (b".?AVClientModeCSNormal@@", 0x1ca1b50, 16, 0xd04490),
    ):
        if primary_vtable(image, base, name) != rva:
            raise SystemExit("Client mode RTTI/vtable differs")
        if struct.unpack_from("<Q", image, rva + slot * 8)[0] - base != target:
            raise SystemExit("Client mode OverrideView slot differs")
        if struct.unpack_from("<Q", image, rva + 0x100)[0] - base != 0xb973b0:
            raise SystemExit("Client mode slot getter differs")
    constants = verify_source(image, base, args.header)
    print(json.dumps({"client_sha256": digest, "constants": constants,
                      "windows": {name: {"rva": hex(rva), "size": size,
                          "sha256": hashlib.sha256(image[rva:rva + size]).hexdigest()}
                          for name, (rva, size) in WINDOWS.items()},
                      "abi": "void(mode*, view*); int(input*, int slot); synchronous six-argument native hull trace",
                      "scope": "static installed-binary contracts; live third-person appearance not observed"}, indent=2))


if __name__ == "__main__":
    main()
