"""Verify the installed native Fullbright source recipe and context contract.

File-only: does not launch/open the client or change installed files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from inspect_world import load_image
from verify_sky_draw import primary_vtable

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game")
EXPECTED_ENGINE = "e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29"
EXPECTED_CLIENT = "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"
EXPECTED_SCENE = "0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b"


def mode_token(name):
    """Native lowercased Murmur2 token, seed independently present in ctors."""
    data = name.lower()
    value = 0x31415926 ^ len(data)
    while len(data) >= 4:
        word = int.from_bytes(data[:4], "little") * 0x5BD1E995 & 0xFFFFFFFF
        word ^= word >> 24
        value = ((value * 0x5BD1E995) ^ (word * 0x5BD1E995)) & 0xFFFFFFFF
        data = data[4:]
    if data:
        value = ((value ^ int.from_bytes(data, "little")) * 0x5BD1E995) & 0xFFFFFFFF
    value ^= value >> 13
    value = value * 0x5BD1E995 & 0xFFFFFFFF
    return value ^ (value >> 15)


def verify_selector(args, source, constant, client, client_base):
    scene_path = args.game / "bin/win64/scenesystem.dll"
    scene_raw, scene, scene_base, scene_nt, _ = load_image(scene_path)
    if hashlib.sha256(scene_raw).hexdigest() != EXPECTED_SCENE:
        raise SystemExit("Unknown scenesystem.dll: repeat Fullbright context investigation")
    scopes = [
        (client, "selector", 0x1202FE0, 0x12038C5, "715ce557237895431119043e8e216ab9ee96c29dc7073393daa0fa3ffee11b21"),
        (client, "view_initializer", 0x1208EA0, 0x120944F, "1a98e359940f5f92ab1bc0d5a8f1e6049af7fc80a93faf2693b5955e6413d98f"),
        (client, "tools_descriptor_ctor", 0x128FF0, 0x1290F4, "68956c7e108c1b558c908b35b24a9696fa574ce46de73b2269602a3dfdcabb35"),
        (client, "default_descriptor_ctor", 0x128620, 0x128742, "ba35687dd3b33a5324b3b2e976a1006c17559271b4f4b5101f6cb27e12994da7"),
        (scene, "pure_context_accessor", 0x11CD70, 0x11CD78, "2eecb7c42160c9b8a3702d7b72a6775f2c346aadb4395084c76f3122c254f82b"),
    ]
    for image, label, begin, end, digest in scopes:
        if hashlib.sha256(image[begin:end]).hexdigest() != digest:
            raise SystemExit(f"Changed {label} instruction scope")
    rip = lambda at: at + 7 + struct.unpack_from("<i", client, at + 3)[0]
    call = lambda at: at + 5 + struct.unpack_from("<i", client, at + 1)[0]
    tools_name, default_name = rip(0x129004), rip(0x128634)
    if client[tools_name:tools_name + 9] != b"ToolsVis\0" or client[default_name:default_name + 12] != b"CsgoForward\0":
        raise SystemExit("Native rendering descriptor names differ")
    tools = 0x1290E1 + 6 + struct.unpack_from("<i", client, 0x1290E3)[0]
    default = 0x12872F + 6 + struct.unpack_from("<i", client, 0x128731)[0]
    if rip(0x12900D) != tools + 8 or rip(0x12863D) != default + 8 or rip(0x1203797) != tools or rip(0x120367D) != default:
        raise SystemExit("Descriptor constructor and selector references disagree")
    vtable = primary_vtable(scene, scene_base, b".?AVCSceneView@@")
    slot = struct.unpack_from("<I", client, 0x120300D)[0]
    accessor = struct.unpack_from("<Q", scene, vtable + slot)[0] - scene_base
    if scene[accessor:accessor + 3] != bytes.fromhex("488d81") or scene[accessor + 7] != 0xC3:
        raise SystemExit("CSceneView context accessor is not a pure inline pointer return")
    values = {
        "Selector": call(0x12090A4), "SelectorCaller": 0x1209094, "SelectorReturn": 0x12090A4 + 5,
        "ToolsDescriptor": tools, "DefaultDescriptor": default,
        "ToolsToken": mode_token(client[tools_name:tools_name + 8]),
        "DefaultToken": mode_token(client[default_name:default_name + 11]),
        "SceneViewVtable": vtable, "SceneContextAccessorSlot": slot, "SceneContextAccessor": accessor,
        "SceneContext": struct.unpack_from("<I", scene, accessor + 3)[0],
        "IntegerCollection": struct.unpack_from("<I", client, 0x1209119)[0],
        "ToolsModeKey": struct.unpack_from("<I", client, 0x1209127)[0],
        "ToolsModeValue": 1, "NativeDefaultMode": 0,
        "SceneTimestamp": struct.unpack_from("<I", scene_raw, scene_nt + 8)[0], "SceneImageSize": len(scene),
        "ContextSetter": call(0x12037AD),
    }
    if client[0x120911D:0x1209120] != bytes.fromhex("4533c0") or \
            client[0x1203858:0x120385B] != bytes.fromhex("83fe01") or \
            struct.unpack_from("<I", client, 0x12037A9)[0] != values["ToolsModeKey"]:
        raise SystemExit("Native default fallback / ToolsVis mode1 output operands differ")
    for label, expected in values.items():
        if constant(label) != expected:
            raise SystemExit(f"{label}: header differs from independently decoded installed operand")
    for label, image, site, size in [
        ("SelectorBytes", client, values["Selector"], 24),
        ("SelectorCallerBytes", client, values["SelectorCaller"], 21),
        ("SceneContextAccessorBytes", scene, accessor, 8),
        ("ContextSetterBytes", client, values["ContextSetter"], 27),
    ]:
        match = re.search(r"\b" + label + r"\[\]\s*\{([^}]+)\}", source)
        expected = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1))) if match else b""
        if len(expected) != size or image[site:site + size] != expected:
            raise SystemExit(f"{label}: complete runtime fingerprint differs from installed file")
    return {"selector": hex(values["Selector"]), "caller_return": hex(values["SelectorReturn"]),
            "tools_material_mode": "ToolsVis", "tools_token": hex(values["ToolsToken"]),
            "default_material_mode": "CsgoForward", "default_token": hex(values["DefaultToken"]),
            "context_owner": "CSceneView", "context_accessor": hex(accessor),
            "parameter": "g_nToolsVisMode", "key": hex(values["ToolsModeKey"]),
            "enabled_value": 1, "native_default_fallback": 0,
            "scene_sha256": EXPECTED_SCENE, "persistent_context_reset_proved": False,
            "status": "historical failed override; retained for investigation only"}


def verify_source_recipe(source, constant, client):
    """Decode the native mat_fullbright=1 producer, not a debug-mode override."""
    for label, begin, end, digest in [
        ("complete_source_owner", 0xE599E0, 0xE5B4AB, "3d6b80e81fa915c2bcd531116433b8d3254f3208b78f595ec1ba9489eb8ba5c4"),
        ("float4_setter", 0x16AE00, 0x16B00D, "74ee1d38ef2bcb7bc24ed18db7724a00b93d24c31a3965b329918e555de10dc9"),
        ("ambient_source_to_view", 0x1218720, 0x1218AC6, "8ce1d36f6de4d86899de1974eca5af81a97fa212c109e2dadb16674c07e74198"),
    ]:
        if hashlib.sha256(client[begin:end]).hexdigest() != digest:
            raise SystemExit(f"Changed {label} instruction scope")
    u32 = lambda at: struct.unpack_from("<I", client, at)[0]
    rip = lambda at, offset, size: at + size + struct.unpack_from("<i", client, at + offset)[0]
    call = lambda at: rip(at, 1, 5)
    ambient, direct, indirect = (mode_token(name) for name in
                                  (b"AmbientColor", b"DirectLighting", b"IndirectLighting"))
    if (ambient, direct, indirect, mode_token(b"ToolsVisMode")) != (0xFF90C40E, 0x1F900C45, 0xDB409112, 0xC6767B23):
        raise SystemExit("Native source key name hashes differ")
    if u32(0xE5AFE4) != ambient or u32(0xE5B08E) != ambient or \
            u32(0xE5B01C) != direct or u32(0xE5B0AF) != direct or \
            u32(0xE5B062) != indirect or u32(0xE5B0BC) != indirect:
        raise SystemExit("Native source recipe/reset keys differ")
    values = {
        "SourceRootToIntegers": u32(0xE5B0D6) - u32(0xE5B09B),
        "SourceFloat4Setter": call(0xE5B09F), "SourceRecipe": 0xE5B080,
        "SourceReset": 0xE5AFD4, "SourceLightingReset": 0xE5B007,
        "AmbientKey": ambient, "DirectKey": direct, "IndirectKey": indirect,
    }
    for label, value in values.items():
        if constant(label) != value:
            raise SystemExit(f"{label}: header differs from independently decoded installed operand")
    for label, site, size in [("SourceFloat4SetterBytes", values["SourceFloat4Setter"], 27),
                              ("SourceRecipeBytes", values["SourceRecipe"], 0x4C),
                              ("SourceResetBytes", values["SourceReset"], 0x28),
                              ("SourceLightingResetBytes", values["SourceLightingReset"], 0x3C)]:
        match = re.search(r"\b" + label + r"\[\]\s*\{([^}]+)\}", source)
        expected = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1))) if match else b""
        if len(expected) != size or client[site:site + size] != expected:
            raise SystemExit(f"{label}: complete runtime fingerprint differs from installed file")
    white = rip(0xE5B080, 4, 8)
    default = rip(0xE5AFD4, 4, 8)
    if struct.unpack_from("<4f", client, white) != (1., 1., 1., 1.) or \
            struct.unpack_from("<4f", client, default) != (0., 0., 0., 1.):
        raise SystemExit("Native fullbright/default ambient float4 constants differ")
    if client[0xE5B002:0xE5B007] != bytes.fromhex("83e9017479") or \
            client[0xE5A9D8:0xE5A9DA] != bytes.fromhex("33ff") or \
            client[0xE5B0A4:0xE5B0A7] != bytes.fromhex("4533c0") or \
            client[0xE5B0B8:0xE5B0BB] != bytes.fromhex("448bc7"):
        raise SystemExit("Native fullbright1 branch or zero lighting values differ")
    if call(0xE5AFF7) != values["SourceFloat4Setter"] or \
            any(call(at) != constant("ClientSetter") for at in (0xE5B02A, 0xE5B04B, 0xE5B06E, 0xE5B0B3, 0xE5B0C7, 0xE5B0DF)):
        raise SystemExit("Source reset/recipe setter ABI targets differ")
    if client[0xE5B022:0xE5B02A] != bytes.fromhex("40387858410f95c0") or \
            client[0xE5B039:0xE5B041] != bytes.fromhex("40387858410f95c0"):
        raise SystemExit("Ordinary direct/indirect native cvar initialization differs")
    # The same stack root is cleared, initialized and destroyed within one
    # native call. These operands are frozen inside complete_source_owner.
    if u32(0xE5A4E6) != 0x5B0 or u32(0xE5A4EE) != 0x1F8 or \
            u32(0xE5A7E9) != 0x5B0 or u32(0xE5B47A) != 0x5B0 or \
            call(0xE5A7ED) != 0x15AC60 or call(0xE5B47E) != 0x15BE40:
        raise SystemExit("Source stack context construction/destruction differs")
    if rip(0x1218755, 4, 8) != 0x1D30A80 or \
            struct.unpack_from("<4I", client, 0x1D30A80) != (ambient,) * 4 or \
            u32(0x121882F) != 0x8310AEF1 or call(0x1218836) != values["SourceFloat4Setter"] or \
            call(0x12092BA) != 0x1218720:
        raise SystemExit("Native source ambient to view shader context bridge differs")
    return {"hook": hex(constant("ClientSetter")), "admitted_return": hex(constant("ClientSetterReturn")),
            "marker_key": "ToolsVisMode / 0xc6767b23", "marker_value": "forward unchanged",
            "source_root": "current integer collection - 0x110; native stack RBP+0x5b0",
            "ambient": {"key": hex(ambient), "enabled": [1, 1, 1, 1], "native_reset": [0, 0, 0, 1]},
            "direct": {"key": hex(direct), "enabled": 0}, "indirect": {"key": hex(indirect), "enabled": 0},
            "preexisting_keys": True, "native_selector_preserved": True,
            "disable_stop": "forward native freshly initialized source; no cached view or sticky output normalization"}


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
    client_raw, client_image, client_base, client_nt, _ = load_image(client_path)
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
    source_contract = verify_source_recipe(source, constant, client_image)
    selector_contract = verify_selector(args, source, constant, client_image, client_base)
    print(json.dumps({"path": str(path), "sha256": digest, "named_cvar": "mat_fullbright",
                      "wrapper": hex(wrapper), "setter": hex(setter), "caller_return": "0x243420",
                      "client_setter": "0x16a9d0", "client_callers": ["0xe5b0e4", "0x799718"],
                      "client_render_mode_consumer": "0x12035e6 -> 0x12037ad",
                      "key": hex(key), "marker_value_forwarded_unchanged": True, "persistent_cvar_writes": False,
                      "source_recipe_contract": source_contract, "historical_selector_contract": selector_contract, "live_observation": False}, indent=2))


if __name__ == "__main__":
    main()
