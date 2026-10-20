"""Verify direct bullet damage contracts against installed AMD64 files.

Read-only, standard library only; no process opened, game launched or writes to
installed files. This does not establish actual server damage or shot outcomes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import DEFAULT, mapped, primary_vtable

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_HASH = '659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0'
ANIMATION_HASH = 'fb2b4cd71468e880c3b3845e8e7a18c9d550dd3c4504e4a257a2110fb91c1235'
SCHEMA = {
    'CCSWeaponBaseVData': (0x221AAE8, 0x2219F80, 83,
        {'m_nDamage': 0x820, 'm_flHeadshotMultiplier': 0x824, 'm_flArmorRatio': 0x828, 'm_flRangeModifier': 0x834}),
    'C_CSPlayerPawn': (0x2239A68, 0x2238D10, 105,
        {'m_ArmorValue': 0x1EC8, 'm_bGunGameImmunity': 0x3508, 'm_flHealthShotBoostExpirationTime': 0x15A8}),
    'C_BasePlayerPawn': (0x21F4588, 0x21F41F0, 28, {'m_pItemServices': 0x12F8}),
    'CCSPlayer_ItemServices': (0x2220EC8, 0x2220E70, 2, {'m_bHasHelmet': 0x49}),
}
CONSTANTS = {'Damage': 2080,
 'HeadshotMultiplier': 2084,
 'ArmorRatio': 2088,
 'RangeModifier': 2100,
 'ArmorValue': 7880,
 'ItemServices': 4856,
 'HasHelmet': 73,
 'ItemServicesVtable': 29429200,
 'ServicePawn': 56,
 'GunGameImmunity': 13576,
 'DamageScaleCtBody': 38521464,
 'DamageScaleCtHead': 38521480,
 'DamageScaleTBody': 38521496,
 'DamageScaleTHead': 38521512,
 'HeadshotOnly': 38521448,
 'CvarValuePointer': 8,
 'CvarValue': 88,
 'TraceHitbox': 16,
 'HitboxGroup': 56,
 'UnknownHitboxPointer': 41106880,
 'HealthShotBoostExpiration': 5544}
WINDOWS = {'WeaponDamageInputs': (13777731, 41, '0ac47a99a7bd3c08a57543e4ac67262932711193a69a474c223a726ff0e8eaba'),
 'DamageScales': (9034225, 72, '8e6f04d41d7af873067c487f7467f573dac3726ca2e35e2e44ed569eb9261297'),
 'HeadAndBodyScale': (9034568, 204, '3352a8992a6b576b6fe7141e616e901fa35f0f87e99ffa5b458a1af913e1e3d0'),
 'ScaleJumpTable': (9035440, 32, 'a99b2cdadb585496cd6fd31cb5a5c28861dae0933b42b880f40ff1eddb38bc21'),
 'ArmorCoverage': (9276656, 116, '6317ef95232772cbb6110dd551df839f639e8450344c352819b5cd2fbaac9125'),
 'ArmorRatioGetter': (8929974, 12, '92bd0b76e4ababda336f9a3cb04e6a34d6984d73413948a246de3c1ecc228f64'),
 'ArmorCalculation': (9044050, 107, '50080077d7cd16adcfeeb280d86774cd87f6373b21c8f4dee644520d34b2b49b'),
 'RangeFalloff': (9050237, 40, '6b48bb6ba4cd928b31766bf86a27f894f5a9099f3724e4fd73238bc66aedce93'),
 'TraceHitgroup': (11198272, 64, '921bf1ff2e3a4f8b08266e296832f4e73ae4a140542a71f0eabc95a8542a37ac'),
 'TraceUnknownHitbox': (24614240, 15, '93a7118a9740163c3cbb16da32f9d0ac2b6c0c5d449d572f718662e4bfabcaae'),
 'CtBodyRegistration': (547744, 144, '371d450e3421106fce2ef1d67bb70f4ceeff80430bfe0c32680d3189dc63682b'),
 'CtHeadRegistration': (547904, 144, 'c377a6af63a2514722c556b14a4a59045bee0054a8b1e3e6fabfe2793740e51f'),
 'TBodyRegistration': (548064, 144, '00cdc74e6bd88ffddf5d0ccac4dbf05057df369b2b73cf2e4b59531f63e3647c'),
 'THeadRegistration': (548224, 144, 'b023062daca0501dfefac0c3cf19f37bf6bafc8b731ed6391ea45aa2ba783aa7'),
 'HeadshotOnlyRegistration': (547568,
                              166,
                              'c2a7f05f4e8e455cd26140ba4e47545407beb5d39f28c47dee8a2cc520b014d2'),
 'HeadshotOnlyConsumer': (9034788, 162, '9a16979c6da86fccaf5a894d7eb44f150b0f103e0841196f55da33bb672614e4'),
 'HealthshotTimeQuery': (9227504, 91, 'b45c8ceda9dbf08a665b673e7cbf7011f408c37681152b6bae04cda8e34367a0'),
 'HealthshotDamageScale': (9043556, 36, 'f9fbf8e03013bb2d37641ba38381d266c7c923f44e2ef4b2e3386485c228145f'),
 'ArmorRatioInitial': (8929632, 24, '7ca2442ce4df9cdf93b8ca0e35eb3a979d7c52e79f3f19ffb9abb81b8d415e9b'),
 'ArmorBonusInitial': (9044013, 18, 'c6b0236858e055c7c0bad9fae911645d8cf81b67b287781e08757dfc0ad8b330')}


def verify(game, header):
    raw, image, base, nt = mapped(game / 'csgo/bin/win64/client.dll')
    if hashlib.sha256(raw).hexdigest() != EXPECTED_HASH:
        raise SystemExit('Changed installed client SHA-256; investigate before updating contracts')
    anim_raw, anim, anim_base, _ = mapped(game / 'bin/win64/animationsystem.dll')
    if hashlib.sha256(anim_raw).hexdigest() != ANIMATION_HASH:
        raise SystemExit('Changed animationsystem SHA-256')
    def u32(at): return struct.unpack_from('<I', image, at)[0]
    def ptr(at): return struct.unpack_from('<Q', image, at)[0] - base
    def rip(at): return at + 7 + struct.unpack_from('<i', image, at + 3)[0]
    def string(at):
        if not 0 < at < len(image): raise SystemExit('Invalid schema string')
        end = image.find(0, at, min(at + 1024, len(image)))
        if end < 0: raise SystemExit('Unterminated schema string')
        return bytes(image[at:end]).decode('ascii')
    fields = {}
    for owner, (registration, array, count, wanted) in SCHEMA.items():
        if string(ptr(registration)) != owner or ptr(registration + 0x28) != array or struct.unpack_from('<H', image, registration + 0x1C)[0] != count:
            raise SystemExit(f'Changed named schema registration: {owner}')
        actual = {string(ptr(array + i*32)): u32(array + i*32 + 16) for i in range(count)}
        if any(actual.get(name) != value for name, value in wanted.items()):
            raise SystemExit(f'Changed schema damage fields: {owner}')
        fields[owner] = wanted
    # Independent CHitBox schema validates the trace's copied hitgroup field.
    reg, arr = 0x81D848, 0x81D6A0
    aptr = lambda at: struct.unpack_from('<Q', anim, at)[0] - anim_base
    astr = lambda at: bytes(anim[at:anim.find(0, at)]).decode('ascii')
    if astr(aptr(reg)) != 'CHitBox' or aptr(reg + 0x28) != arr or struct.unpack_from('<H', anim, reg + 0x1C)[0] != 13:
        raise SystemExit('Changed CHitBox schema registration')
    afields = {astr(aptr(arr + i*32)): struct.unpack_from('<I', anim, arr + i*32 + 16)[0] for i in range(13)}
    if afields.get('m_nGroupId') != CONSTANTS['HitboxGroup']:
        raise SystemExit('Changed CHitBox::m_nGroupId')
    if primary_vtable(image, base, b'.?AVCCSPlayer_ItemServices@@') != CONSTANTS['ItemServicesVtable']:
        raise SystemExit('Changed item-services RTTI ownership')
    for label, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at+size]).hexdigest() != digest:
            raise SystemExit(f'Changed native damage window: {label}')
    wrappers = {'DamageScaleCtBody': (0x85BB2, 0x85BBB, 'mp_damage_scale_ct_body'),
        'DamageScaleCtHead': (0x85C52, 0x85C5B, 'mp_damage_scale_ct_head'),
        'DamageScaleTBody': (0x85CF2, 0x85CFB, 'mp_damage_scale_t_body'),
        'DamageScaleTHead': (0x85D92, 0x85D9B, 'mp_damage_scale_t_head'),
        'HeadshotOnly': (0x85B70, 0x85B77, 'mp_damage_headshot_only')}
    for name, (name_at, wrapper_at, native_name) in wrappers.items():
        if string(rip(name_at)) != native_name or rip(wrapper_at) != CONSTANTS[name]:
            raise SystemExit(f'Changed named damage rule wrapper: {name}')
    # Bind rule consumers to the same wrappers; slot +8 holds current storage.
    for at, wrapper in [(0x89D9F9,'DamageScaleCtBody'), (0x89DA02,'DamageScaleTBody'),
                        (0x89DA23,'DamageScaleCtHead'), (0x89DA2C,'DamageScaleTHead'),
                        (0x89DC24,'HeadshotOnly')]:
        if rip(at) != CONSTANTS[wrapper] + CONSTANTS['CvarValuePointer']:
            raise SystemExit('Damage rule registration/consumer disagreement')
    if rip(0x1779564) != CONSTANTS['UnknownHitboxPointer']:
        raise SystemExit('Changed unknown trace-hitbox sentinel storage')
    # Full windows establish these operand widths and semantics.
    if image[0xAADF49] != CONSTANTS['TraceHitbox'] or image[0xAADF5F] != CONSTANTS['HitboxGroup']:
        raise SystemExit('Changed trace hitbox/group operands')
    if struct.unpack_from('<f', image, 0x1B14FE4)[0] != 500.0 or struct.unpack_from('<f', image, 0x1AC6070)[0] != 0.5:
        raise SystemExit('Changed native falloff or ordinary armor constants')
    if struct.unpack_from('<d', image, 0x1C0E8D8)[0] != 1.25 or struct.unpack_from('<d', image, 0x1C0E8D0)[0] != 0.75:
        raise SystemExit('Changed native stomach/leg multipliers')
    # Bind the exponent operation to its named import, not a presumed ABI.
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
    if imports.get(0x1AAD530) != ('tier0.dll', 'V_powf'):
        raise SystemExit('Changed native range falloff exponent import')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in CONSTANTS.items():
        matches = re.findall(rf'\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;', source)
        if len(matches) != 1 or int(matches[0], 0) != value:
            raise SystemExit(f'Header mismatch: aim_damage_offsets::{name}')
    for name, (at, size, _) in WINDOWS.items():
        matches = re.findall(rf'\b{name}Bytes\s*\[\s*\]\s*\{{([^}}]+)\}}', source)
        if len(matches) != 1:
            raise SystemExit(f'Missing runtime damage window: {name}')
        actual = bytes(int(x.strip(),0) for x in matches[0].split(',') if x.strip())
        if actual != image[at:at+size]: raise SystemExit(f'Runtime damage bytes mismatch: {name}')
        entries = re.findall(rf'\{{\s*(0[xX][0-9a-fA-F]+|\d+)\s*,\s*{name}Bytes\s*,\s*sizeof\({name}Bytes\)\s*\}}', source)
        if len(entries) != 1 or int(entries[0],0) != at:
            raise SystemExit(f'Runtime damage window address mismatch: {name}')
    return {'verified': True, 'client_sha256': EXPECTED_HASH, 'animationsystem_sha256': ANIMATION_HASH,
        'architecture': 'AMD64 PE32+', 'schema': fields, 'constants': CONSTANTS,
        'native_windows': len(WINDOWS), 'scope': 'direct ordinary bullets; conservative floor estimate; no penetration or actual-outcome guarantee'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT)
    parser.add_argument('--header', type=Path, default=ROOT / 'include/aim_damage_offsets.h')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    result = verify(args.game, args.header)
    report = json.dumps(result, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(report+'\n', encoding='utf-8')
    print(report)


if __name__ == '__main__':
    main()
