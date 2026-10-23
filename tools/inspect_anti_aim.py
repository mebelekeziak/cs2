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
    "pitch_down_default": (0xCE9E0, "488bc44881ecb8000000f30f1015a697b901488d156736b90133c90f57c0894c24300f57c966894c2434"),
    "pitch_up_default": (0xCEB20, "488bc44881ecb8000000f30f10156696b901488d153735b90133c90f57c0894c24300f57c966894c2434"),
    "native_pitch_clamp": (0xB649B0, "40534883ec20488d0da3d09f01488bdae82bc15fff488d0da4d09f01f30f11442438e819c15fff0f57053237f6004c8d442438488d542440f30f11442440488bcbe80a585fff4c8d442438f30f1103"),
    "pitch_default_89": (0x1C68198, "0000b242"),
    "clone_proto_sequence": (0xB5F16B, "488d4f104885ff488d5310480f44c8e8516dc5ff8b4308488bcf894708"),
    "producer_snapshot_copy": (0xB66D8C, "4c89442430488d4de0458ba090000000498bd0e8ac83ffff"),
    "setup_dispatch_same_command": (0xA71E5C, "488b074d8bc6498bd5488bcfff9038010000"),
    "command_lookup_entry": (0x9452D0, "40534883ec208bdae883000000"),
    "replay_buttons_reset": (0xB6D837, "4889866002000048898668020000488b476048898650020000"),
    "replay_wrapper": (0xCEB8E0, "40534883ec20488bd9e8d21ee8ff33c0c683bd0b00000048c783cc0b0000ffffffff8983d80b00008983f00b0000c783f40b0000ffffffff4883c4205bc3"),
    "replay_angle_loads": (0xB6D857, "488b4f404885c9480f45c1488b4840488d05cbca69014885c9480f45c1488d4c2420f30f105820f30f10501cf30f104818e8b32861ff"),
    "replay_camera_stores": (0xB6D88D, "f20f104424208bd38b442428488bcef20f118688060000898690060000c78694060000ffffffffe82737ffff"),
    "physical_base_source": (0xB6721C, "418b85680400004c8d4d80488b8df0000000418bd6f2410f108560040000894588f20f114580488b01"),
    "physical_base_copy": (0xB6726F, "f20f1045808b4588f2410f11856004000041898568040000488b4c2430488d5580e81bf90100"),
    # The base producer omits zero QAngle scalar fields. History's converter
    # below uses a different contract and must not supply this presence rule.
    "base_angles_message_present": (0xB86C36, "834b1004488b43404885c0751a488b4b084883e1fcf64308017403488b09e867c5aeff48894340"),
    "base_angles_sparse_pitch": (0xB86C5D, "f30f1045000f57c933d20f2ec17a0e750c895018836010fe8b4810eb0c834810018b4810f30f114018"),
    "base_angles_sparse_yaw": (0xB86C86, "f30f1045040f2ec17a0d750b83e1fd89501c894810eb0b83c902894810f30f11401c"),
    "base_angles_sparse_roll": (0xB86CA8, "f30f1045080f2ec17a0d750b83e1fb895020894810eb5a83c904894810f30f114020"),
    "command_base_present_before_finalize": (0xB66F57, "488b7c24304885ff74064883c710eb03488bfa834f1001488b4730"),
    "base_up": (0xB67210, "81481000010000f30f117060"),
    "history_source": (0xD03E10, "4863c3488d144048c1e205490397e00b00003b1f7417413b1e7412488b0586857c018078580074054532c0eb0341b0014c89642428410f28d9498bcdf3440f115c2420e808b9fdff"),
    "history_convert": (0xCDF7C4, "488b4c24780f28def30f10442470450fb6c648894c2428488bd7488bcdf30f11442420e824c50100"),
    "history_angles": (0xCFBDB1, "f30f104710834910018b4110f30f11411883c802f30f104f1489411083c804f30f11491cf30f104718894110f30f114120"),
    "historical_setup": (0xA75854, "f64658187417f30f1086a0010000e829320000f30f11452cf30f1145208b465848a9000600007417f30f1086a4010000e807320000f30f114530f30f114524f30f1086a8010000e8f0310000f30f114534f30f114528"),
    "zero_slice": (0xA66A7D, "430f2e343e7a060f846e01000080bc247001000000f2440f116e20896e28f3440f114e2cf3440f114630f3440f117e34"),
    "analog_accumulation": (0xA66D86, "f3470f584c3e10f3470f58443e14"),
    "digital_positive_unit_load": (0xA66979, "f3440f103502170601"),
    "digital_negative_unit_load": (0xA669FE, "f3440f101db5160601"),
    "digital_positive_unit": (0x1AC8084, "0000803f"),
    "digital_negative_unit": (0x1AC80BC, "000080bf"),
    "ingest_after_integration": (0xA71FEE, "498bd5488bcf488bd8488b07ff90f0000000"),
    "air_vectors": (0x8E663D, "488d4f144c8d4c24604c8d442430488d542440e8fbe6e500"),
    "air_forward_flatten": (0x8E66FE, "c744244800000000"),
    "air_left_flatten": (0x8E670E, "f3440f11642438"),
    "air_forward_normalize": (0x8E6753, "410f28cbf30f5ec8f30f59f9f3440f59c9f30f59cef30f117c2440f3440f114c2444f30f114c2448"),
    "air_left_normalize": (0x8E6811, "410f28cbf30f5ec8f3440f59d1f3440f59c1f3440f59e1f3440f11542430f3440f11442434f3440f11642438"),
    "air_combine": (0x8E688B, "f3440f594540c744245800000000f3440f595540440f28a424a0000000f3410f59fff3450f59cf440f28bc2480000000f3410f58faf3450f58c1"),
}
WINDOWS = {
    "sequence_checked_command_lookup": (0x9452D0, 0x4E, "d07b8ccce4c3721c7409d0ccf3240853b4b7bd64631bd29f32b5b4cffd226839"),
    "controller_command_ring": (0x945360, 0x18A, "353b37d9b850f005f6892ccfb8c764bde82511a5e3ea783088ad1674396b946c"),
    "producer_previous_button_input": (0xB67730, 0x5B, "1b81f65dd55ec85c315331250f3de5a8e6c2d1647991404a86a2ffc6efcb6aa2"),
    # Raw button selects digital versus analog; raw angle deltas are always
    # accumulated. Digital axis dispatch recognizes exact single-button masks.
    "subtick_raw_decoder": (0xA75B23, 0x70, "20dd7ac41dde2e252c5c43a1fe0c387beaeb1011ce4309330c5600b7a3d20a41"),
    "digital_axis_dispatch": (0xA66CEA, 0x6A, "38d1d8ee4be0fc511510e6562b629e4fbf44fa6a4436a30d56b8ea6b53f2b36a"),
    "native_command_clone": (0xB5F150, 0x91, "2d8a6cf1d043f69a72d07efeb0fdc884b17ea0f9f2032e131166ec1cd4d15ef8"),
    "native_reset": (0xB6D7C0, 0x109, "6fa1b94d6899e6171adf1001e7ad8ac193dc9c8dee66103185dece7ddb5a9ac1"),
    "shift_after_reset": (0xB60FE0, 0xF0, "f4e3c686caa6bd4acdc162bf9490afe4220652a64d3963580c76901aa6229d8c"),
    "crc_finalizer": (0xCEE5F0, 0x32E, "011b41d362fa9e2831cc2babc947d4fc7237b98abd4df1921a5411738d5da76e"),
    "history_builder": (0xD03790, 0xC9B, "8bd2bdd444be51cb9eb4641797ad6194e64a01ca56b93ddbfd6ce686868011bb"),
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
    # Decode the named game-rules schema and the existing native rules load.
    registration, fields, count = 0x2214C38, 0x2213F50, 98
    def pointer(at):
        return struct.unpack_from("<Q", image, at)[0] - base
    def string(at):
        if not 0 <= at < len(image):
            raise SystemExit("Invalid game-rules schema string pointer")
        end = image.find(0, at, min(at + 256, len(image)))
        if end < 0:
            raise SystemExit("Unterminated game-rules schema string")
        return bytes(image[at:end]).decode("ascii")
    if (string(pointer(registration)) != "C_CSGameRules" or pointer(registration + 0x28) != fields or
            struct.unpack_from("<H", image, registration + 0x1C)[0] != count):
        raise SystemExit("Game-rules schema registration differs")
    named_fields = {string(pointer(fields + i * 32)): struct.unpack_from("<I", image, fields + i * 32 + 16)[0]
                    for i in range(count)}
    if named_fields.get("m_bFreezePeriod") != 0x40 or image[0x818DD0:0x818DD7] != bytes.fromhex("488b05b1e3d401"):
        raise SystemExit("Native round-freeze contract differs")
    expected = {
        "Input": rip(0xEE574), "InputVtable": rip(0xEE58C),
        "Replay": 0xCEB8E0, "ReplaySlot": 8,
        "InputAngles": struct.unpack_from("<I", image, 0xB6D8A0)[0],
        "InputPreviousButtons": struct.unpack_from("<I", image, 0xB6D84C)[0],
        "InputCurrentButtons": struct.unpack_from("<I", image, 0xB6774D)[0],
        "CommandLookup": 0xB6D7FC + 5 + struct.unpack_from("<i", image, 0xB6D7FD)[0],
        "GameRules": rip(0x818DD0), "FreezePeriod": named_fields["m_bFreezePeriod"],
        "Up": image[0xB6721B], "UpPresent": struct.unpack_from("<I", image, 0xB67213)[0],
    }
    # Independently connect the game-specific vtable to its native base reset.
    replay = struct.unpack_from("<Q", image, expected["InputVtable"] + expected["ReplaySlot"] * 8)[0] - base
    target = 0xCEB8E9 + 5 + struct.unpack_from("<i", image, 0xCEB8EA)[0]
    if replay != expected["Replay"] or target != 0xB6D7C0:
        raise SystemExit("Input reset vtable/call target differs")
    if (struct.unpack_from("<Q", image, expected["InputVtable"] - 8)[0] - base != 0x1F256C8 or
            struct.unpack_from("<I", image, 0x1F256C8 + 12)[0] != 0x232A400 or
            image[0x232A410:0x232A421] != b".?AVCCSGOInput@@\0"):
        raise SystemExit("Input RTTI ownership differs")
    for name, native in expected.items():
        if value(name, "base" if name in ("Up", "UpPresent") else None) != native:
            raise SystemExit(f"Source constant {name} differs from native operand: {native:#x}")
    if struct.unpack_from("<I", image, 0xB6D8A6)[0] != expected["InputAngles"] + 8:
        raise SystemExit("Input angle roll store no longer follows pitch/yaw")
    if struct.unpack_from("<I", image, 0xB67767)[0] != expected["InputPreviousButtons"]:
        raise SystemExit("Producer previous-button input differs from replay destination")
    prefix = re.search(r"ReplayBytes\[\]\s*\{([^}]*)\}", source, re.S)
    parsed = bytes(int(x.strip(), 0) for x in prefix[1].split(",") if x.strip()) if prefix else b""
    if parsed != bytes.fromhex(CHECKS["replay_wrapper"][1])[:23]:
        raise SystemExit("ReplayBytes differs from independently frozen entry")
    lookup_prefix = re.search(r"CommandLookupBytes\[\]\s*\{([^}]*)\}", source, re.S)
    parsed_lookup = bytes(int(x.strip(), 0) for x in lookup_prefix[1].split(",") if x.strip()) if lookup_prefix else b""
    if parsed_lookup != bytes.fromhex(CHECKS["command_lookup_entry"][1]):
        raise SystemExit("CommandLookupBytes differs from independently decoded entry")
    for name, record, string, text in (
            ("Attack", 0x1B2ADB0, 0x1B2B018, b"IN_ATTACK\0"),
            ("Use", 0x1B2AE50, 0x1B2B050, b"IN_USE\0"),
            ("Attack2", 0x1B2AEF0, 0x1B2B098, b"IN_ATTACK2\0")):
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
    if struct.unpack_from("<I", raw, nt + 8)[0] != 0x6ADA7340 or len(image) != 0x29A0000:
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
