"""Pin installed per-view lighting operands and compare independently to runtime.

Read-only: no process access, game launch, source generation or offset updates.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from inspect_baked_lighting import CHECKS, DEFAULT_GAME, EXPECTED_SCENE
from inspect_world import load_image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    args = parser.parse_args()
    path = args.game / "bin/win64/scenesystem.dll"
    raw, image, _, nt, _ = load_image(path)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_SCENE:
        raise SystemExit("Changed scenesystem.dll: repeat environment lighting investigation")
    for label, (site, hexbytes) in CHECKS.items():
        expected = bytes.fromhex(hexbytes)
        if image[site:site + len(expected)] != expected:
            raise SystemExit(f"Changed installed operand {label} at 0x{site:X}")
    source = (ROOT / "include/world_environment_lighting_offsets.h").read_text()
    def constant(name):
        match = re.search(r"\b" + name + r"\s*=\s*(0x[0-9A-Fa-f]+)", source)
        if not match:
            raise SystemExit(f"Missing header constant {name}")
        return int(match.group(1), 16)
    expected = {
        "Timestamp": struct.unpack_from("<I", raw, nt + 8)[0],
        "ImageSize": len(image),
        "CopyBuffer": 0x1B0CCC + 5 + struct.unpack_from("<i", image, 0x1B0CCD)[0],
        "CopyReturn": 0x1B0CCC + 5,
        "BufferProducer": 0x1B0CB5,
        "BufferUpload": 0x1B0F77,
        "DirectionalProducer": 0x1AF917,
        "DirectionalStore": 0x1AF939,
        "SunProducer": 0x1AFA8C,
        "BufferSize": struct.unpack_from("<I", image, 0x1B0CB7)[0],
        # Native base source is RBP-0x30. Store RBP+0x7970 therefore
        # maps to source+0x79A0, not the neighboring direction at +0x7990.
        "SunRgb": struct.unpack_from("<I", image, 0x1AFAA0)[0] + 0x30,
        "DirectionalLightmap": image[0x1AF93D] + 0x30,
    }
    for name, value in expected.items():
        if constant(name) != value:
            raise SystemExit(f"{name}: header differs from decoded local native operand")
    sites = {"CopyBytes": 0x569120, "ProducerBytes": 0x1B0CB5,
             "UploadBytes": 0x1B0F77,
             "DirectionalBytes": 0x1AF917, "DirectionalStoreBytes": 0x1AF939, "SunBytes": 0x1AFA8C}
    for name, site in sites.items():
        match = re.search(r"\b" + name + r"\[\]\s*\{([^}]+)\}", source)
        if not match:
            raise SystemExit(f"Missing checked byte array {name}")
        code = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]+)", match.group(1)))
        if not code or image[site:site + len(code)] != code:
            raise SystemExit(f"{name}: header code differs from installed native bytes")
    print(json.dumps({"path": str(path), "sha256": digest,
        "verified_runtime_constants": {name: hex(value) for name, value in expected.items()},
        "mechanism": "current-thread renderer stack packets before native copy/GPU upload",
        "live_observation": False}, indent=2))


if __name__ == "__main__":
    main()
