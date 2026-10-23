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
EXPECTED_HASH = 'ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5'
SCHEMA = {
    'CBasePlayerController': (0x2225c98, 0x2225a60, 17, {'m_iDesiredFOV': 0x794}),
    'C_BasePlayerPawn': (0x21f67e8, 0x21f6450, 28, {'m_pWeaponServices': 0x12F0,
        'm_flFOVSensitivityAdjust': 0x149C, 'm_flMouseSensitivity': 0x14A0}),
    'C_CSPlayerPawn': (0x223bac8, 0x223ad40, 105, {'m_bIsScoped': 0x1EA0}),
    'CPlayer_WeaponServices': (0x222ddc8, 0x222dd30, 4, {'m_hActiveWeapon': 0x60}),
    'C_CSWeaponBaseGun': (0x221e168, 0x221dd20, 7, {'m_zoomLevel': 0x1F20}),
    'CCSWeaponBaseVData': (0x221cd08, 0x221c1a0, 83, {'m_nZoomLevels': 0x7F4, 'm_nZoomFOV1': 0x7F8, 'm_nZoomFOV2': 0x7FC}),
}
TABLES = {
    'C_WeaponAug': 0x1be0a98, 'C_WeaponAWP': 0x1be1928,
    'C_WeaponG3SG1': 0x1be5368, 'C_WeaponSCAR20': 0x1bf2df8,
    'C_WeaponSG556': 0x1bf3c88, 'C_WeaponSSG08': 0x1bf4b18,
}
WINDOWS = {
    'normal_view': (0xd04490, 0x347, '73d89ec002d0381a9489ed106355b5092cea747f654b451828bdbf56388f4cfc'),
    'baseline_getter': (0x940a70, 0x2A, 'c0a7b6a82dc42b4a58bb56392630bcd1ece22de81a2045f2838ede332e9f7965'),
    'camera_fov_interpolation': (0x88b460, 0x1B9, 'ea9a5d647bce1a74ea42dfc544c2145529a645ab612a5add9af1f5414af4161d'),
    'camera_baseline': (0xa68ff0, 0xC6, 'd9378b09c514fe4d01562bdf03a14e2af17c995f5a69063f8a10eb332ca946d6'),
    'view_fov_producer': (0xb9d856, 0xFB, '189b552b6187cc6a03d075f364d3083a98eb470acf4f285ce9d423053204fcb2'),
    'projection_consumer': (0xb52c6d, 0x86, '4d9f237806bd06a9fb3e648e4adc9401b59465570d8d1b1687785ae38b4194c3'),
    'weapon_fov_selection': (0x80f250, 0x70, '92043a52340becfd3050af85f3f2f3fa6484bc20ba94cae345257215923fdbc5'),
    'zoom_state_cycle': (0x8256de, 0x8E, '3a531d5f442d174d4672e4cbda396c0ee9984f5eabadca1d1f198b0501cba1f4'),
    'zoom_state_consumer': (0x82a04c, 0x3D, 'c57dab89226b1ff96914e1479a05cab10cfac7af8691301cf5e1c66aecd79f1d'),
    'mouse_input_caller': (0xb663a0, 0x12F, '24765e122f216c788614036c8587e4f5b0565f21c4a03e14a877d934bb171e1b'),
    'mouse_scale_identity': (0xb80fc0, 4, '2fe08174d918f72b8cbcdf7f2affed11f88d44ad2929886406754774bd9e36be'),
    'mouse_scope_dispatch': (0xba75f0, 0x28C, '5a1cbc2af6cfb2b4fbbf81c25371909090481573df27a981f709b3d9f26d75bf'),
    'mouse_convert': (0xba7d10, 0x12A, 'a0b7adf0f18d899c0c5200adf43dd6a3bff72bd906d9c73375d7a23343746c60'),
    'mouse_convert_alternate': (0xba7b40, 0x1C1, '632348576c80825498732f5f0b5b3c751b2cb3f3c52c81f57bedc54e33e04603'),
    'pawn_sensitivity_producer': (0xb23610, 0xEA, '49535a11b5a08b08bcae34b8e5d06c6580ec2971016f32df9b35876e2b3c326f'),
    'base_sensitivity_registration': (0xd81ee, 0x2A, 'ab3ad53b0ee6d88085600e923bb61225487c935a6382e5b86d72b2d2b80a89e8'),
    'zoom_ratio_registration': (0xc957e, 0x2A, '8ef3451db60b7c51e6baa800a507b47fee57e9af2ca9d6b84aec8ea97bdbff71'),
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
        if ptr(table + 0xBF0) != 0x80f250:
            raise SystemExit(f'Changed weapon zoom FOV method: {name}')
    table = primary_vtable(image, base, b'.?AVClientModeCSNormal@@')
    if table != 0x1ca1b50 or ptr(table + 16*8) != 0xd04490:
        raise SystemExit('Changed final normal-mode view slot')
    input_table = primary_vtable(image, base, b'.?AVCCSGOInput@@')
    if input_table != 0x1c9fa58 or ptr(input_table + 24*8) != 0xb80fc0:
        raise SystemExit('Changed native mouse pre-scope scale virtual slot')
    if rip(0xee58c, 7, 3) != input_table or rip(0xee574, 7, 3) != rip(0xee59a, 7, 3):
        raise SystemExit('Changed embedded input constructor contract')
    if u32(rip(0xb66409, 8, 4)) != 0x3F800000 or call(0xb66446) != 0xba75f0:
        raise SystemExit('Changed exact unscoped mouse scale of 1.0')
    if string(rip(0xd81ee, 7, 3)) != 'sensitivity' or string(rip(0xc957e, 7, 3)) != 'zoom_sensitivity_ratio':
        raise SystemExit('Changed native base/zoom sensitivity cvar registrations')
    if rip(0xd81fa, 7, 3) != rip(0xba7d2a, 7, 3) or rip(0xc958a, 7, 3) != rip(0xb2369d, 7, 3):
        raise SystemExit('Changed native sensitivity cvar producer/consumer binding')
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at+size]).hexdigest() != digest:
            raise SystemExit(f'Changed native window: {name}')
    # Independently decoded producer/consumer operands, in addition to named schema.
    expected = {
        'OverrideView': ptr(table + 16*8), 'ModeVtable': table,
        'ModeSlot': image[0xb973b2], 'ViewFov': u32(0xb52c71),
        'BaselineFov': 0x940a70, 'DesiredFov': u32(0x940a76),
        'WeaponServices': fields['C_BasePlayerPawn']['m_pWeaponServices'],
        'ActiveWeapon': fields['CPlayer_WeaponServices']['m_hActiveWeapon'],
        'Scoped': fields['C_CSPlayerPawn']['m_bIsScoped'], 'ZoomLevel': u32(0x82a052+2),
        'MouseConvert': call(0xba7774), 'MouseConvertAlternate': call(0xba778c),
        'MouseInput': rip(0xee574, 7, 3), 'MouseInputVtable': input_table,
        'MouseScale': ptr(input_table + 24*8), 'MouseCaller': 0xb66403,
        'MouseScaleUnit': rip(0xb66409, 8, 4),
        'MouseScopeDispatch': 0xba771a, 'FovSensitivity': u32(0xba7726+4),
    }
    if expected['ViewFov'] != u32(0xd046ff+4) or expected['ZoomLevel'] != fields['C_CSWeaponBaseGun']['m_zoomLevel']:
        raise SystemExit('Independent FOV/zoom operands disagree')
    if expected['FovSensitivity'] != fields['C_BasePlayerPawn']['m_flFOVSensitivityAdjust']:
        raise SystemExit('Independent native mouse scale member disagrees with schema')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in expected.items():
        matches = re.findall(rf'\b{name}\s*=\s*(0[xX][\da-fA-F]+|\d+)', source)
        if len(matches) != 1 or int(matches[0],0) != value:
            raise SystemExit(f'Header {name} differs from native evidence')
    for name, at, size in [('OverrideViewBytes',0xd04490,15), ('BaselineFovBytes',0x940a70,42),
            ('MouseConvertBytes',0xba7d10,16), ('MouseConvertAlternateBytes',0xba7b40,19),
            ('MouseScaleBytes',0xb80fc0,4), ('MouseCallerBytes',0xb66403,0x48),
            ('MouseScaleUnitBytes',0x1ac8084,4),
            ('MouseScopeDispatchBytes',0xba771a,0x77)]:
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
