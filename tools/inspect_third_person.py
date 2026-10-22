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
EXPECTED_HASH = "5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49"
CHECKS = {
    "override_entry": (0xB99250, "48895c241055488bec4881ec80000000488b01488bda"),
    "query_complete": (0xB627A0, "4863c24869d0280900000fb6840a29020000c3"),
    "mode_slot_getter": (0xB96570, "8b41288902488bc2c3"),
    "camera_query_call": (0xB9929A, "8b5510488b0d5c2a6a01e8f794fcff"),
    "shape_entry": (0x36DDB0, "f30f10420c0f2e027a367534f30f1042100f2e42047a297527f30f1042140f2e"),
    "trace_entry": (0xA1A000, "488954241048894c240855535657415441564157488dac24f0dbffffb8102500"),
    "trace_init_entry": (0x1779300, "4055415541574883ec304c8b3dafaafb004533ed488be94d85ff0f858e010000"),
    "filter_populate_entry": (0xB722A0, "48895c2408574883ec200fb6442450488bf9884137488bda448849384c894108"),
}
WINDOWS = {
    "override_view": (0xB99250, 0x2E0),
    "query_local_model": (0xB1BE10, 0x7C),
    "query_local_wrapper": (0xB1BEE0, 0x44),
    "camera_thinker": (0xB62A20, 0xED0),
    "shape_initializer": (0x36DDB0, 0x55),
    "filter_population": (0xB722A0, 0xE1),
    "local_pawn_resolver": (0x96A8F0, 0x97),
}
WINDOW_HASHES = {
    "override_view": "23d45bf1ef215c91ba0872fa99e4f6ed494b7fcde5ddfbba753ad98e96489e21",
    "query_local_model": "b6422888b2076483183ad0f3e445cacc6cd05574ab0533cc26c2b8e051580078",
    "query_local_wrapper": "c20e142030474510dbc83c5785d3e74b429f3d212291c02c5594642ef266a930",
    "camera_thinker": "dc557df4e54058cfcfe2494877cbb362d27e8115c22c1826b33f386aba0dfe53",
    "shape_initializer": "5fb55f27d925068b696009d304453ca831411627f83f431b2f249eae53f01cc9",
    "filter_population": "5c52bfcf18d2507236a1837cdfff74b0c2582c3ba676fa53c9a568488bdfc7ac",
    "local_pawn_resolver": "00ab6bae7d5701f7f20d7585d44596fe268d495cf08567f3cec49435449e2cbf",
}


def verify_source(image, base, path):
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8-sig"), flags=re.S)
    expected = {
        "OverrideView": 0xB99250, "IsThirdPerson": 0xB627A0,
        "LocalPawn": 0x96A8F0, "ModeSharedVtable": 0x1C68368,
        "ModeNormalVtable": 0x1C9FC20, "ModeSlot": image[0xB96572],
        "ModeSlotGetter": 0xB96570,
        "ViewOrigin": struct.unpack_from("<I", image, 0xB9931E)[0],
        "ViewAngles": struct.unpack_from("<I", image, 0xB99490)[0],
        "IsThirdPersonCameraReturn": 0xB992A9,
        "InputPointer": 0xB9929D + 7 + struct.unpack_from("<i", image, 0xB992A0)[0],
        "TraceManager": 0xB636C7 + 7 + struct.unpack_from("<i", image, 0xB636CA)[0],
        "TraceShape": 0xA1A000, "InitializeShape": 0x36DDB0,
        "InitializeTrace": 0x1779300, "PopulateFilter": 0xB722A0,
        "FilterVtable": 0xB63557 + 7 + struct.unpack_from("<i", image, 0xB6355A)[0],
        "ShapeSize": 0x30, "TraceSize": 0xC0, "FilterSize": 0x48,
        "TraceFraction": 0xAC, "CameraMask": 0x80,
        "CameraExclude": 0xC0000, "CameraLayer": 3,
    }
    for name, native in expected.items():
        matches = re.findall(rf"\b{name}\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)", source)
        if len(matches) != 1 or int(matches[0], 0) != native:
            raise SystemExit(f"Source constant {name} differs from native evidence {native:#x}")
    extent = re.findall(r"\bHullExtent\s*=\s*([0-9.]+)f", source)
    native_extent = struct.unpack_from("<f", image, 0x2234550)[0]
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
    input_slot = 0xB9929D + 7 + struct.unpack_from("<i", image, 0xB992A0)[0]
    input_object = 0xEE574 + 7 + struct.unpack_from("<i", image, 0xEE577)[0]
    if struct.unpack_from("<Q", image, input_slot)[0] - base != input_object or input_object != 0x257B640:
        raise SystemExit("Native camera input pointer differs from constructed CCSGOInput")
    for name, rva, slot, target in (
        (b".?AVClientModeShared@@", 0x1C68368, 16, 0xB99250),
        (b".?AVClientModeCSNormal@@", 0x1C9FC20, 16, 0xD02E30),
    ):
        if primary_vtable(image, base, name) != rva:
            raise SystemExit("Client mode RTTI/vtable differs")
        if struct.unpack_from("<Q", image, rva + slot * 8)[0] - base != target:
            raise SystemExit("Client mode OverrideView slot differs")
        if struct.unpack_from("<Q", image, rva + 0x100)[0] - base != 0xB96570:
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
