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
EXPECTED_HASH = 'ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5'
SCHEMA = {'C_BasePlayerPawn': (0x21F67E8, 0x21F6450, 28, {'m_pWeaponServices': 4848}),
 'CPlayer_WeaponServices': (0x222DDC8, 0x222DD30, 4, {'m_hActiveWeapon': 96}),
 'CCSPlayer_WeaponServices': (0x2224758, 0x2224670, 5, {'m_flNextAttack': 208}),
 'C_CSWeaponBase': (0x221CEB8,
                    0x221D1B0,
                    57,
                    {'m_weaponMode': 6656,
                     'm_flRecoilIndex': 6696,
                     'm_bInReload': 6716,
                     'm_bBurstMode': 6700}),
 'C_CSWeaponBaseGun': (0x221E168, 0x221DD20, 7, {'m_iBurstShotsRemaining': 7972}),
 'C_BasePlayerWeapon': (0x22261B8,
                        0x22260C0,
                        7,
                        {'m_iClip1': 6440,
                         'm_nNextPrimaryAttackTick': 6424,
                         'm_flNextPrimaryAttackTickRatio': 6428}),
 'CCSWeaponBaseVData': (0x221CD08,
                        0x221C1A0,
                        83,
                        {'m_nNumBullets': 1840,
                         'm_bIsFullAuto': 1837,
                         'm_flRange': 2096,
                         'm_bIsRevolver': 1822,
                         'm_flSpread': 1872}),
 'C_CSPlayerPawn': (0x223BAC8,
                    0x223AD40,
                    105,
                    {'m_pAimPunchServices': 5528,
                     'm_bIsDefusing': 7842,
                     'm_bWaitForNoAttack': 7876,
                     'm_iShotsFired': 7864, 'm_bIsWalking': 7808}),
 'C_CSPlayerPawnBase': (0x223A4D8, 0x223BB80, 26, {'m_iPlayerState': 5348}),
 'C_CSGameRules': (0x2214C38, 0x2213F50, 98, {'m_bFreezePeriod': 64}),
 'C_BaseEntity': (0x21F6E78, 0x21F5550, 82, {'m_fFlags': 1012}),
 'CCSPlayer_AimPunchServices': (0x2221398,
                                0x22212C0,
                                6,
                                {'m_predictableBaseTick': 72,
                                 'm_predictableBaseTickInterpAmount': 76,
                                 'm_predictableBaseAngle': 80,
                                 'm_predictableBaseAngleVel': 92,
                                 'm_unpredictableBaseTick': 160,
                                 'm_unpredictableBaseAngle': 164})}
TABLES = {'C_AK47': 0x1BDFBB0,
 'C_WeaponAug': 0x1BE0A98,
 'C_WeaponAWP': 0x1BE1928,
 'C_WeaponBizon': 0x1BE27B8,
 'C_WeaponFamas': 0x1BE3648,
 'C_WeaponFiveSeven': 0x1BE44D8,
 'C_WeaponG3SG1': 0x1BE5368,
 'C_WeaponGalilAR': 0x1BE61F8,
 'C_WeaponGlock': 0x1BE7088,
 'C_WeaponHKP2000': 0x1BE7F18,
 'C_WeaponUSPSilencer': 0x1BE8DA8,
 'C_WeaponM4A1': 0x1BE9C48,
 'C_WeaponM4A1Silencer': 0x1BEAAD8,
 'C_WeaponMAC10': 0x1BEB978,
 'C_WeaponMP5SD': 0x1BED698,
 'C_WeaponMP7': 0x1BEE528,
 'C_WeaponMP9': 0x1BEF3B8,
 'C_WeaponNegev': 0x1BF0248,
 'C_WeaponP250': 0x1BF10D8,
 'C_WeaponP90': 0x1BF1F68,
 'C_WeaponSCAR20': 0x1BF2DF8,
 'C_WeaponSG556': 0x1BF3C88,
 'C_WeaponSSG08': 0x1BF4B18,
 'C_WeaponTec9': 0x1BF59A8,
 'C_WeaponUMP45': 0x1BF6838,
 'C_WeaponM249': 0x1BF76C8,
 'C_WeaponCZ75a': 0x1BFD130,
 'C_DEagle': 0x1BFE020,
 'C_WeaponElite': 0x1C009B0}
WINDOWS = {
    'spread_getter': (0x80E2C0, 43, '25fe2c8f08c83dfa604e353a2324870533fb2f5ca497ba181eb48215cbe42561'),
    'current_inaccuracy': (0x80CFD0, 796, '1054ae1b18c90cab613e7536aaed31318e24e381c2ce5084d3c36fef421cfa9e'),
    'primary_cooldown': (0x94EC90, 166, '88e672f4119eb12cfa6e0b44b43d514b4c24fc9326e7b519de0343aeb3e6203e'),
    'current_time': (0x75E050, 120, 'b874b2f990e5f2f34b82bf517732cc02955b91237c91b8de3b3628bbdbc29990'),
    'native_punch_current_query': (0x8534E0, 219, 'f01208c234523476f5ccd199fb5838fd0b7954f7e38ff65a84acb438c2b18fe2'),
    'native_punch_time_resolution': (0x853690, 304, '836723ca9d4b2d3b9b264e88aabad9f2f05c2d6b2b205c3ea6fe1859e9799b7a'),
    'native_punch_evaluator_and_memoization': (0x84E6E0, 2446, '5bb435e5c457bebbee2b449f6be708f469f836e21d84086978853f7cda7ac8ac'),
    'native_punch_shot_query': (0x853410, 206, '8607d641d2416e400811a15c3336cf9257d957b0d5ed613ebc863b0de78288ef'),
    'shot_punch_add': (0x80F168, 73, '91c99d54d6b1f68428b72812f4bde6756fb1a48a4baee3470b63aa4bdcedf8c7'),
    'current_punch_pawn_wrapper': (0x8CD850, 30, 'dce07ab01bb46e80a2a5d0d2d4cba639038eb5d801e75e4c39c12b4f7c06b275'),
    'shot_native_model_inputs': (0x804B6B, 223, '12326217c2839b9ce3e2c2fac780b95b999126d59fe94f54b133d53acbfc2059'),
    'shot_spread_distribution': (0xD25630, 1420, 'ad1abc396b82ec66dba50abd83371b0b2b9db84d718c3d044e241f80c90ff349'),
    'shot_single_projectile_spread_fallback': (0x7CEA70, 198, '0b5fb705e53eac1458d052ec676bd6f4d4f6746262915d3d3868f6168e664901'),
    'shotgun_disabled_for_single_projectile': (0x7CC1C0, 84, '8afc878c42c1c55fc9a711a71fac0f4f660ce6a81ab0a29db9010103d26717c7'),
    'service_deadline_consumer': (0x90368F, 102, '98d998f369b1398db9ff5e7313944ed908a09e864d6deb5e083b8427763ded3c'),
    'primary_action_readiness': (0x818D6E, 208, '6ec4c95150ba77b3d98401222b02d380a8d4e316c79c80dc9ba593214630aaba'),
    'debug_max_radius_registration': (0xF5AA3, 128, '351b4a81aa7182349e8be4b22b1e42606660f7a145d196f32f4f3492ec9a8b75'),
    'debug_fixed_angle_registration': (0xF59F3, 128, '48234ce43615a743255f283d870302c86f4313418feee08bf9f68d10c3ba38ef'),
    'shot_angle_basis': (0x1744D50, 376, '16f0b386c892cc0bc0351e22847c248a406a1df756acca385043ba43b3f6e4b9'),
    'shot_direction_and_range': (0x888BBE, 407, '60509e558ab57b90dd13934c7072b807af3555dca52456707b95e62d287f9d4e'),
}


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
        if ptr(table + 0xBD0) != 0x80E2C0 or ptr(table + 0xCF8) != 0x80CFD0:
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
        'PrimaryCooldown': call(0x818D86), 'CurrentTime': call(0x9036A9), 'AimPunch': call(0x8CD860),
        'SpreadSlot': u32(0x804B73), 'InaccuracySlot': u32(0x804B87),
        'Data': u32(0x80E2CA), 'ServicePawn': image[0x8535DC], 'IdentityScene': image[0x94ECA4],
        'ServicesVtable': primary_vtable(image, base, b'.?AVCCSPlayer_WeaponServices@@'),
        'PunchVtable': primary_vtable(image, base, b'.?AVCCSPlayer_AimPunchServices@@'),
        'GameRules': rip(0x818DD0), 'DebugMaxRadius': rip(0xF5B17), 'DebugFixedAngle': rip(0xF5A67),
        'CvarValuePointer': 8, 'CvarValue': image[0xD25678],
    })
    if rip(0xD2565E) != expected['DebugMaxRadius'] + expected['CvarValuePointer'] or rip(0xD25679) != expected['DebugFixedAngle'] + 8:
        raise SystemExit('Debug wrapper constructor/native-consumer disagreement')
    if string(rip(0xF5B10)) != 'weapon_debug_max_inaccuracy' or string(rip(0xF5A60)) != 'weapon_debug_inaccuracy_only_up':
        raise SystemExit('Changed debug distribution registration names')
    if u32(0x8CD859) != expected['PunchServices'] or u32(0x80E2C3) != expected['Mode'] or u32(0x80E2DD) != fields['CCSWeaponBaseVData']['m_flSpread']:
        raise SystemExit('Independent native data/punch operands disagree with schema')
    if string(ptr(rip(0xD25B2B))) != 'weapon_revolver' or string(ptr(rip(0xD25B86))) != 'weapon_negev':
        raise SystemExit('Changed special spread item-name identities')
    for at, value in [(0x1AC8084, 1.0), (0x1AC808C, 3.0), (0x1C1085C, 1.5707963705062866), (0x1B085A4, 6.2831854820251465)]:
        if struct.unpack_from('<f', image, at)[0] != value: raise SystemExit('Changed native radial/angle constant')
    # The update changed pawn walking/readiness members and recompiled the
    # cached shotgun helper. Tie these operands to named schema and native
    # callers, rather than accepting entry similarity or a relocation delta.
    pawn_fields = fields['C_CSPlayerPawn']
    if (u32(0x80D0C9) != pawn_fields['m_bIsWalking'] or
            u32(0x818DE3) != expected['Defusing'] or
            u32(0x818E00) != expected['ShotsFired'] or
            u32(0x818E33) != expected['WaitForNoAttack']):
        raise SystemExit('Native walking/readiness operands disagree with pawn schema')
    if (call(0xD258D6) != 0x7CEA70 or call(0x7CEAA5) != 0x7CC0E0 or
            call(0x7CC1BB) != 0x7CC330 or call(0x7CC1E8) != 0x378D80 or
            u32(0x7CC1F8) != expected['NumBullets']):
        raise SystemExit('Changed native single-projectile fallback/initializer chain')
    # FF /2 RIP-indirect calls have six bytes; both draws use named RandomFloat.
    draws = [at + 6 + struct.unpack_from('<i', image, at + 2)[0]
             for at in (0x7CEAFD, 0x7CEB17)]
    if draws != [0x1AAF670, 0x1AAF670]:
        raise SystemExit('Single-projectile fallback no longer uses the two native uniform draws')
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
    required_imports = {0x1AAEEA0: 'RandomSeed', 0x1AAF670: 'RandomFloat', 0x1AAF8A8: 'V_cosf', 0x1AAF8A0: 'V_sinf'}
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
