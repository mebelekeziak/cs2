"""Verify local native player-model loading, setter and frame-owner contracts.

Installed files only. Never loads CS2, modifies assets, scans process memory or
substitutes discovered values into source. Python standard library only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from verify_sky_draw import mapped, primary_vtable

ROOT = Path(__file__).resolve().parents[1]
GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game")
IDENTITIES = {
    "client": ("csgo/bin/win64/client.dll", "5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49"),
    "engine": ("bin/win64/engine2.dll", "6456df18d8f954394024c2c07da8c848df15a5ace74ce7390d78970ceb39ee21"),
    "host": ("csgo/bin/win64/host.dll", "56efa66a333edb347f044c9bcbc5236bd7d006e478623b3364e5d0a7d681b9cc"),
    "resource": ("bin/win64/resourcesystem.dll", "0004439e37b6ffd5a37683f9f288726584be2a3b4ccc6674af8303cde0343027"),
    "tier0": ("bin/win64/tier0.dll", "990ea56bf1626d299ba54b64b423f2e409bd592bb737c7a5edf7972ad5c7143e"),
}
WINDOWS = {
    "client": {
        "set_model": (0x9643C0, 0x31, "4c9a23388e27ba12b6cc41631398993366ee636ac68b8d8796081dbbadfde9d5"),
        "set_model_binding": (0x964400, 0x194, "6f9a4ed46a4ca0d7420f53f5ebcd376441ccd6f144ef39150dc410b78c3dee7f"),
        "set_model_state": (0xAC35A0, 0x167, "f3e66b85b8b74f30b461d820a3f9987d11ef4e46c8e5825bae84f37f8b41ad1d"),
        "model_name_getter": (0x17762B0, 0x13, "0f9397ebb412e95b02480ce6736d8c2126d22a244889cbd08d2edb9077b274e2"),
        "entity_model_getter": (0x21C840, 0x71, "8b13d562a2e45309600be262a42c29bfd80adad4bb4d56b944869d9093a464bf"),
        "frame_stage": (0xB6D700, 0x5F0, "d50a5ef8cbe95d7e35402543784f3c79e0f420c06d5c3ebb709a12d71d4022f1"),
        "model_interface_binding": (0xB65115, 0x22, "0fdd45e86c89bc00c05ed89597101bb12240c806957d523d02a9728aa56d15f7"),
        "native_model_name_consumer": (0xAC3741, 0x1E, "6c9e70280c7c621cdd7683421e3f0fb4ebb6cb67d31e7774ea225724564771f0"),
    },
    "engine": {"network_stage_producers": (0x47E2F, 0x514, "4520217d4a0ba54a77eb60188f553e2dfae5b0e13604cf553e1c14300a6b7165")},
    "host": {
        "native_model_resolver": (0x16560, 0xBF, "dfd9fb0e1773eb52ccb5cd45bc645ee17a8bd77fc35f6bfd32a1946372d9aed5"),
        "resource_name_initializer": (0xC4380, 0x11B, "5be7ea1ea87172092e8dc7f1c39f6bb2be2adc18c1da532b957cdca1e0dc7c26"),
    },
    "resource": {
        "find_or_register": (0x16540, 0x432, "673c0672345c2852e3327d02bbfc42126c94619a977fa229acb23cc1ef7f28ad"),
        "blocking_jit_loader": (0x16B20, 0x44B, "8b507d490197796d1272460b637c185710945c6aa9c643d8f2520527157447c4"),
        "manifest_submission": (0x1AA80, 0x186, "2fc1e143f491959ec5f0f301e6e4c7170cf596f45303322344d6391acc93dff7"),
    },
    "tier0": {"name_purge": (0x53D60, 0xC0, "833ba31d5e534790fc3645939e7057685ea53ee3dbaa2d9a63e74e08225e6899")},
}


def require(condition, reason):
    if not condition:
        raise SystemExit(reason)


def cstring(image, rva):
    require(0 <= rva < len(image), "Invalid string RVA")
    end = image.find(0, rva, min(len(image), rva + 512))
    require(end >= rva, "Unterminated native string")
    return bytes(image[rva:end]).decode("ascii")


def schema(image, base, registration, fields, count, name):
    pointer = lambda at: struct.unpack_from("<Q", image, at)[0] - base
    require(cstring(image, pointer(registration)) == name, f"Changed {name} registration")
    require(pointer(registration + 0x28) == fields, f"Changed {name} field array")
    require(struct.unpack_from("<H", image, registration + 0x1C)[0] == count, f"Changed {name} field count")
    return {cstring(image, pointer(fields + i * 32)): struct.unpack_from("<I", image, fields + i * 32 + 16)[0]
            for i in range(count)}


def exported(image, raw, nt, name):
    directory, size = struct.unpack_from("<II", raw, nt + 24 + 112)
    count = struct.unpack_from("<I", image, directory + 24)[0]
    functions, names, ordinals = struct.unpack_from("<III", image, directory + 28)
    hits = []
    for index in range(count):
        string = struct.unpack_from("<I", image, names + index * 4)[0]
        if cstring(image, string) == name:
            ordinal = struct.unpack_from("<H", image, ordinals + index * 2)[0]
            hits.append(struct.unpack_from("<I", image, functions + ordinal * 4)[0])
    require(len(hits) == 1 and not directory <= hits[0] < directory + size, "Changed native named export")
    return hits[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=GAME)
    parser.add_argument("--header", type=Path, default=ROOT / "include/model_swap_offsets.h")
    args = parser.parse_args()
    modules, reports = {}, {}
    for key, (relative, expected_hash) in IDENTITIES.items():
        path = args.game / relative
        raw, image, base, nt = mapped(path)
        digest = hashlib.sha256(raw).hexdigest()
        require(digest == expected_hash, f"Changed installed {key}: repeat model-swap investigation")
        modules[key] = (raw, image, base, nt)
        reports[key] = {"path": str(path), "sha256": digest,
                        "timestamp": hex(struct.unpack_from("<I", raw, nt + 8)[0]),
                        "image_size": hex(len(image))}
        for label, (rva, size, expected) in WINDOWS.get(key, {}).items():
            require(hashlib.sha256(image[rva:rva + size]).hexdigest() == expected, f"Changed native window: {label}")
    _, client, cb, _ = modules["client"]
    _, engine, eb, _ = modules["engine"]
    _, host, hb, _ = modules["host"]
    _, resource, rb, _ = modules["resource"]
    tier_raw, tier0, _, tier_nt = modules["tier0"]
    u32 = lambda image, at: struct.unpack_from("<I", image, at)[0]
    pointer = lambda image, base, at: struct.unpack_from("<Q", image, at)[0] - base
    rip = lambda image, at: at + 7 + struct.unpack_from("<i", image, at + 3)[0]
    call = lambda image, at: at + 5 + struct.unpack_from("<i", image, at + 1)[0]
    table = primary_vtable(client, cb, b".?AVCSource2Client@@")
    require(table == 0x1C630F0 and pointer(client, cb, table + 36 * 8) == 0xB6D700, "Changed native frame callback RTTI/slot")
    require(primary_vtable(host, hb, b".?AVCGameModelInfo@@") == 0x11D800, "Changed GameModelInfo RTTI")
    require(pointer(host, hb, 0x11D800 + 0x60) == 0x16560, "Changed model resolver slot")
    require(primary_vtable(resource, rb, b".?AVCResourceSystem@@") == 0x65348, "Changed resource-system RTTI")
    require(pointer(resource, rb, 0x65348 + 0x148) == 0x16B20, "Changed blocking-load vtable slot")
    require(pointer(resource, rb, 0x65348 + 0x278) == 0x16540, "Changed find/register resource slot")
    require(cstring(client, rip(client, 0xB65124)) == "GameModelInfo001", "Changed model interface name")
    require(rip(client, 0xB6512D) == rip(client, 0x9643CC) == 0x2560EB0, "Model setter/interface binding disagree")
    require(client[0x9643DB:0x9643DE] == bytes.fromhex("ff5060"), "Changed model resolution ABI")
    require(call(client, 0x9643E6) == 0x964400, "Changed model setter binding call")
    skeleton = schema(client, cb, 0x21FF428, 0x21FF300, 7, "CSkeletonInstance")
    state = schema(client, cb, 0x21FE008, 0x21FDE40, 14, "CModelState")
    binding = skeleton["m_modelState"] + state["m_hModel"]
    require(binding == u32(client, 0x21C894) == 0x1E0, "Model binding schema/getter disagree")
    require(client[0x21C89D:0x21C8A0] == bytes.fromhex("488b18"), "Changed resource-binding indirection")
    require(call(client, 0xAC374E) == 0x17762B0 and client[0x17762B3] == 8, "Changed native model-name getter")
    require(u32(client, 0xAC35C5) == state["m_hModel"], "Changed native strong-handle member")
    # Native setter increments/decrements the binding count; the project must not
    # borrow and later manually decrement this engine-owned reference.
    require(client[0xAC35BE:0xAC35C2] == bytes.fromhex("f0ff4220"), "Changed native handle acquisition")
    require(client[0xAC35DC:0xAC35E0] == bytes.fromhex("f0ff4a20"), "Changed native handle release")
    for stage, at in ((4, 0x47E2F), (5, 0x47FC7), (6, 0x481AB), (7, 0x48239), (8, 0x4832E)):
        require(rip(engine, at) == 0x6969D8 and engine[at + 7] == 0xBA and u32(engine, at + 8) == stage,
                f"Changed native stage-{stage} producer")
        require(engine[at + 15:at + 21] == bytes.fromhex("ff9020010000"), "Changed native frame dispatch slot")
    require(u32(client, 0xB6DCC8 + (8 - 3) * 4) == 0xB6DB2F, "Changed stage-8 native branch")
    require(u32(client, 0xB6DCC8 + (10 - 3) * 4) == 0xB6D884, "Changed later worker-stage branch")
    require(rip(host, 0x165B9) == 0x1445E0, "Changed host resource-system pointer")
    require(call(host, 0x165AC) == 0xC4380, "Changed native resource-name initializer")
    require(u32(host, 0x1658B) == 0xC00000C8, "Changed native resource-name stack flags")
    require(u32(host, 0x165C3) == 0x6C646D76, "Changed native model resource type")
    require(host[0x165DD:0x165E9] == bytes.fromhex("41b001488bceff9078020000"), "Changed find/register call")
    require(u32(host, 0xC445F) == 0xD0 and u32(host, 0xC448A) == 0xD8, "Changed resource hash/type members")
    require(exported(tier0, tier_raw, tier_nt, "?Purge@CBufferString@@QEAAXH@Z") == 0x53D60, "Changed resource-name cleanup export")
    # JIT loader submits one path to a blocking native manifest and retains or
    # releases that manifest using the native ownership path.
    require(resource[0x16BC4:0x16BCC] == bytes.fromhex("c744244001000000"), "Changed single-resource manifest count")
    require(resource[0x16BFA:0x16C00] == bytes.fromhex("ff90b0000000"), "Changed native manifest submission")
    require(resource[0x16F38:0x16F3E] == bytes.fromhex("ff90c8000000"), "Changed native manifest release")
    require(resource[0x16F4A:0x16F50] == bytes.fromhex("ff9078020000"), "Changed loaded binding lookup")
    require(cstring(resource, rip(resource, 0x16CD4)) == "CResourceSystem::BlockingLoadResourceByNameIntoJustInTimeManifest",
            "Changed blocking-load diagnostic identity")
    expected = {
        "FrameStage": 0xB6D700, "FrameVtable": table, "FrameSlot": 36, "ApplyStage": 8,
        "SetModel": 0x9643C0, "SetModelBinding": 0x964400, "ModelInterface": 0x2560EB0,
        "ModelResolveSlot": 0x60, "ModelNameGetter": 0x17762B0, "ModelBinding": binding,
        "ResourceName": client[0x17762B3], "HostTimestamp": int(reports["host"]["timestamp"], 0),
        "HostImageSize": len(host), "ResourceSystemTimestamp": int(reports["resource"]["timestamp"], 0),
        "ResourceSystemImageSize": len(resource), "Tier0Timestamp": int(reports["tier0"]["timestamp"], 0),
        "Tier0ImageSize": len(tier0), "EngineTimestamp": int(reports["engine"]["timestamp"], 0),
        "EngineImageSize": len(engine), "InitializeResourceName": 0xC4380, "HostModelResolver": 0x16560,
        "HostModelVtable": 0x11D800, "ResourceSystemPointer": 0x1445E0, "ResourceSystemVtable": 0x65348,
        "BlockingLoad": 0x16B20, "BlockingLoadSlot": 0x148, "ResourceNameSize": 0xE0,
        "ResourceNameFlagsOffset": 4, "ResourceNameFlags": 0xC00000C8, "PurgeResourceName": 0x53D60,
        "ResourceHash": 0xD0, "ResourceType": 0xD8, "ModelResourceType": 0x6C646D76,
    }
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", args.header.read_text(encoding="utf-8-sig"), flags=re.S)
    for name, value in expected.items():
        matches = re.findall(rf"\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*;", text)
        require(len(matches) == 1 and int(matches[0], 0) == value, f"Header mismatch: {name}; native {value:#x}")
    for name, image, at, size in (
        ("FrameStageBytes", client, 0xB6D700, 29), ("SetModelBytes", client, 0x9643C0, 0x31),
        ("ModelNameGetterBytes", client, 0x17762B0, 0x13),
        ("InitializeResourceNameBytes", host, 0xC4380, 0x46),
        ("BlockingLoadBytes", resource, 0x16B20, 0x22),
        ("HostModelResolverBytes", host, 0x16560, 0x48),
        ("PurgeResourceNameBytes", tier0, 0x53D60, 0x1E),
    ):
        match = re.search(rf"\b{name}\[\]\s*\{{([^}}]*)\}}", text, re.S)
        actual = bytes(int(value.strip(), 0) for value in match[1].split(",") if value.strip()) if match else b""
        require(actual == bytes(image[at:at + size]), f"Header fingerprint mismatch: {name}")
    print(json.dumps({"component": "model_swap", "modules": reports, "verified_header": str(args.header),
                      "contracts": {name: hex(value) for name, value in expected.items()},
                      "live_observation": False, "preload": "native just-in-time blocking manifest",
                      "model_getter": "skeleton+0x1e0 -> binding -> CModel+0x08 -> copied path",
                      "apply": "after native stage 8 returns; engine network-update owner",
                      "windows": {key: {name: {"rva": hex(at), "size": hex(size), "sha256": digest}
                                         for name, (at, size, digest) in values.items()}
                                  for key, values in WINDOWS.items()}}, indent=2))


if __name__ == "__main__":
    main()
