"""Read-only installed-client static lighting evidence; never opens a process.

This pins observed operands, not an implementation of a baked-light override.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from inspect_world import load_image

DEFAULT_GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game")
# October 15, 2026: all frozen sites and full historical callback/copy/upload
# disassemblies compared unchanged before accepting the 1.41.9.1 PE metadata.
EXPECTED_SCENE = "040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5"
CHECKS = {
    "baked_cvar_registration": (0x8799, "488d1560675e00488d0d71a59100"),
    "baked_cvar_consumer": (0x5499B, "488b057ee38c0040387058750b"),
    "baked_bind_call": (0x549AE, "e80d550200"),
    "baked_bind_helper": (0x79EC0, "48895c240848896c241048897424185741544155415641574883ec40"),
    "directional_lightmaps_registration": (0x11659, "488d15c0af5e00488d0dd9259100"),
    "directional_lightmaps_value": (0x1AF917, "488b052a4377008078580074050f57c0eb04"),
    "directional_lightmaps_stack_store": (0x1AF939, "f30f114510"),
    "native_one_constant": (0x5FFF40, "0000803f"),
    "selected_sun_copy": (0x1A7B3E, "e8cd54eeff"),
    "sun_rgb_source_r": (0x1AFA8C, "f30f1087cc000000"),
    "sun_rgb_store_r": (0x1AFA9C, "f30f118570790000"),
    "sun_rgb_source_g": (0x1AFAA4, "f30f108fd0000000"),
    "sun_rgb_source_b": (0x1AFAB4, "f30f1087d4000000"),
    "view_buffer_copy": (0x1B0CB5, "41b8e07d000089859c7c0000488d55d0488d8db07d0000e84f843b00"),
    "view_buffer_upload": (0x1B0F77, "c7442428e07d00004533c04889542420488b01498b9550e50000ff90f8000000"),
}
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    args = parser.parse_args()
    path = args.game / "bin/win64/scenesystem.dll"
    raw, image, _, _, _ = load_image(path)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_SCENE:
        raise SystemExit("scenesystem.dll changed: repeat the static lighting investigation")
    for name, (rva, hexbytes) in CHECKS.items():
        expected = bytes.fromhex(hexbytes)
        if image[rva:rva + len(expected)] != expected:
            raise SystemExit(f"{name}: installed bytes differ at 0x{rva:X}")
    for rva, text in [(0x5EEF00, b"sc_disable_baked_lighting\0"),
                      (0x5FC620, b"r_directional_lightmaps\0")]:
        if image[rva:rva + len(text)] != text:
            raise SystemExit("Lighting cvar identification changed")
    target = 0x1B0CCC + 5 + struct.unpack_from("<i", image, 0x1B0CCD)[0]
    assert target == 0x569120
    print(json.dumps({"path": str(path), "sha256": digest,
        "checked_sites": len(CHECKS), "view_buffer_size": "0x7DE0",
        "sun_rgb_view_buffer_offset": "0x79A0",
        "directional_lightmap_constant_offset": "0x40",
        "baked_light_override_implemented": False, "live_observation": False}, indent=2))


if __name__ == "__main__":
    main()
