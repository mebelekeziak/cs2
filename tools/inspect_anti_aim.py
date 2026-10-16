"""Verify installed anti-aim command/camera-reset contracts without opening CS2."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, require_bytes
from inspect_movement import CHECKS as MOVEMENT_CHECKS
from inspect_silent_aim import CHECKS as AIM_CHECKS, EXPECTED_HASH

ROOT = Path(__file__).resolve().parents[1]
CHECKS = {
    "replay_wrapper": (0xCEA280, "40534883ec20488bd9e8f226e8ff33c0c683bd0b00000048c783cc0b0000ffffffff8983d80b00008983f00b0000c783f40b0000ffffffff4883c4205bc3"),
    "replay_angle_loads": (0xB6CA17, "488b4f404885c9480f45c1488b4840488d05bbb869014885c9480f45c1488d4c2420f30f105820f30f10501cf30f104818e8133761ff"),
    "replay_camera_stores": (0xB6CA4D, "f20f104424208bd38b442428488bcef20f118688060000898690060000c78694060000ffffffffe82737ffff"),
    "physical_base_source": (0xB663DC, "418b85680400004c8d4d80488b8df0000000418bd6f2410f108560040000894588f20f114580488b01"),
    "physical_base_copy": (0xB6642F, "f20f1045808b4588f2410f11856004000041898568040000488b4c2430488d5580e81bf90100"),
    # The base producer omits zero QAngle scalar fields. History's converter
    # below uses a different contract and must not supply this presence rule.
    "base_angles_message_present": (0xB85DF6, "834b1004488b43404885c0751a488b4b084883e1fcf64308017403488b09e8a7c5aeff48894340"),
    "base_angles_sparse_pitch": (0xB85E1D, "f30f1045000f57c933d20f2ec17a0e750c895018836010fe8b4810eb0c834810018b4810f30f114018"),
    "base_angles_sparse_yaw": (0xB85E46, "f30f1045040f2ec17a0d750b83e1fd89501c894810eb0b83c902894810f30f11401c"),
    "base_angles_sparse_roll": (0xB85E68, "f30f1045080f2ec17a0d750b83e1fb895020894810eb5a83c904894810f30f114020"),
    "command_base_present_before_finalize": (0xB66117, "488b7c24304885ff74064883c710eb03488bfa834f1001488b4730"),
    "base_up": (0xB663D0, "81481000010000f30f117060"),
    "history_source": (0xD027B0, "4863c3488d144048c1e205490397e00b00003b1f7417413b1e7412488b052e787c018078580074054532c0eb0341b0014c89642428410f28d9498bcdf3440f115c2420e808b9fdff"),
    "history_convert": (0xCDE164, "488b4c24780f28def30f10442470450fb6c648894c2428488bd7488bcdf30f11442420e824c50100"),
    "history_angles": (0xCFA751, "f30f104710834910018b4110f30f11411883c802f30f104f1489411083c804f30f11491cf30f104718894110f30f114120"),
    "historical_setup": (0xA74A24, "f64658187417f30f1086a0010000e829320000f30f11452cf30f1145208b465848a9000600007417f30f1086a4010000e807320000f30f114530f30f114524f30f1086a8010000e8f0310000f30f114534f30f114528"),
    "zero_slice": (0xA65C4D, "430f2e343e7a060f846e01000080bc247001000000f2440f116e20896e28f3440f114e2cf3440f114630f3440f117e34"),
    "analog_accumulation": (0xA65F56, "f3470f584c3e10f3470f58443e14"),
    "ingest_after_integration": (0xA711BE, "498bd5488bcf488bd8488b07ff90f0000000"),
    "air_vectors": (0x8E580D, "488d4f144c8d4c24604c8d442430488d542440e8cbd2e500"),
    "air_forward_flatten": (0x8E58CE, "c744244800000000"),
    "air_left_flatten": (0x8E58DE, "f3440f11642438"),
    "air_forward_normalize": (0x8E5923, "410f28cbf30f5ec8f30f59f9f3440f59c9f30f59cef30f117c2440f3440f114c2444f30f114c2448"),
    "air_left_normalize": (0x8E59E1, "410f28cbf30f5ec8f3440f59d1f3440f59c1f3440f59e1f3440f11542430f3440f11442434f3440f11642438"),
    "air_combine": (0x8E5A5B, "f3440f594540c744245800000000f3440f595540440f28a424a0000000f3410f59fff3450f59cf440f28bc2480000000f3410f58faf3450f58c1"),
}
WINDOWS = {
    "native_reset": (0xB6C980, 0x109, "b66c354a1aba07951b36ba4d405023210f2aa87bb14f8cd6f9ad438e2028cf84"),
    "shift_after_reset": (0xB601A0, 0xF0, "a7d405ae7970ba6cdb0ebcf4e3918c14ccbad724c546e468131ee752a22d87b0"),
    "crc_finalizer": (0xCECF90, 0x32E, "41c611ad84d2515854a65c7130cb1bad0f7caf51bad3d880decbb449b542f4b8"),
    "history_builder": (0xD02130, 0xC9B, "50591f8c252fbd1b2038d5b52939907262c4e5eca89021f9e02ef6f426554ebe"),
}


def verify_source(image, base, header):
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", header.read_text(encoding="utf-8-sig"), flags=re.S)
    def value(name, scope=None):
        text = source if scope is None else re.search(rf"namespace\s+{scope}\s*\{{([^}}]*)\}}", source, re.S)[1]
        matches = re.findall(rf"\b{name}\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)", text)
        if len(matches) != 1:
            raise SystemExit(f"Expected one source constant {name}")
        return int(matches[0], 0)
    def rip(site):
        return site + 7 + struct.unpack_from("<i", image, site + 3)[0]
    expected = {
        "Input": rip(0xEE574), "InputVtable": rip(0xEE58C),
        "Replay": 0xCEA280, "ReplaySlot": 8,
        "InputAngles": struct.unpack_from("<I", image, 0xB6CA60)[0],
        "Up": image[0xB663DB], "UpPresent": struct.unpack_from("<I", image, 0xB663D3)[0],
    }
    # Independently connect the game-specific vtable to its native base reset.
    replay = struct.unpack_from("<Q", image, expected["InputVtable"] + expected["ReplaySlot"] * 8)[0] - base
    target = 0xCEA289 + 5 + struct.unpack_from("<i", image, 0xCEA28A)[0]
    if replay != expected["Replay"] or target != 0xB6C980:
        raise SystemExit("Input reset vtable/call target differs")
    if (struct.unpack_from("<Q", image, expected["InputVtable"] - 8)[0] - base != 0x1F236C8 or
            struct.unpack_from("<I", image, 0x1F236C8 + 12)[0] != 0x23283D0 or
            image[0x23283E0:0x23283F1] != b".?AVCCSGOInput@@\0"):
        raise SystemExit("Input RTTI ownership differs")
    for name, native in expected.items():
        if value(name, "base" if name in ("Up", "UpPresent") else None) != native:
            raise SystemExit(f"Source constant {name} differs from native operand: {native:#x}")
    if struct.unpack_from("<I", image, 0xB6CA66)[0] != expected["InputAngles"] + 8:
        raise SystemExit("Input angle roll store no longer follows pitch/yaw")
    prefix = re.search(r"ReplayBytes\[\]\s*\{([^}]*)\}", source, re.S)
    parsed = bytes(int(x.strip(), 0) for x in prefix[1].split(",") if x.strip()) if prefix else b""
    if parsed != bytes.fromhex(CHECKS["replay_wrapper"][1])[:23]:
        raise SystemExit("ReplayBytes differs from independently frozen entry")
    for name, record, string, text in (
            ("Attack", 0x1B28D10, 0x1B28F78, b"IN_ATTACK\0"),
            ("Use", 0x1B28DB0, 0x1B28FB8, b"IN_USE\0"),
            ("Attack2", 0x1B28E50, 0x1B29000, b"IN_ATTACK2\0")):
        native_name, native_value = struct.unpack_from("<QQ", image, record)
        if native_name - base != string or image[string:string + len(text)] != text or value(name) != native_value:
            raise SystemExit(f"Named input enum {name} differs")
        expected[name] = native_value
    return {name: hex(native) for name, native in expected.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"))
    parser.add_argument("--header", type=Path, default=ROOT / "include/anti_aim_offsets.h")
    args = parser.parse_args()
    raw, image, base, nt = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH:
        raise SystemExit("Client changed: repeat anti-aim command/camera investigation")
    if struct.unpack_from("<I", raw, nt + 8)[0] != 0x6AD01314 or len(image) != 0x299D000:
        raise SystemExit("Client PE identity differs")
    require_bytes(image, MOVEMENT_CHECKS)
    require_bytes(image, AIM_CHECKS)
    require_bytes(image, CHECKS)
    for name, (rva, size, digest_window) in WINDOWS.items():
        if hashlib.sha256(image[rva:rva + size]).hexdigest() != digest_window:
            raise SystemExit(f"Changed native side-effect contract: {name}")
    verified = verify_source(image, base, args.header)
    print(json.dumps({"path": str(args.client), "sha256": digest, "source_offsets": str(args.header),
        "verified_source": verified, "verified_code": list(CHECKS), "verified_body_contracts": list(WINDOWS),
        "abi": "void(input*, int slot); native camera reset before history counters clear",
        "live_observation": False}, indent=2))


if __name__ == "__main__":
    main()
