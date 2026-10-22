"""Verify installed current-weapon spread, recoil, readiness and distribution contracts.
Reads installed files only; no client process, launch or writes. Standard library only.
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
SCHEMA = {'C_BasePlayerPawn': (35603848, 35602928, 28, {'m_pWeaponServices': 4848}),
 'CPlayer_WeaponServices': (35831240, 35831088, 4, {'m_hActiveWeapon': 96}),
 'CCSPlayer_WeaponServices': (35792008, 35791824, 5, {'m_flNextAttack': 208}),
 'C_CSWeaponBase': (35761304,
                    35762048,
                    57,
                    {'m_weaponMode': 6656, 'm_flRecoilIndex': 6696, 'm_bInReload': 6716, 'm_bBurstMode': 6700}),
 'C_CSWeaponBaseGun': (35766088, 35764992, 7, {'m_iBurstShotsRemaining': 7972}),
 'C_BasePlayerWeapon': (35798840,
                        35798592,
                        7,
                        {'m_iClip1': 6440, 'm_nNextPrimaryAttackTick': 6424, 'm_flNextPrimaryAttackTickRatio': 6428}),
 'CCSWeaponBaseVData': (35760872,
                        35757952,
                        83,
                        {'m_nNumBullets': 1840,
                         'm_bIsFullAuto': 1837,
                         'm_flRange': 2096,
                         'm_bIsRevolver': 1822,
                         'm_flSpread': 1872}),
 'C_CSPlayerPawn': (35887720,
                    35884304,
                    105,
                    {'m_pAimPunchServices': 5528, 'm_bIsDefusing': 7834, 'm_bWaitForNoAttack': 7868, 'm_iShotsFired': 7856}),
 'C_CSPlayerPawnBase': (35882168, 35887856, 26, {'m_iPlayerState': 5348}),
 'C_CSGameRules': (35727992, 35724832, 98, {'m_bFreezePeriod': 64}),
 'C_BaseEntity': (35605480, 35599040, 82, {'m_fFlags': 1012}),
 'CCSPlayer_AimPunchServices': (35779480,
                                35779264,
                                6,
                                {'m_predictableBaseTick': 72,
                                 'm_predictableBaseTickInterpAmount': 76,
                                 'm_predictableBaseAngle': 80,
                                 'm_predictableBaseAngleVel': 92,
                                 'm_unpredictableBaseTick': 160,
                                 'm_unpredictableBaseAngle': 164})}
TABLES = {'C_AK47': 29219872,
 'C_WeaponAug': 29223688,
 'C_WeaponAWP': 29227416,
 'C_WeaponBizon': 29231144,
 'C_WeaponFamas': 29234872,
 'C_WeaponFiveSeven': 29238600,
 'C_WeaponG3SG1': 29242328,
 'C_WeaponGalilAR': 29246056,
 'C_WeaponGlock': 29249784,
 'C_WeaponHKP2000': 29253512,
 'C_WeaponUSPSilencer': 29257240,
 'C_WeaponM4A1': 29260984,
 'C_WeaponM4A1Silencer': 29264712,
 'C_WeaponMAC10': 29268456,
 'C_WeaponMP5SD': 29275912,
 'C_WeaponMP7': 29279640,
 'C_WeaponMP9': 29283368,
 'C_WeaponNegev': 29287096,
 'C_WeaponP250': 29290824,
 'C_WeaponP90': 29294552,
 'C_WeaponSCAR20': 29298280,
 'C_WeaponSG556': 29302008,
 'C_WeaponSSG08': 29305736,
 'C_WeaponTec9': 29309464,
 'C_WeaponUMP45': 29313192,
 'C_WeaponM249': 29316920,
 'C_WeaponCZ75a': 29340064,
 'C_DEagle': 29343888,
 'C_WeaponElite': 29354528}
WINDOWS = {'spread_getter': (8443024, 43, '25fe2c8f08c83dfa604e353a2324870533fb2f5ca497ba181eb48215cbe42561'),
 'current_inaccuracy': (8438176, 796, '6a6ae4b60028839f258f6927849a4b7ecc45cdcb254de1492df3f7a80266bce6'),
 'primary_cooldown': (9756256, 166, 'f92dede76bb2daabc5a6df6c9c8254f7c159d59c7f47ea4d51d3a3ca893d5963'),
 'current_time': (7721552, 120, '6cb2a9bf9925d40b5085e184f543aa69464b42ea67e5f78fabb35e533d655367'),
 'native_punch_current_query': (8726192, 219, 'a3b2fcbed5782c0e86ef381e8bbb46d6f41ff29e41ad1b5a7bb0091d180eeb0a'),
 'native_punch_time_resolution': (8726624, 304, '88767ba59ec224e2c7db3c599f3f34783b85289d0054f550c89590dd9e0d7334'),
 'native_punch_evaluator_and_memoization': (8706224, 2446, 'c3709d2b8f64d138297bb70a5942d55c389353e3eba2b8bdd1d90bd2471ec1d2'),
 'native_punch_shot_query': (8725984, 206, '48cfdeb4f95611258a9e7cccd36252dc2ab1fef846875b319264ebcdd8f59bd1'),
 'shot_punch_add': (8446776, 73, '91c99d54d6b1f68428b72812f4bde6756fb1a48a4baee3470b63aa4bdcedf8c7'),
 'current_punch_pawn_wrapper': (9226784, 30, 'dce07ab01bb46e80a2a5d0d2d4cba639038eb5d801e75e4c39c12b4f7c06b275'),
 'shot_native_model_inputs': (8404283, 223, '480c577d48ac3a71215b73c42703ce5cec8fd74d9a1574b554c6912b1f8b86af'),
 'shot_spread_distribution': (13778912, 1420, '3f2193f622797106ae1f692f98951cc4449294241bf888c954e69e9aa53b3079'),
 'shot_single_projectile_spread_fallback': (8182811, 138, 'f9109fab92aef397a90015c2a819ab618d88def20018d894d929f4232c670111'),
 'shotgun_disabled_for_single_projectile': (8172050, 39, 'e6f643801a3c28af6cf4cf5af7c1edffe93fe9c293483c2b8754496c3d2f41b7'),
 'service_deadline_consumer': (9447519, 102, '5db53701b0a8c9b034b069251e6239fb08d496800e536526de79922cc9abfdf1'),
 'primary_action_readiness': (8486718, 208, '4eb43a8721a34fec78889a44b52c4a1ffd55f423efe1aa7b776ccb3d7c103c3a'),
 'debug_max_radius_registration': (1006243, 128, 'a4b4ce00b9487c34c81d733f03e1e97c3c7d7ed0d1414da7d5b6511cd2efc2b7'),
 'debug_fixed_angle_registration': (1006067, 128, 'be127ac6f10ac3bb80443f00cef3d3d01a4f1a5cfa455828e4442e8b8462090c'),
 'shot_angle_basis': (0x1742AF0, 0x178, '23326b17d2cb04023d27e7a6bad00260c72723bff6590387d7e55b1a12b13e1f'),
 'shot_direction_and_range': (0x887D8E, 0x197, '6b5153ceb26173cc21d8d143d31b4389a022271cc89294f40f006a8e5b1feb19')}


def verify(image, base, nt, raw, header):
    def u32(at): return struct.unpack_from('<I', image, at)[0]
    def ptr(at): return struct.unpack_from('<Q', image, at)[0] - base
    def string(at):
        if not 0 < at < len(image): raise SystemExit('Invalid string pointer')
        end = image.find(0, at, min(at + 1024, len(image)))
        if end < 0: raise SystemExit('Unterminated evidence string')
        return bytes(image[at:end]).decode('ascii')
    def rip(at): return at + 7 + struct.unpack_from('<i', image, at + 3)[0]
    def call(at):
        if image[at] != 0xE8: raise SystemExit(f'Expected direct call at {at:#x}')
        return at + 5 + struct.unpack_from('<i', image, at + 1)[0]
    fields = {}
    for name, (registration, array, count, wanted) in SCHEMA.items():
        if string(ptr(registration)) != name or ptr(registration + 0x28) != array or struct.unpack_from('<H', image, registration + 0x1C)[0] != count:
            raise SystemExit(f'Changed named schema registration: {name}')
        actual = {string(ptr(array + i*32)): u32(array + i*32 + 16) for i in range(count)}
        if any(actual.get(field) != value for field, value in wanted.items()):
            raise SystemExit(f'Changed named schema field: {name}')
        fields[name] = wanted
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at+size]).hexdigest() != digest:
            raise SystemExit(f'Changed native weapon contract: {name}')
    for name, table in TABLES.items():
        if primary_vtable(image, base, ('.?AV'+name+'@@').encode()) != table:
            raise SystemExit(f'Changed weapon RTTI ownership: {name}')
        if ptr(table + 0xBD0) != 0x80D490 or ptr(table + 0xCF8) != 0x80C1A0:
            raise SystemExit(f'Changed native spread/inaccuracy slots: {name}')
    schema_constants = {
        'WeaponServices': ('C_BasePlayerPawn', 'm_pWeaponServices'),
        'ActiveWeapon': ('CPlayer_WeaponServices', 'm_hActiveWeapon'),
        'NextAttack': ('CCSPlayer_WeaponServices', 'm_flNextAttack'),
        'Mode': ('C_CSWeaponBase', 'm_weaponMode'), 'RecoilIndex': ('C_CSWeaponBase', 'm_flRecoilIndex'),
        'InReload': ('C_CSWeaponBase', 'm_bInReload'), 'BurstMode': ('C_CSWeaponBase', 'm_bBurstMode'),
        'BurstRemaining': ('C_CSWeaponBaseGun', 'm_iBurstShotsRemaining'),
        'Clip': ('C_BasePlayerWeapon', 'm_iClip1'), 'NumBullets': ('CCSWeaponBaseVData', 'm_nNumBullets'),
        'FullAuto': ('CCSWeaponBaseVData', 'm_bIsFullAuto'), 'Range': ('CCSWeaponBaseVData', 'm_flRange'),
        'IsRevolver': ('CCSWeaponBaseVData', 'm_bIsRevolver'),
        'PunchServices': ('C_CSPlayerPawn', 'm_pAimPunchServices'), 'Defusing': ('C_CSPlayerPawn', 'm_bIsDefusing'),
        'WaitForNoAttack': ('C_CSPlayerPawn', 'm_bWaitForNoAttack'), 'ShotsFired': ('C_CSPlayerPawn', 'm_iShotsFired'),
        'PlayerState': ('C_CSPlayerPawnBase', 'm_iPlayerState'), 'FreezePeriod': ('C_CSGameRules', 'm_bFreezePeriod'),
        'Flags': ('C_BaseEntity', 'm_fFlags'),
    }
    expected = {name: fields[owner][field] for name, (owner, field) in schema_constants.items()}
    expected.update({
        'Spread': ptr(TABLES['C_AK47'] + 0xBD0), 'Inaccuracy': ptr(TABLES['C_AK47'] + 0xCF8),
        'PrimaryCooldown': call(0x817F56), 'CurrentTime': call(0x902879), 'AimPunch': call(0x8CCA30),
        'SpreadSlot': u32(0x803D43), 'InaccuracySlot': u32(0x803D57),
        'Data': u32(0x80D49A), 'ServicePawn': image[0x8527AC], 'IdentityScene': image[0x94DE74],
        'ServicesVtable': primary_vtable(image, base, b'.?AVCCSPlayer_WeaponServices@@'),
        'PunchVtable': primary_vtable(image, base, b'.?AVCCSPlayer_AimPunchServices@@'),
        'GameRules': rip(0x817FA0), 'DebugMaxRadius': rip(0xF5B17), 'DebugFixedAngle': rip(0xF5A67),
        'CvarValuePointer': 8, 'CvarValue': image[0xD24028],
    })
    if rip(0xD2400E) != expected['DebugMaxRadius'] + expected['CvarValuePointer'] or rip(0xD24029) != expected['DebugFixedAngle'] + 8:
        raise SystemExit('Debug wrapper constructor/native-consumer disagreement')
    if string(rip(0xF5B10)) != 'weapon_debug_max_inaccuracy' or string(rip(0xF5A60)) != 'weapon_debug_inaccuracy_only_up':
        raise SystemExit('Changed debug distribution registration names')
    if u32(0x8CCA29) != expected['PunchServices'] or u32(0x80D493) != expected['Mode'] or u32(0x80D4AD) != fields['CCSWeaponBaseVData']['m_flSpread']:
        raise SystemExit('Independent native data/punch operands disagree with schema')
    if string(ptr(rip(0xD244DB))) != 'weapon_revolver' or string(ptr(rip(0xD24536))) != 'weapon_negev':
        raise SystemExit('Changed special spread item-name identities')
    for at, value in [(0x1AC6074, 1.0), (0x1AC607C, 3.0), (0x1C0E8CC, 1.5707963705062866), (0x1B06564, 6.2831854820251465)]:
        if struct.unpack_from('<f', image, at)[0] != value: raise SystemExit('Changed native radial/angle constant')
    # Bind the native draws and trigonometry to named imports, not guessed ABI.
    imports = {}
    descriptors = struct.unpack_from('<I', raw, nt + 24 + 112 + 8)[0]
    for descriptor in range(descriptors, descriptors + 8192, 20):
        original, _, _, name, iat = struct.unpack_from('<IIIII', image, descriptor)
        if not original and not iat: break
        original = original or iat
        for i in range(10000):
            target = struct.unpack_from('<Q', image, original+i*8)[0]
            if not target: break
            imports[iat+i*8] = (string(name), '#'+str(target&0xFFFF) if target>>63 else string(target+2))
    required_imports = {0x1AACEA8: 'RandomSeed', 0x1AAD678: 'RandomFloat', 0x1AAD8B0: 'V_cosf', 0x1AAD8A8: 'V_sinf'}
    if any(imports.get(at) != ('tier0.dll', name) for at, name in required_imports.items()):
        raise SystemExit('Changed native RNG or trigonometry imports')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in expected.items():
        matches = re.findall(rf'\b{name}\s*=\s*(0[xX][\da-fA-F]+|\d+)', source)
        if len(matches) != 1 or int(matches[0], 0) != value:
            raise SystemExit(f'Header {name} differs from independently decoded native evidence')
    prefixes = {'SpreadBytes': ('Spread',43), 'InaccuracyBytes': ('Inaccuracy',33),
        'PrimaryCooldownBytes': ('PrimaryCooldown',26), 'CurrentTimeBytes': ('CurrentTime',15), 'AimPunchBytes': ('AimPunch',27)}
    for name, (target, size) in prefixes.items():
        match = re.search(rf'\b{name}\[\]\s*\{{([^}}]*)\}}', source, re.S)
        copied = bytes(int(v.strip(),0) for v in match[1].split(',') if v.strip()) if match else b''
        if copied != image[expected[target]:expected[target]+size]: raise SystemExit(f'Header {name} differs')
    records = [(int(at,0),name,negev=='true') for at,name,negev in re.findall(r'\{(0x[\da-fA-F]+),\s*"([^"]+)",\s*(true|false)\}',source)]
    if records != [(at,name,name=='C_WeaponNegev') for name,at in TABLES.items()]:
        raise SystemExit('Header supported weapon tables or Negev policy differ')
    return expected, fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll'))
    parser.add_argument('--header', type=Path, default=ROOT/'include/aim_weapon_offsets.h')
    args = parser.parse_args()
    raw, image, base, nt = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH: raise SystemExit('Client changed: repeat current-weapon investigation')
    constants, fields = verify(image, base, nt, raw, args.header)
    print(json.dumps({'client_sha256': digest, 'constants': {name:hex(value) for name,value in constants.items()},
        'schema': fields, 'supported_weapon_tables': TABLES, 'native_windows': WINDOWS,
        'model': 'Two independent uniform radial samples with independent uniform angles; Negev shaping; native debug distribution overrides',
        'punch': 'Native current-prediction query; engine may refresh its own memoization caches; no direct recoil-state writes',
        'scope': 'Static installed evidence; no live weapon accuracy, shot receipt or hit-registration claim'}, indent=2))


if __name__ == '__main__': main()
