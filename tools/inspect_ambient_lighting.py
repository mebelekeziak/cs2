"""Read-only native ambient producer and original installed shader consumers.

Uses the MIT VRF dictionary and research-only zstandard dependency already
retained under logs/research. Does not access a live process or modify assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

from inspect_baked_lighting import DEFAULT_GAME, EXPECTED_SCENE
from inspect_surface_tint import ASSETS, disassemble, extract
from inspect_world import load_image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT_GAME)
    parser.add_argument('--research', type=Path, default=ROOT / 'logs/research')
    args = parser.parse_args()
    raw, image, _, _, _ = load_image(args.game / 'bin/win64/scenesystem.dll')
    if hashlib.sha256(raw).hexdigest() != EXPECTED_SCENE:
        raise SystemExit('Changed scene binary; repeat ambient investigation')
    sites = {
        'context_key': (0x1AF956, '41b8f1ae1083'),
        'context_getter': (0x1AF993, 'e8d8d8e9ff'),
        'float4_store': (0x1AF998, '0f10442460488b05e4427700f30f7f85f0000000'),
    }
    for name, (site, code) in sites.items():
        expected = bytes.fromhex(code)
        if image[site:site + len(expected)] != expected:
            raise SystemExit(f'Changed ambient producer {name}')
    if struct.unpack_from('<I', image, 0x1AF9A8)[0] + 0x30 != 0x120:
        raise SystemExit('Changed ambient frame offset')
    dictionary = (args.research / 'vrf20/vfx_zstd_2bc2fa87.bin').read_bytes()
    if hashlib.sha256(dictionary).hexdigest() != 'e7ab9ea47f213132dce97bcf18c77c5fed91bea765af1ceb4b580ed0bf660f5b':
        raise SystemExit('Changed official VRF dictionary')
    sys.path.insert(0, str(args.research / 'python-deps'))
    import zstandard
    decoder = zstandard.ZstdDecompressor(dict_data=zstandard.ZstdCompressionDict(dictionary))
    output = {'frame_ambient_rgb': '0x120', 'context_key': '0x8310aef1',
              'context_key_name_resolved': False, 'shader_consumers': {}, 'live_observation': False}
    args.research.mkdir(parents=True, exist_ok=True)
    for label, (asset, expected_hash) in ASSETS.items():
        data = extract(args.game / 'csgo/shaders_pc_dir.vpk', asset)
        if hashlib.sha256(data).hexdigest() != expected_hash:
            raise SystemExit(f'Changed installed {label} shader')
        offset = struct.unpack_from('<I', data)[0]
        compression, size, packed = struct.unpack_from('<iII', data, offset)
        if compression != -3:
            raise SystemExit('Changed shader compression')
        decoded = decoder.decompress(data[offset + 12:offset + 12 + packed])
        if len(decoded) != size:
            raise SystemExit('Truncated shader data')
        variants = []
        at = 0
        while True:
            at = decoded.find(b'DXBC', at)
            if at < 0:
                break
            length = struct.unpack_from('<I', decoded, at + 24)[0]
            bytecode = decoded[at:at + length]
            assembly = disassemble(bytecode)
            if ('cb3[1946].xyzx' not in assembly or
                len(re.findall(r'cb3\[18\]\.xxyz', assembly)) != 2 or
                not re.search(r'mad r\d+\.yzw, r\d+\.zzzz, r\d+\.xxyz, cb3\[18\]\.xxyz', assembly) or
                not re.search(r'mov r\d+\.yzw, cb3\[18\]\.xxyz', assembly)):
                raise SystemExit(f'Changed native diffuse fill consumer {label}:{len(variants)}')
            number = len(variants)
            (args.research / f'ambient-{label}-ps{number}-20261015.asm').write_text(assembly)
            variants.append({'variant': number, 'sha256': hashlib.sha256(bytecode).hexdigest(),
                             'bytes': length, 'baked_cb5': 'dcl_constantbuffer CB5[1]' in assembly,
                             'ambient_float4': 18, 'sun_float4': 1946})
            at += length
        if not variants:
            raise SystemExit('No original shader variants found')
        output['shader_consumers'][label] = {'asset_sha256': expected_hash, 'variants': variants}
    (args.research / 'ambient-lighting-report.json').write_text(json.dumps(output, indent=2))
    print(json.dumps(output, indent=2))


if __name__ == '__main__':
    main()
