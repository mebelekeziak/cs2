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
EXPECTED_HASH = 'ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5'
WINDOWS = {'current_services_getter': (0xA80B30, 31, '80c963b123e5423ad1ecdaecb3f1dbf86572839d1ddf9b3bc7b800286bd90ce3'),
 'pawn_current_getter': (0x93D490, 243, 'c16498dd3b6eb5d91229014a8fc02763d8d3924ecb56bb065b6d7d512d068a96'),
 'base_eye': (0x15FA90, 108, 'eb16fe1a63b2b1368355ff327d3dbfac618e1d7c71c07884f35f50cd1024794a'),
 'view_offset': (0x160750, 69, '4e08b8c69babe4ae525cdcbc69b179476bab02d9daba67e24fad7688f406ccd1'),
 'view_offset_interpolation': (0x160560, 453, 'f015f8d2e605e5ce6d3e58a26dd1184fe5714c73be6cd2780cc7bfbf5a9db737'),
 'history_producer': (0x914700, 480, 'dcd9fbf34b67833e5dd0cb783e38a317d888f73cb42edd5e3369ce89a657545d'),
 'history_interpolator': (0x904860, 1194, 'd1c72b196f1c23671f2193e1d51063c40ab24c981d23871f42d1d0b148c1b37d'),
 'history_bracketing': (0x904D10, 411, 'c71c793a63e494908d6c7f6e5f1ee8c1a3f6fcc5f78d425fbaf3ece3c9b7ae67')}


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
    registration, array, count = 0x21F67E8, 0x21F6450, 28
    if string(ptr(registration)) != 'C_BasePlayerPawn' or ptr(registration + 0x28) != array or struct.unpack_from('<H', image, registration + 0x1C)[0] != count:
        raise SystemExit('Changed C_BasePlayerPawn named schema registration')
    fields = {string(ptr(array + i * 32)): u32(array + i * 32 + 16) for i in range(count)}
    services = primary_vtable(image, base, b'.?AVCCSPlayer_WeaponServices@@')
    pawn = primary_vtable(image, base, b'.?AVC_CSPlayerPawn@@')
    slot = u32(0x9147C8)
    pawn_slot = u32(0xA80B42)
    offset_slot = u32(0x15FABB)
    expected = {
        'ServicesVtable': services, 'PawnVtable': pawn, 'GetterSlot': slot,
        'Getter': ptr(services + slot), 'PawnGetterSlot': pawn_slot, 'PawnGetter': ptr(pawn + pawn_slot),
        'ViewOffsetSlot': offset_slot, 'ViewOffsetGetter': ptr(pawn + offset_slot),
        'BaseEye': call(0x93D56B), 'ServicePawn': image[0xA80B39],
        'WeaponServices': fields['m_pWeaponServices'], 'CameraServices': fields['m_pCameraServices'],
        'ObserverServices': fields['m_pObserverServices'],
    }
    if u32(0x904AB6) != slot or u32(0x93D4A3) != fields['m_pCameraServices'] or u32(0x93D4E0) != fields['m_pObserverServices']:
        raise SystemExit('Independent native origin/fallback/schema operands disagree')
    if call(0x160771) != 0x160560 or call(0x160782) != 0x16A3B0 or call(0x15FAA5) != 0x219A60:
        raise SystemExit('Changed native interpolation/transform/absolute-origin call chain')
    debug = 0x9048A5
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
