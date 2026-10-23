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
    "client": ("csgo/bin/win64/client.dll", "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"),
    "engine": ("bin/win64/engine2.dll", "e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29"),
    "host": ("csgo/bin/win64/host.dll", "f7db2685acbba12c27b3237aa0efa3f17a1698b0055adb0b950f0ee24519783c"),
    "resource": ("bin/win64/resourcesystem.dll", "29d503822278305b197c5d4c299b77961f6c5b9c0ab989dae81723c8546fa75f"),
    "tier0": ("bin/win64/tier0.dll", "f96f4f9666be66ee3dd42a015b305d954b5dd5582b0608a74e50548df426584d"),
}
WINDOWS = {
    "client": {
        "set_model": (0x9651f0, 0x31, "c16e8e2bcc48e29596ddf55db590ae9d01eab7707b4ea6be2d1ea9e4d3e468d1"),
        "set_model_binding": (0x965230, 0x194, "b1e560bc9c5c89c2017609120d69206743394dd4e7843f570356baa3b4ad4857"),
        "set_model_state": (0xac43e0, 0x167, "6673e97420e17d447465c72da41e2790076c2c90ca1deb4496f9346a8452a11f"),
        "model_name_getter": (0x1778510, 0x13, "831f8f6ce52a8aed6c6bc78627787822ea8f05b9c9b0eb0b0b405e175d5f49a1"),
        "entity_model_getter": (0x21c820, 0x71, "1b78084ca9dc4f57b6bbfd9f94709def101392cb4e707c998d0e0c29db152d2a"),
        "frame_stage": (0xb6e540, 0x5F0, "b7a10b983622647cc31c42274618f8708848ea82d75777edbbf07c82fde0bcbc"),
        "model_interface_binding": (0xb65f55, 0x22, "e8c303b0d2765e93cb1a8c0f1f3cebf68b3b77c914fde48e3cea5ff0b086a62c"),
        "native_model_name_consumer": (0xac4581, 0x1E, "4a868e3d2685bd8b4cd3d05507d68456c7e1c07e14fc4766ee38b893ef977423"),
    },
    "engine": {"network_stage_producers": (0x482bf, 0x514, "721781d9fe4b11255f4643b18752c199efc4cf1b90dcef8a4e08a30efc35a290")},
    "host": {
        "native_model_resolver": (0x16560, 0xBF, "dfd9fb0e1773eb52ccb5cd45bc645ee17a8bd77fc35f6bfd32a1946372d9aed5"),
        "resource_name_initializer": (0xC4380, 0x11B, "5be7ea1ea87172092e8dc7f1c39f6bb2be2adc18c1da532b957cdca1e0dc7c26"),
    },
    "resource": {
        "find_or_register": (0x16540, 0x432, "80e64b6f705f77cdcce23f8223e0e8d84891b93a04a678c831697855081ec483"),
        "blocking_jit_loader": (0x16B20, 0x44B, "0d265605d67eeafe4c0c219ee8a88a01459e3932f456b8ab3993159e2e3fd51c"),
        "manifest_submission": (0x1AA80, 0x186, "9028636fbd2adee191ce807042e1d4174ce3ec71926ac3307c7840f30de1aa95"),
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
    require(table == 0x1c65030 and pointer(client, cb, table + 36 * 8) == 0xb6e540, "Changed native frame callback RTTI/slot")
    require(primary_vtable(host, hb, b".?AVCGameModelInfo@@") == 0x11D800, "Changed GameModelInfo RTTI")
    require(pointer(host, hb, 0x11D800 + 0x60) == 0x16560, "Changed model resolver slot")
    require(primary_vtable(resource, rb, b".?AVCResourceSystem@@") == 0x65348, "Changed resource-system RTTI")
    require(pointer(resource, rb, 0x65348 + 0x148) == 0x16B20, "Changed blocking-load vtable slot")
    require(pointer(resource, rb, 0x65348 + 0x278) == 0x16540, "Changed find/register resource slot")
    require(pointer(resource, rb, 0x65348 + 0xB0) == 0x1AA80, "Changed native blocking manifest submission slot")
    require(cstring(client, rip(client, 0xb65f64)) == "GameModelInfo001", "Changed model interface name")
    require(rip(client, 0xb65f6d) == rip(client, 0x9651fc) == 0x2562e60, "Model setter/interface binding disagree")
    require(client[0x96520b:0x96520e] == bytes.fromhex("ff5060"), "Changed model resolution ABI")
    require(call(client, 0x965216) == 0x965230, "Changed model setter binding call")
    skeleton = schema(client, cb, 0x2201478, 0x2201380, 7, "CSkeletonInstance")
    state = schema(client, cb, 0x2200058, 0x21ffe90, 14, "CModelState")
    binding = skeleton["m_modelState"] + state["m_hModel"]
    require(binding == u32(client, 0x21c874) == 0x1E0, "Model binding schema/getter disagree")
    require(client[0x21c87d:0x21c880] == bytes.fromhex("488b18"), "Changed resource-binding indirection")
    require(call(client, 0xac458e) == 0x1778510 and client[0x1778513] == 8, "Changed native model-name getter")
    require(u32(client, 0xac4405) == state["m_hModel"], "Changed native strong-handle member")
    # Native setter increments/decrements the binding count; the project must not
    # borrow and later manually decrement this engine-owned reference.
    require(client[0xac43fe:0xac4402] == bytes.fromhex("f0ff4220"), "Changed native handle acquisition")
    require(client[0xac441c:0xac4420] == bytes.fromhex("f0ff4a20"), "Changed native handle release")
    for stage, at in ((4, 0x482bf), (5, 0x48457), (6, 0x4863b), (7, 0x486c9), (8, 0x487be)):
        require(rip(engine, at) == 0x7340f8 and engine[at + 7] == 0xBA and u32(engine, at + 8) == stage,
                f"Changed native stage-{stage} producer")
        require(engine[at + 15:at + 21] == bytes.fromhex("ff9020010000"), "Changed native frame dispatch slot")
    require(u32(client, 0xb6eb08 + (8 - 3) * 4) == 0xb6e96f, "Changed stage-8 native branch")
    require(u32(client, 0xb6eb08 + (10 - 3) * 4) == 0xb6e6c4, "Changed later worker-stage branch")
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
    require(resource[0x16BE9:0x16BF0] == bytes.fromhex("66c74424500100"), "Changed blocking manifest flags")
    require(resource[0x1AA80:0x1AA88] == bytes.fromhex("41b001e908000000"), "Changed synchronous manifest submission thunk")
    require(resource[0x16F38:0x16F3E] == bytes.fromhex("ff90c8000000"), "Changed native manifest release")
    require(resource[0x16F4A:0x16F50] == bytes.fromhex("ff9078020000"), "Changed loaded binding lookup")
    require(cstring(resource, rip(resource, 0x16CD4)) == "CResourceSystem::BlockingLoadResourceByNameIntoJustInTimeManifest",
            "Changed blocking-load diagnostic identity")
    expected = {
        "FrameStage": 0xb6e540, "FrameVtable": table, "FrameSlot": 36, "ApplyStage": 8,
        "SetModel": 0x9651f0, "SetModelBinding": 0x965230, "ModelInterface": 0x2562e60,
        "ModelResolveSlot": 0x60, "ModelNameGetter": 0x1778510, "ModelBinding": binding,
        "ResourceName": client[0x1778513], "HostTimestamp": int(reports["host"]["timestamp"], 0),
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
        ("FrameStageBytes", client, 0xb6e540, 29), ("SetModelBytes", client, 0x9651f0, 0x31),
        ("ModelNameGetterBytes", client, 0x1778510, 0x13),
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
