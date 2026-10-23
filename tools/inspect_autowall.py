"""Read-only installed-client autowall contract verification; does not launch CS2."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import DEFAULT, mapped

ROOT = Path(__file__).resolve().parents[1]


def verify(game, header):
    contract = json.loads((ROOT / 'tools/aim_autowall_contract.json').read_text())
    raw, image, base, _ = mapped(game / 'csgo/bin/win64/client.dll')
    if hashlib.sha256(raw).hexdigest() != contract['client_sha256']:
        raise SystemExit('Changed client hash; investigate native penetration contracts')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(), flags=re.S)
    for name, value in contract['constants'].items():
        match = re.findall(rf'\b{name}\s*=\s*(0x[\da-fA-F]+|\d+)\s*;', source)
        if len(match) != 1 or int(match[0], 0) != value:
            raise SystemExit(f'Header constant mismatch: {name}')
    for name, (at, size, digest) in contract['windows'].items():
        actual = bytes(image[at:at+size])
        if hashlib.sha256(actual).hexdigest() != digest:
            raise SystemExit(f'Changed native window: {name}')
        match = re.findall(rf'\b{name}Bytes\[\]\{{([^}}]+)\}}', source)
        if len(match) != 1 or bytes(int(x, 0) for x in match[0].split(',')) != actual:
            raise SystemExit(f'Header instruction window mismatch: {name}')
        match = re.findall(rf'\{{(0x[\da-fA-F]+|\d+),\s*{name}Bytes,\s*sizeof\({name}Bytes\)\}}', source)
        if len(match) != 1 or int(match[0], 0) != at:
            raise SystemExit(f'Header runtime window address mismatch: {name}')
    def u32(at): return struct.unpack_from('<I', image, at)[0]
    def ptr(at): return struct.unpack_from('<Q', image, at)[0] - base
    def string(at): return bytes(image[at:image.index(0, at)]).decode('ascii')
    registration, fields = 0x221cd08, 0x221c1a0
    if string(ptr(registration)) != 'CCSWeaponBaseVData' or ptr(registration+0x28) != fields:
        raise SystemExit('Changed weapon schema owner')
    named = {string(ptr(fields+i*32)): u32(fields+i*32+16) for i in range(83)}
    if named.get('m_flPenetration') != contract['constants']['Penetration']:
        raise SystemExit('Changed named weapon penetration field')
    # Independent native operands bind schema, stack layouts and buffer strides.
    operands = {'Penetration': (0xd251af, 4), 'SegmentCount': (0x886585, 4),
        'Segments': (0x8865c3, 4), 'HitSize': (0xa1adca, 1),
        'SegmentFlags': (0x8a2674, 1), 'SegmentFirst': (0x8a26de, 1),
        'SegmentLast': (0x8a2889, 1)}
    # Full frozen functions establish semantics; also decode the selected immediates.
    for name, (at, width) in operands.items():
        value = int.from_bytes(image[at:at+width], 'little')
        if value != contract['constants'][name]:
            raise SystemExit(f'Native operand disagreement: {name} at {at:#x}: {value:#x}')
    return {'client_sha256': contract['client_sha256'], 'constants': contract['constants'],
        'native_windows': len(contract['windows']), 'weapon_schema': {'m_flPenetration': named['m_flPenetration']}}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT)
    parser.add_argument('--header', type=Path, default=ROOT / 'include/aim_autowall_offsets.h')
    args = parser.parse_args()
    print(json.dumps(verify(args.game, args.header), indent=2))
