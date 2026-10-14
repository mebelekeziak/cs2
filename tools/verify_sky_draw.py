"""Stdlib-only verification of immutable local sky draw evidence and source constants."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game')


def mapped(path):
    raw = path.read_bytes()
    nt = struct.unpack_from('<I', raw, 0x3c)[0]
    image = bytearray(struct.unpack_from('<I', raw, nt + 24 + 56)[0])
    base = struct.unpack_from('<Q', raw, nt + 24 + 24)[0]
    for index in range(struct.unpack_from('<H', raw, nt + 6)[0]):
        section = nt + 24 + struct.unpack_from('<H', raw, nt + 20)[0] + index * 40
        size, rva, rawsize, offset = struct.unpack_from('<4I', raw, section + 8)
        image[rva:rva + rawsize] = raw[offset:offset + rawsize]
    return raw, image, base, nt


def require_bytes(image, checks):
    for label, (rva, expected) in checks.items():
        value = bytes.fromhex(expected)
        if image[rva:rva + len(value)] != value:
            raise SystemExit(f'Changed machine evidence: {label} at {rva:#x}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT)
    parser.add_argument('--header', type=Path, default=ROOT / 'include/world_offsets.h')
    args = parser.parse_args()
    raw, image, base, nt = mapped(args.game / 'bin/win64/scenesystem.dll')
    digest = hashlib.sha256(raw).hexdigest()
    if digest != 'bd770af13a0b1352d87de26cbcfef9e36ef809771ae6a2e5314d7bc6b341f831':
        raise SystemExit('Changed scenesystem.dll: repeat independent investigation.')
    checks = {
        'render_entry': (0x154FB0, '4585c90f8e1f0700004c8bdc554156498dab58fcffff4881ec98040000'),
        'batch_source': (0x154FCD, '4963c14c8bf2486bc87049897b184a8b7c01a8'),
        'sixth_view_arg': (0x154FF1, '4c8bbdd8030000'),
        'tint_rg': (0x155149, 'f20f108fe8000000'),
        'tint_b': (0x155156, 'f30f109ff0000000'),
        'tint_key': (0x155173, 'ba2a00e699'),
        'tint_call': (0x1551A4, 'e8c788efff'),
        'seventh_stats_arg': (0x155610, '488b85e0030000'),
        'allocation_size': (0x1557EE, 'ba10010000'),
        'visible_kind': (0x155908, 'c787c4000000883cfde5'),
        'setter_entry': (0x4DA70, '48895c24084889742410574883ec20'),
        'setter_input_copy': (0x4DC66, '0f1007'),
        'setter_output_copy': (0x4DC73, '0f1101'),
    }
    require_bytes(image, checks)
    client_raw, client, _, _ = mapped(args.game / 'csgo/bin/win64/client.dll')
    if hashlib.sha256(client_raw).hexdigest() != '44af2aa0d2c64f90d273444cb393b8b679094f0b02136dbdb89d4ec82a560008':
        raise SystemExit('Changed client.dll: repeat lighting discriminator investigation.')
    require_bytes(client, {'lighting_kind': (0x28232E, 'c780c40000009eee8108')})
    table = 0x5F64D8
    locator = struct.unpack_from('<Q', image, table - 8)[0] - base
    name = struct.unpack_from('<I', image, locator + 12)[0] + 16
    if bytes(image[name:image.find(0, name)]) != b'.?AVCSkyBoxObjectDesc@@':
        raise SystemExit('Unexpected descriptor RTTI')
    render = struct.unpack_from('<Q', image, table + 8)[0] - base
    call = 0x1551A4
    observed = {
        'SceneTimestamp': struct.unpack_from('<I', raw, nt + 8)[0],
        'SceneImageSize': len(image), 'SkyDescriptorVtable': table, 'SkyRender': render,
        'SetFloat4': call + 5 + struct.unpack_from('<i', image, call + 1)[0],
        'SkyTintReturn': call + 5, 'DrawStride': image[0x154FD6],
        'DrawSource': 0x70 + struct.unpack_from('<b', image, 0x154FDF)[0],
        'SceneKind': struct.unpack_from('<I', image, 0x15590A)[0],
        'VisibleKind': struct.unpack_from('<I', image, 0x15590E)[0],
        'LightingKind': struct.unpack_from('<I', client, 0x282334)[0],
        'LightingProducer': 0x28232E,
        'SkyTintKey': struct.unpack_from('<I', image, 0x155174)[0],
    }
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', args.header.read_text(), flags=re.S)
    for name, value in observed.items():
        match = re.search(rf'\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;', source)
        if not match or int(match.group(1), 0) != value:
            raise SystemExit(f'Header {name} differs from decoded operand {value:#x}')
    for name, address, module in [('SkyRenderBytes',render,image),('SetFloat4Bytes',observed['SetFloat4'],image),
                                  ('LightingProducerBytes',observed['LightingProducer'],client)]:
        match = re.search(rf'\b{name}\[\]\s*\{{([^}}]+)\}}', source)
        blob = bytes(int(v,0) for v in re.findall(r'0[xX][0-9a-fA-F]+|\d+', match[1])) if match else b''
        if not blob or module[address:address+len(blob)] != blob:
            raise SystemExit(f'Header prolog {name} differs')
    print(json.dumps({'scenesystem_sha256':digest, 'verified_code':list(checks),
        'source_constants':{k:hex(v) for k,v in observed.items()},
        'setter_copies_by_value':True, 'live_observation':False}, indent=2))


if __name__ == '__main__':
    main()
