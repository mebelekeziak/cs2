"""Verify the installed CGrenadeTracer practice-preview API, without launching CS2."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable, DEFAULT

ROOT = Path(__file__).resolve().parents[1]
SHA256 = 'ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5'
WINDOWS = {
    'factory': (0x10cba50, 0x526, '0db969734701399288afe62cca1e9905f80e59da362a00f27e785072747b455f'),
    'simulate': (0x10cc800, 0x472, '4a6167307c9ec5384f989b08579943b4d16ebc9b1a3ee18feb56f4a74fa6edf5'),
    'remove': (0xa1c6c0, 0xb5, '3ae05915c488c8344ad33bd5b52adf42d4d786209b88f4077668a45434e5e103'),
    'position': (0x219a60, 0x2c, '5b0f9e059a829ffdad784ecba8b13ee57fa82262aaa1d65a3cca336ac58037af'),
    'practice_caller': (0x7e0a50, 0x183, '43e91a32993d9a5b82d75724f5ae1b8047d6d8c0371c0c12949ab50abad725d7'),
    'console_caller': (0x10d9630, 0xf0, 'fe6c4786a8c8f240d4cbf51b3dbf990c5b3bde1ad66a7efc54b7d856a06de132'),
    'collision_callback': (0x10d2a80, 0x39e, '98561984f442e02c8ea862c856a7758c4f65838a80ccad4af033c8940a751680'),
    'initialize': (0x10d3860, 0xe2, '0237d5b703bbfbfea3e2ca63d40e7a40928269252e0de161d26471f238fdf976'),
    'append_point': (0x10c98f0, 0xa2, '071044cd381eec6d6d0c6d8b0a122ed9b377d760f3ba64e75029cc41ed969cc3'),
    'new_segment': (0x10d4430, 0x150, 'c6b440b2ecaa3fc9d38615bafd45d47713cb568a0cf3b0e89a23ad7f1548d6b5'),
    'clear_segments': (0x10d28b0, 0xe0, '14236c5e9c32adba1722519eff04aba787d10240a24fa5413703dd45ab65d519'),
}


def verify(game=DEFAULT, header=ROOT/'include/grenade_native_offsets.h'):
    raw, image, base, _ = mapped(game/'csgo/bin/win64/client.dll')
    if hashlib.sha256(raw).hexdigest() != SHA256:
        raise SystemExit('Changed client; independently reverify native grenade preview.')
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at+size]).hexdigest() != digest:
            raise SystemExit('Changed complete native grenade window: '+name)

    def call(at):
        if image[at] != 0xe8:
            raise SystemExit('Expected native grenade direct call')
        return at+5+struct.unpack_from('<i', image, at+1)[0]

    values = {
        'Create': call(0x7e0ae9),
        'Simulate': call(0x7e0bbe),
        'Remove': call(0x10d9704),
        'Position': call(0x10ccc10),
        'TracerVtable': primary_vtable(image, base, b'.?AVCGrenadeTracer@@'),
        'Globals': 0x10cc9ce+struct.unpack_from('<i', image, 0x10cc9ca)[0],
        'Segments': struct.unpack_from('<i', image, 0x10cc846)[0],
    }
    if call(0x10d96e7) != values['Create'] or call(0x10d96fc) != values['Simulate']:
        raise SystemExit('Practice and console paths no longer share the same native implementation')
    # The producer emits vector descriptors at +10D0, data +8, capacity +16,
    # flags +20. Each outer element is itself a 24-byte point-vector descriptor.
    checks = {
        0x10c9915: '4c8d0440',       # index * 3, later scaled by 8
        0x10c9919: '488b83d8100000', # outer data at tracer+10D8
        0x10c9935: '498b41f0',       # previous inner data at descriptor+8
        0x10c9940: 'f30f105488f4',   # VectorWS point stride 3*4
        0x10d4557: '488d0c6d00000000',
        0x10d456b: '488954c808',
        0x10d4570: '488954c810',
        0x10d4575: '8914c8',
    }
    for at, hex_bytes in checks.items():
        expected = bytes.fromhex(hex_bytes)
        if image[at:at+len(expected)] != expected:
            raise SystemExit('Changed native grenade vector layout')

    clocks = [image[at] for at in (0x10cca92, 0x10cca95, 0x10cca9c, 0x10cca9f, 0x10ccaa6)]
    values['ClockSnapshotSize'] = max(clocks)+4
    # Getter values come from the six recognized held class vtables, not our
    # GrenadeKind ordinal. Molotov and incendiary share the native preview kind.
    classes = ['C_HEGrenade', 'C_Flashbang', 'C_SmokeGrenade', 'C_DecoyGrenade', 'C_MolotovGrenade', 'C_IncendiaryGrenade']
    kinds = []
    for name in classes:
        table = primary_vtable(image, base, ('.?AV'+name+'@@').encode())
        getter = struct.unpack_from('<Q', image, table+0xd58)[0]-base
        if image[getter:getter+3] == b'\x33\xc0\xc3':
            kinds.append(0)
        elif image[getter] == 0xb8 and image[getter+5] == 0xc3:
            kinds.append(struct.unpack_from('<I', image, getter+1)[0])
        else:
            raise SystemExit('Changed native grenade kind getter')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for symbol, expected in values.items():
        found = re.findall(r'\b'+symbol+r'\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)\s*;', source)
        if len(found) != 1 or int(found[0], 0) != expected:
            raise SystemExit('Native grenade header mismatch: '+symbol)
    for symbol, expected in [('KindTypes', kinds), ('ClockSlots', clocks)]:
        match = re.search(symbol+r'\[\]\s*\{([^}]+)\}', source)
        actual = [int(v, 0) for v in re.findall(r'0x[0-9A-Fa-f]+|[0-9]+', match.group(1))] if match else []
        if actual != expected:
            raise SystemExit('Native grenade header array mismatch: '+symbol)
    for symbol in ('Create', 'Simulate', 'Remove', 'Position'):
        match = re.search(symbol+r'Bytes\[\]\s*\{([^}]+)\}', source)
        actual = bytes(int(v, 0) for v in re.findall(r'0x[0-9A-Fa-f]+|[0-9]+', match.group(1))) if match else b''
        if len(actual) < 16 or actual != image[values[symbol]:values[symbol]+len(actual)]:
            raise SystemExit('Native grenade entry fingerprint mismatch: '+symbol)
    return {'sha256': SHA256, 'contracts': values, 'native_types': kinds, 'clock_slots': clocks,
            'complete_windows': list(WINDOWS), 'live_observation': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT)
    parser.add_argument('--header', type=Path, default=ROOT/'include/grenade_native_offsets.h')
    args = parser.parse_args()
    print(json.dumps(verify(args.game, args.header), indent=2))
