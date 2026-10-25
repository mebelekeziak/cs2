"""Verify the installed named mat_fullbright producer and runtime hook contract.

File-only: does not launch/open the client or change installed files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from inspect_world import load_image

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game")
EXPECTED_ENGINE = "e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29"
EXPECTED_CLIENT = "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--header", type=Path, default=ROOT / "include/world_fullbright_offsets.h")
    args = parser.parse_args()
    path = args.game / "bin/win64/engine2.dll"
    raw, image, _, nt, _ = load_image(path)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_ENGINE:
        raise SystemExit("Unknown engine2.dll: repeat fullbright investigation")
    for label, start, end, expected in [
        ("registration", 0x19DD0, 0x19E8B, "7eff03b01f4fef687b2ec40f2ea39ca5f748958f8361778e5e7c32463a064991"),
        ("integer_setter", 0x2485A0, 0x2487BA, "8f8617527ecb38e791a300c48be1e6385eb22e25de054c5fa76032962d8eb14b"),
        ("fullbright_producer", 0x24331E, 0x243420, "84c8ff8267a11167ae1a5eb81e0b8bccdee8faac65c330f627cb228425d63b93"),
    ]:
        if hashlib.sha256(image[start:end]).hexdigest() != expected:
            raise SystemExit(f"Changed {label} instruction scope")
    rip = lambda at: at + 7 + struct.unpack_from("<i", image, at + 3)[0]
    name = rip(0x19E64)
    if image[name:name + 15] != b"mat_fullbright\0":
        raise SystemExit("Fullbright registration name differs")
    wrapper = rip(0x19E6B)
    if rip(0x24331E) != wrapper + 8:
        raise SystemExit("Fullbright producer does not read named cvar storage")
    # Native storage int +0x58 is copied through RBP+0x1158 to the exact
    # SetInt R8D argument; a native diagnostic branch may choose 1 instead.
    if image[0x243333:0x24333C] != bytes.fromhex("8b4058898558110000") or \
            image[0x24340C:0x243413] != bytes.fromhex("448b8558110000"):
        raise SystemExit("Fullbright producer value/stack member changed")
    setter = 0x24341B + 5 + struct.unpack_from("<i", image, 0x24341C)[0]
    key = struct.unpack_from("<I", image, 0x243414)[0]
    source = args.header.read_text()
    def constant(name):
        match = re.search(r"\b" + name + r"\s*=\s*(0x[0-9a-fA-F]+)", source)
        if not match:
            raise SystemExit(f"Missing header constant {name}")
        return int(match.group(1), 16)
    for label, value in {
        "Timestamp": struct.unpack_from("<I", raw, nt + 8)[0], "ImageSize": len(image),
        "Setter": setter, "SetterReturn": 0x24341B + 5, "Producer": 0x24340C,
        "CvarRead": 0x24331E, "Registration": 0x19E64, "FullbrightKey": key,
    }.items():
        if constant(label) != value:
            raise SystemExit(f"{label}: header differs from independently decoded installed operand")
    for label, site in {"SetterBytes": setter, "ProducerBytes": 0x24340C,
                        "CvarReadBytes": 0x24331E, "RegistrationBytes": 0x19E64}.items():
        match = re.search(r"\b" + label + r"\[\]\s*\{([^}]+)\}", source)
        if not match:
            raise SystemExit(f"Missing header fingerprint {label}")
        expected = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))
        if not expected or image[site:site + len(expected)] != expected:
            raise SystemExit(f"{label}: runtime fingerprint differs from installed file")
    client_path = args.game / "csgo/bin/win64/client.dll"
    client_raw, client_image, _, client_nt, _ = load_image(client_path)
    if hashlib.sha256(client_raw).hexdigest() != EXPECTED_CLIENT:
        raise SystemExit("Unknown client.dll: repeat fullbright investigation")
    for label, start, end, expected in [
        ("client_integer_setter", 0x16A9D0, 0x16ABDE, "d66a6b5ac1b7cea31df8a3cf7b7dd3f80b21fea7705adc399a5548366f13d6bd"),
        ("client_producer", 0xE5AFC8, 0xE5B0E4, "5ef307fabf07dedf161bdd4a621acdbab79ebf47e0f492c4fd6cfb2df8233a2e"),
        ("client_registration", 0x108830, 0x10885D, "bf29ddbb388c9315fa4f5d7e1f575403710a07b2226fa9b553e5a480ca132295"),
        ("client_aux_producer", 0x7996FA, 0x799718, "61b10833769d896ff8c80c2ecd11699a2a04880223ecef81bd143568c38380c7"),
        ("client_aux_registration", 0x92CD0, 0x92CFD, "ab98f3691d8c8d4ec3285e19c1626bc8faca1c0694489f485182c7f05db7f727"),
        ("client_render_mode_consumer", 0x12034B7, 0x12038B0, "41ad4c220ee5d9969288f21b6f041035db55a369e643a1f24de815c3524d58d1"),
    ]:
        if hashlib.sha256(client_image[start:end]).hexdigest() != expected:
            raise SystemExit(f"Changed {label} instruction scope")
    client_rip = lambda at: at + 7 + struct.unpack_from("<i", client_image, at + 3)[0]
    for registration, read in [(0x10883A, 0xE5AFC8), (0x92CDA, 0x7996FA)]:
        name = client_rip(registration)
        if client_image[name:name + 15] != b"mat_fullbright\0" or client_rip(registration + 7) + 8 != client_rip(read):
            raise SystemExit("Client producer does not read independently named mat_fullbright storage")
    if client_image[0xE5AFEE:0xE5AFF7] != bytes.fromhex("8b4058898500170000") or \
            client_image[0xE5B0CC:0xE5B0D3] != bytes.fromhex("448b8500170000"):
        raise SystemExit("Client fullbright local value chain changed")
    for call in [0xE5B0DF, 0x799713]:
        if call + 5 + struct.unpack_from("<i", client_image, call + 1)[0] != 0x16A9D0:
            raise SystemExit("Client fullbright setter target changed")
    if struct.unpack_from("<I", client_image, 0xE5B0DB)[0] != key or struct.unpack_from("<I", client_image, 0x799709)[0] != key:
        raise SystemExit("Client producer uses a different fullbright key")
    for label, value in {
        "ClientTimestamp": struct.unpack_from("<I", client_raw, client_nt + 8)[0], "ClientImageSize": len(client_image),
        "ClientSetter": 0x16A9D0, "ClientSetterReturn": 0xE5B0E4, "ClientAuxReturn": 0x799718,
        "ClientProducer": 0xE5B0CC, "ClientCvarRead": 0xE5AFC8, "ClientRegistration": 0x10883A,
        "ClientAuxProducer": 0x7996FA, "ClientAuxRegistration": 0x92CDA,
    }.items():
        if constant(label) != value:
            raise SystemExit(f"{label}: header differs from independently decoded installed operand")
    for label, site in {"ClientSetterBytes": 0x16A9D0, "ClientProducerBytes": 0xE5B0CC,
                        "ClientCvarReadBytes": 0xE5AFC8, "ClientRegistrationBytes": 0x10883A,
                        "ClientAuxProducerBytes": 0x7996FA, "ClientAuxRegistrationBytes": 0x92CDA}.items():
        match = re.search(r"\b" + label + r"\[\]\s*\{([^}]+)\}", source)
        if not match:
            raise SystemExit(f"Missing header fingerprint {label}")
        expected = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))
        if not expected or client_image[site:site + len(expected)] != expected:
            raise SystemExit(f"{label}: runtime fingerprint differs from installed file")
    print(json.dumps({"path": str(path), "sha256": digest, "named_cvar": "mat_fullbright",
                      "wrapper": hex(wrapper), "setter": hex(setter), "caller_return": "0x243420",
                      "client_setter": "0x16a9d0", "client_callers": ["0xe5b0e4", "0x799718"],
                      "client_render_mode_consumer": "0x12035e6 -> 0x12037ad",
                      "key": hex(key), "override_value": 1, "persistent_cvar_writes": False,
                      "live_observation": False}, indent=2))


if __name__ == "__main__":
    main()
