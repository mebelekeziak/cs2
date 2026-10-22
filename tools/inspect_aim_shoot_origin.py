"""Verify the installed current native shooting-origin getter and its callers.
Reads installed files only. No process, client launch or installed-file writes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_HASH = '5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49'
WINDOWS = {
    'current_services_getter': (0xA7FD00, 31, '80c963b123e5423ad1ecdaecb3f1dbf86572839d1ddf9b3bc7b800286bd90ce3'),
    'pawn_current_getter': (0x93C660, 243, 'b807d65f18e89fc5d9275ae0e93b50683a0b5da27e659efa420bd4ccc88af373'),
    'base_eye': (0x15FAB0, 108, 'eb16fe1a63b2b1368355ff327d3dbfac618e1d7c71c07884f35f50cd1024794a'),
    'view_offset': (0x160770, 69, '444834a2c1cf77952ad9426a58ae056b251597bc0445a4ab06ebafc3c1a76dbe'),
    'view_offset_interpolation': (0x160580, 469, '86509e524b621888a6d4a1d4616bed1f47659f687a9e512f68f72fca5ccf6079'),
    'history_producer': (0x9138D0, 480, 'bd72c534a2293b60f784d90de6d0fd5491a5a8704b97bbc011802db7f290dc76'),
    'history_interpolator': (0x903A30, 1194, '0e78b1a66da3da4deaf5c119e5cac6286a1c5fe72ef8fc83f6eca1cb58874f6f'),
    'history_bracketing': (0x903EE0, 411, '65e54a0c42f477e31339452fe801dc9484300faf036985b79f39ecd7bf6f3229'),
}


def verify(image, base, header):
    def u32(at): return struct.unpack_from('<I', image, at)[0]
    def ptr(at): return struct.unpack_from('<Q', image, at)[0] - base
    def string(at):
        if not 0 < at < len(image): raise SystemExit('Invalid string address')
        end = image.find(0, at, min(at + 1024, len(image)))
        if end < 0: raise SystemExit('Unterminated evidence string')
        return bytes(image[at:end]).decode('ascii')
    def call(at):
        if image[at] != 0xE8: raise SystemExit(f'Expected direct call at {at:#x}')
        return at + 5 + struct.unpack_from('<i', image, at + 1)[0]
    registration, array, count = 0x21F4588, 0x21F41F0, 28
    if string(ptr(registration)) != 'C_BasePlayerPawn' or ptr(registration + 0x28) != array or struct.unpack_from('<H', image, registration + 0x1C)[0] != count:
        raise SystemExit('Changed C_BasePlayerPawn named schema registration')
    fields = {string(ptr(array + i * 32)): u32(array + i * 32 + 16) for i in range(count)}
    services = primary_vtable(image, base, b'.?AVCCSPlayer_WeaponServices@@')
    pawn = primary_vtable(image, base, b'.?AVC_CSPlayerPawn@@')
    slot = u32(0x913998)
    pawn_slot = u32(0xA7FD12)
    offset_slot = u32(0x15FADB)
    expected = {
        'ServicesVtable': services, 'PawnVtable': pawn, 'GetterSlot': slot,
        'Getter': ptr(services + slot), 'PawnGetterSlot': pawn_slot, 'PawnGetter': ptr(pawn + pawn_slot),
        'ViewOffsetSlot': offset_slot, 'ViewOffsetGetter': ptr(pawn + offset_slot),
        'BaseEye': call(0x93C73B), 'ServicePawn': image[0xA7FD09],
        'WeaponServices': fields['m_pWeaponServices'], 'CameraServices': fields['m_pCameraServices'],
        'ObserverServices': fields['m_pObserverServices'],
    }
    if u32(0x903C86) != slot or u32(0x93C673) != fields['m_pCameraServices'] or u32(0x93C6B0) != fields['m_pObserverServices']:
        raise SystemExit('Independent native origin/fallback/schema operands disagree')
    if call(0x160791) != 0x160580 or call(0x1607A2) != 0x16A3D0 or call(0x15FAC5) != 0x219A80:
        raise SystemExit('Changed native interpolation/transform/absolute-origin call chain')
    debug = 0x903A75
    if string(debug + 7 + struct.unpack_from('<i', image, debug + 3)[0]) != '%s: GetInterpolatedShootPosition time [ #%d + %.3f ]\n':
        raise SystemExit('Changed named historical shooting-origin anchor')
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at + size]).hexdigest() != digest:
            raise SystemExit(f'Changed native shooting-origin contract: {name}')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in expected.items():
        values = re.findall(rf'\b{name}\s*=\s*(0[xX][\da-fA-F]+|\d+)', source)
        if len(values) != 1 or int(values[0], 0) != value:
            raise SystemExit(f'Header {name} differs from independently decoded native evidence')
    for name, target, size in [('GetterBytes', 'Getter', 31), ('PawnGetterBytes', 'PawnGetter', 23),
                              ('ViewOffsetGetterBytes', 'ViewOffsetGetter', 10), ('BaseEyeBytes', 'BaseEye', 15)]:
        match = re.search(rf'\b{name}\[\]\s*\{{([^}}]*)\}}', source, re.S)
        copied = bytes(int(v.strip(), 0) for v in match[1].split(',') if v.strip()) if match else b''
        if copied != image[expected[target]:expected[target] + size]: raise SystemExit(f'Header {name} differs')
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll'))
    parser.add_argument('--header', type=Path, default=ROOT / 'include/aim_shoot_origin_offsets.h')
    args = parser.parse_args()
    raw, image, base, _ = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH: raise SystemExit('Client changed: repeat native shooting-origin investigation')
    expected = verify(image, base, args.header)
    print(json.dumps({'client_sha256': digest, 'constants': {name: hex(value) for name, value in expected.items()},
        'native_windows': WINDOWS, 'scope': 'Current native origin only; no historical timestamp selection, input-time mutation or future-origin prediction; installed-file evidence, no live shot observation'}, indent=2))


if __name__ == '__main__': main()
