"""Read-only verification of the installed world's chosen rendering paths.

Does not launch, open, inject or change the client. No internet offsets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
LEGACY_CLIENT_SHA256 = "44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008"
DEFAULT_CLIENT = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")

def load_image(path):
    raw = path.read_bytes()
    nt = struct.unpack_from("<I", raw, 0x3C)[0]
    optional = nt + 24
    base = struct.unpack_from("<Q", raw, optional + 24)[0]
    image = bytearray(struct.unpack_from("<I", raw, optional + 56)[0])
    for index in range(struct.unpack_from("<H", raw, nt + 6)[0]):
        section = optional + struct.unpack_from("<H", raw, nt + 20)[0] + index * 40
        rva, size, start = struct.unpack_from("<III", raw, section + 12)
        image[rva:rva + size] = raw[start:start + size]
    pdata, size = struct.unpack_from("<II", raw, optional + 112 + 3 * 8)
    functions = [struct.unpack_from("<III", image, pos) for pos in range(pdata, pdata + size, 12)]
    return raw, image, base, nt, functions

def rip_target(image, site):
    return site + 7 + struct.unpack_from("<i", image, site + 3)[0]

def legacy_update_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=DEFAULT_CLIENT)
    args = parser.parse_args()
    raw, image, base, nt, functions = load_image(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != LEGACY_CLIENT_SHA256:
        raise SystemExit("Historical --legacy-update requires the original 1.41.9.0 client binary; use default verification for the current per-view sky implementation.")
    checks = {
        "sky_update": (0x282020, "48895c241048897424185557415441554157488bec4883ec70"),
        "visible_color_source": (0x282066, "488d91ac100000"),
        "visible_brightness": (0x282076, "f30f1093b4100000"),
        "visible_scene_pointer": (0x2820B2, "488db3e8100000"),
        "visible_scene_tint": (0x2821A3, "f20f1045b0f20f1181e8000000"),
        "lighting_color_source": (0x282275, "488d93b0100000"),
        "lighting_brightness": (0x282285, "f30f1093b4100000"),
        "lighting_scene_pointer": (0x2822C1, "488b8bf0100000"),
        "lighting_scene_tint": (0x2823C2, "f20f1045b0f20f1181e8000000"),
        "registration": (0x271346, "488d05d30c0100488bcf4889442428"),
        "zero_return": (0x28253E, "33c0"),
        "pre_output_owner": (0x3CDB30, "48894c240855535657415541564157488dac24d0feffff4881ec30020000"),
        "pre_output_caller": (0xA4EAA0, "4883ec28488b0d55539601e880f097ff"),
        "worker_thunk": (0x3DE9B0, "488b4908e91726fcff"),
        "callback_dispatch": (0x3A1156, "488b0a4c8b4a08498bd741ffd1"),
        "stack_owned_worker": (0x3CE123, "488d05660774014889442448"),
    }
    for name, (site, expected) in checks.items():
        wanted = bytes.fromhex(expected)
        if image[site:site + len(wanted)] != wanted:
            raise SystemExit(f"Evidence differs: {name} at 0x{site:X}")
    decoded = {
        "SkyUpdate": rip_target(image, 0x271346),
        "PreOutput": 0xA4EAAB + 5 + struct.unpack_from("<i", image, 0xA4EAAC)[0],
        "SkyVtable": rip_target(image, 0x26FCC4),
        "VisibleScene": struct.unpack_from("<I", image, 0x2820B2 + 3)[0],
        "LightingScene": struct.unpack_from("<I", image, 0x2822C1 + 3)[0],
        "SceneLinearTint": struct.unpack_from("<I", image, 0x2821A8 + 4)[0],
    }
    if struct.unpack_from("<Q", image, decoded["SkyVtable"] + 13 * 8)[0] - base != 0x271300:
        raise SystemExit("Sky callback registration vtable slot differs")
    print(json.dumps({"client": str(args.client), "sha256": digest,
        "machine": hex(struct.unpack_from('<H', raw, nt + 4)[0]),
        "timestamp": hex(struct.unpack_from('<I', raw, nt + 8)[0]),
        "image_size": hex(len(image)), "verified_code": list(checks),
        "historical_research_offsets": {name: hex(value) for name, value in decoded.items()},
        "callback": "RCX=C_EnvSky; EAX=0; original writes both scene tint float3 values",
        "native_update_recurrence": "transient queue; not a dependable per-frame callback",
        "pre_output_context": "engine rendergamesystem.cpp OnClientPreOutputParallelWithServer; stack-owned scene-update worker context",
        "live_observation": False}, indent=2))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_CLIENT.parents[3])
    parser.add_argument("--legacy-update", action="store_true",
                        help="Inspect the original 1.41.9.0 transient-update research; requires that historical binary and is not used by the feature")
    args = parser.parse_args()
    saved = sys.argv
    try:
        if args.legacy_update:
            sys.argv = [saved[0], "--client", str(args.game / "csgo/bin/win64/client.dll")]
            legacy_update_main()
            return
        # Dedicated frozen verifiers compare final runtime source constants
        # against independently decoded local machine operands.
        from verify_sky_draw import main as sky_main
        from verify_world_materials import main as material_main
        from verify_world_lighting import main as lighting_main
        from verify_lighting_switches import main as switches_main
        from verify_environment_lighting import main as environment_main
        sys.argv = [saved[0], "--game", str(args.game)]
        sky_main()
        sys.argv = [saved[0], "--installation", str(args.game / "bin/win64")]
        material_main()
        for verify in (lighting_main, switches_main, environment_main):
            sys.argv = [saved[0], "--game", str(args.game)]
            verify()
    finally:
        sys.argv = saved

if __name__ == "__main__":
    main()
