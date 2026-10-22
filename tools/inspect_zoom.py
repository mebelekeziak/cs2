"""Verify installed scoped weapon states, native baseline FOV and final view hook.

Reads installed files only; standard-library Python; no game launch or writes.
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
SCHEMA = {
    'CBasePlayerController': (0x2223A38, 0x2223800, 17, {'m_iDesiredFOV': 0x794}),
    'C_BasePlayerPawn': (0x21F4588, 0x21F41F0, 28, {'m_pWeaponServices': 0x12F0,
        'm_flFOVSensitivityAdjust': 0x149C, 'm_flMouseSensitivity': 0x14A0}),
    'C_CSPlayerPawn': (0x2239A68, 0x2238D10, 105, {'m_bIsScoped': 0x1E98}),
    'CPlayer_WeaponServices': (0x222BDC8, 0x222BD30, 4, {'m_hActiveWeapon': 0x60}),
    'C_CSWeaponBaseGun': (0x221BF48, 0x221BB00, 7, {'m_zoomLevel': 0x1F20}),
    'CCSWeaponBaseVData': (0x221AAE8, 0x2219F80, 83, {'m_nZoomLevels': 0x7F4, 'm_nZoomFOV1': 0x7F8, 'm_nZoomFOV2': 0x7FC}),
}
TABLES = {
    'C_WeaponAug': 0x1BDEB08, 'C_WeaponAWP': 0x1BDF998,
    'C_WeaponG3SG1': 0x1BE33D8, 'C_WeaponSCAR20': 0x1BF0E68,
    'C_WeaponSG556': 0x1BF1CF8, 'C_WeaponSSG08': 0x1BF2B88,
}
WINDOWS = {
    'normal_view': (0xD02E30, 0x347, '0d7730c1f497c51a69155eceebafce1532a2db268a067f100e389d3accd71fda'),
    'baseline_getter': (0x93FC40, 0x2A, '4a403c400767ea2369d0cf2c62fcd42a2781979bc3498c06f88e4a6d0da498d0'),
    'camera_fov_interpolation': (0x88A630, 0x1B9, 'fe57717ffe5a069f02a39724688e546b684148bf28b56bfd30acc98571144919'),
    'camera_baseline': (0xA681C0, 0xC6, '7beb7c1fb0ee497462fa0ffe3f911d0578ab1db5de55f4004763e0422f3ef91b'),
    'view_fov_producer': (0xB9CA16, 0xFB, '477b4cc97d32ebcbb8177c642204187c98d8e341e660774ca9c11a48cb4795fe'),
    'projection_consumer': (0xB51E2D, 0x86, 'f6cb162f061bd95c6ba86fced7f27af048cfccdfa8bd31ff486351e272a4fbff'),
    'weapon_fov_selection': (0x80E420, 0x70, '92043a52340becfd3050af85f3f2f3fa6484bc20ba94cae345257215923fdbc5'),
    'zoom_state_cycle': (0x8248AE, 0x8E, '75328c193009bbe638bf3ae9669196205e41eaecc89e04d4bfc0460c567e7afb'),
    'zoom_state_consumer': (0x82921C, 0x3D, 'c57dab89226b1ff96914e1479a05cab10cfac7af8691301cf5e1c66aecd79f1d'),
    'mouse_input_caller': (0xB65560, 0x12F, '189914a5f4e6017a1512ee3eeffa4072588439ebcaf321e48ed79078dc262a54'),
    'mouse_scale_identity': (0xB80180, 4, '2fe08174d918f72b8cbcdf7f2affed11f88d44ad2929886406754774bd9e36be'),
    'mouse_scope_dispatch': (0xBA67B0, 0x28C, '773339cce21be75b1d2bb2808d370bb0a4791e7cfc7a3c5c7d4b58781dd413bd'),
    'mouse_convert': (0xBA6ED0, 0x12A, 'd3a470027c68bd3eaf82e154a829f5c61720462c185d58ffc044728f7070bfcf'),
    'mouse_convert_alternate': (0xBA6D00, 0x1C1, 'ceabc8b34419d915471748014bd59c572013ae009c0d292cbf0c8a3dfd5387b1'),
    'pawn_sensitivity_producer': (0xB227D0, 0xEA, '127879958eb4d5ad520304b92ae771fcda3edae4776e70cdfadb8c496de6f88c'),
    'base_sensitivity_registration': (0xD81EE, 0x2A, 'e781cf2cd1be0cd27df72b27e0994b9101ad5a7029883c07a302be9ec97068d9'),
    'zoom_ratio_registration': (0xC957E, 0x2A, '8a026fe7679cc28ba69cda04bb123be748817d646a84acc7b6223db2f5a4706b'),
}


def verify(image, base, header):
    def u32(at): return struct.unpack_from('<I', image, at)[0]
    def ptr(at): return struct.unpack_from('<Q', image, at)[0] - base
    def rip(at, size, displacement): return at + size + struct.unpack_from('<i', image, at + displacement)[0]
    def call(at):
        if image[at] != 0xE8: raise SystemExit(f'Changed relative call at {at:#x}')
        return rip(at, 5, 1)
    def string(at):
        if not 0 < at < len(image): raise SystemExit('Invalid schema string pointer')
        end = image.find(0, at)
        if end < 0 or end - at > 1024: raise SystemExit('Invalid schema string')
        return bytes(image[at:end]).decode('ascii')
    fields = {}
    for name, (registration, array, count, wanted) in SCHEMA.items():
        if string(ptr(registration)) != name or ptr(registration + 0x28) != array or struct.unpack_from('<H', image, registration + 0x1C)[0] != count:
            raise SystemExit(f'Changed schema registration: {name}')
        actual = {string(ptr(array + i*32)): u32(array + i*32 + 16) for i in range(count)}
        if any(actual.get(field) != offset for field, offset in wanted.items()):
            raise SystemExit(f'Changed schema fields: {name}')
        fields[name] = wanted
    for name, table in TABLES.items():
        if primary_vtable(image, base, ('.?AV'+name+'@@').encode()) != table:
            raise SystemExit(f'Changed scoped weapon RTTI: {name}')
        if ptr(table + 0xBF0) != 0x80E420:
            raise SystemExit(f'Changed weapon zoom FOV method: {name}')
    table = primary_vtable(image, base, b'.?AVClientModeCSNormal@@')
    if table != 0x1C9FC20 or ptr(table + 16*8) != 0xD02E30:
        raise SystemExit('Changed final normal-mode view slot')
    input_table = primary_vtable(image, base, b'.?AVCCSGOInput@@')
    if input_table != 0x1C9DB20 or ptr(input_table + 24*8) != 0xB80180:
        raise SystemExit('Changed native mouse pre-scope scale virtual slot')
    if rip(0xEE58C, 7, 3) != input_table or rip(0xEE574, 7, 3) != rip(0xEE59A, 7, 3):
        raise SystemExit('Changed embedded input constructor contract')
    if u32(rip(0xB655C9, 8, 4)) != 0x3F800000 or call(0xB65606) != 0xBA67B0:
        raise SystemExit('Changed exact unscoped mouse scale of 1.0')
    if string(rip(0xD81EE, 7, 3)) != 'sensitivity' or string(rip(0xC957E, 7, 3)) != 'zoom_sensitivity_ratio':
        raise SystemExit('Changed native base/zoom sensitivity cvar registrations')
    if rip(0xD81FA, 7, 3) != rip(0xBA6EEA, 7, 3) or rip(0xC958A, 7, 3) != rip(0xB2285D, 7, 3):
        raise SystemExit('Changed native sensitivity cvar producer/consumer binding')
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at+size]).hexdigest() != digest:
            raise SystemExit(f'Changed native window: {name}')
    # Independently decoded producer/consumer operands, in addition to named schema.
    expected = {
        'OverrideView': ptr(table + 16*8), 'ModeVtable': table,
        'ModeSlot': image[0xB96572], 'ViewFov': u32(0xB51E31),
        'BaselineFov': 0x93FC40, 'DesiredFov': u32(0x93FC46),
        'WeaponServices': fields['C_BasePlayerPawn']['m_pWeaponServices'],
        'ActiveWeapon': fields['CPlayer_WeaponServices']['m_hActiveWeapon'],
        'Scoped': fields['C_CSPlayerPawn']['m_bIsScoped'], 'ZoomLevel': u32(0x829222+2),
        'MouseConvert': call(0xBA6934), 'MouseConvertAlternate': call(0xBA694C),
        'MouseInput': rip(0xEE574, 7, 3), 'MouseInputVtable': input_table,
        'MouseScale': ptr(input_table + 24*8), 'MouseCaller': 0xB655C3,
        'MouseScaleUnit': rip(0xB655C9, 8, 4),
        'MouseScopeDispatch': 0xBA68DA, 'FovSensitivity': u32(0xBA68E6+4),
    }
    if expected['ViewFov'] != u32(0xD0309F+4) or expected['ZoomLevel'] != fields['C_CSWeaponBaseGun']['m_zoomLevel']:
        raise SystemExit('Independent FOV/zoom operands disagree')
    if expected['FovSensitivity'] != fields['C_BasePlayerPawn']['m_flFOVSensitivityAdjust']:
        raise SystemExit('Independent native mouse scale member disagrees with schema')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in expected.items():
        matches = re.findall(rf'\b{name}\s*=\s*(0[xX][\da-fA-F]+|\d+)', source)
        if len(matches) != 1 or int(matches[0],0) != value:
            raise SystemExit(f'Header {name} differs from native evidence')
    for name, at, size in [('OverrideViewBytes',0xD02E30,15), ('BaselineFovBytes',0x93FC40,42),
            ('MouseConvertBytes',0xBA6ED0,16), ('MouseConvertAlternateBytes',0xBA6D00,19),
            ('MouseScaleBytes',0xB80180,4), ('MouseCallerBytes',0xB655C3,0x48),
            ('MouseScaleUnitBytes',0x1AC6074,4),
            ('MouseScopeDispatchBytes',0xBA68DA,0x77)]:
        match = re.search(rf'\b{name}\[\]\s*\{{([^}}]*)\}}', source,re.S)
        actual = bytes(int(v.strip(),0) for v in match[1].split(',') if v.strip()) if match else b''
        if actual != image[at:at+size]: raise SystemExit(f'Header {name} differs')
    match = re.search(r'\bScopedWeaponVtables\[\]\s*\{([^}]*)\}', source,re.S)
    tables = [int(v.strip(),0) for v in match[1].split(',') if v.strip()] if match else []
    if tables != list(TABLES.values()): raise SystemExit('Header scoped weapon vtables differ')
    return expected, fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll'))
    parser.add_argument('--header', type=Path, default=ROOT/'include/zoom_offsets.h')
    args = parser.parse_args()
    raw, image, base, _ = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH: raise SystemExit('Client changed: repeat zoom investigation')
    constants, fields = verify(image, base, args.header)
    print(json.dumps({'client_sha256': digest, 'constants': {k:hex(v) for k,v in constants.items()},
        'schema': fields, 'scoped_weapon_tables': TABLES, 'windows': WINDOWS,
        'abi': ['void(mode*,view*)', 'float(controller*)',
            'void(int,float,float,float,float*,float*,int*,int*)',
            'void(int,float,float,float,float,float*,float*)'],
        'scope': 'Static installed evidence; live scoped appearance unobserved'}, indent=2))


if __name__ == '__main__': main()
