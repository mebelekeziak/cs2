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
EXPECTED_HASH = 'ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5'
ANIMATION_HASH = '8aa57d0c5b90cb49ad5fd9f141004710180a601b08159e50e93c4470142ea132'
SCHEMA = {'CCSWeaponBaseVData': (35769608,
                        35766688,
                        83,
                        {'m_nDamage': 2080,
                         'm_flHeadshotMultiplier': 2084,
                         'm_flArmorRatio': 2088,
                         'm_flRangeModifier': 2100}),
 'C_CSPlayerPawn': (35896008,
                    35892544,
                    105,
                    {'m_ArmorValue': 7888, 'm_bGunGameImmunity': 13576, 'm_flHealthShotBoostExpirationTime': 5544}),
 'C_BasePlayerPawn': (35612648, 35611728, 28, {'m_pItemServices': 4856}),
 'CCSPlayer_ItemServices': (35796808, 35796720, 2, {'m_bHasHelmet': 73})}
CONSTANTS = {'Damage': 2080,
 'HeadshotMultiplier': 2084,
 'ArmorRatio': 2088,
 'RangeModifier': 2100,
 'ArmorValue': 7888,
 'ItemServices': 4856,
 'HasHelmet': 73,
 'ItemServicesVtable': 29437280,
 'ServicePawn': 56,
 'GunGameImmunity': 13576,
 'DamageScaleCtBody': 38529752,
 'DamageScaleCtHead': 38529768,
 'DamageScaleTBody': 38529784,
 'DamageScaleTHead': 38529800,
 'HeadshotOnly': 38529736,
 'CvarValuePointer': 8,
 'CvarValue': 88,
 'TraceHitbox': 16,
 'HitboxGroup': 56,
 'UnknownHitboxPointer': 41114976,
 'HealthShotBoostExpiration': 5544}
WINDOWS = {'WeaponDamageInputs': (13783443, 41, 'b42cbe034c28fd2c731cbfbe48ed381a3d5db9e780cbf2712c052da44221c5cd'),
 'DamageScales': (9037857, 72, 'df84344aabe1ef19f7939010439b77884c509c92c76fa9a495d707375c4808dd'),
 'HeadAndBodyScale': (9038200, 204, '85576439368adf71523759c473d43ad083a8e2b0c8084a33d3b466e9cf9a6f0e'),
 'ScaleJumpTable': (9039072, 32, '43e174eeed108a87a59718ca8bff7219d9d08273b97c499d3e9087516eb2b48c'),
 'ArmorCoverage': (9280288, 116, '3f642f12b1e3100154f38be9d16f0caa02e71680cbdcd4dd930189c9a4b641bf'),
 'ArmorRatioGetter': (8933606, 12, '92bd0b76e4ababda336f9a3cb04e6a34d6984d73413948a246de3c1ecc228f64'),
 'ArmorCalculation': (9047682, 107, 'a37427b7911fa19754e0fd8c21e9b87706260cc4afc85c02037fd90d7713e6e2'),
 'RangeFalloff': (9053869, 40, '3263dc3ddf6e039f9dc4424ec317cdfebcd7e075f9233d209b48aac545205146'),
 'TraceHitgroup': (11201920, 64, 'a4c684bcec8c6393f991f8e177831793f23a6c5747c354e3334501cc88970884'),
 'TraceUnknownHitbox': (24623040, 15, 'ec041288f58528dd6a09575c4de504633a67f9699f8425ace15147fe6681e5f7'),
 'CtBodyRegistration': (547744, 144, '7f176c087eeddb4734cdfa66e2d0ffbe2f632280b13548343e295a780c3beda2'),
 'CtHeadRegistration': (547904, 144, 'ea5e1f8a8b96779847d47bbfb82fb71463eeee0f692accff10b041329640d212'),
 'TBodyRegistration': (548064, 144, '17f3899e9a0ec1635ce7846b1a95aa5bb9e05a04b374f7c7dd67f694c2f8d96d'),
 'THeadRegistration': (548224, 144, 'b7d3c33dd3f68bc01ecef157862951d72305b4266abc69eedf56790389973ea3'),
 'HeadshotOnlyRegistration': (547568, 166, 'd9d78aaf3aafbca05fb1483730fba3e50a73b84fb0f4f2690e920b7158f9d1e9'),
 'HeadshotOnlyConsumer': (9038420, 162, '5b4ed658a664c9ea747bd0707455efa9f1ea1a2ddc1027ae2d22f58b1004ca0c'),
 'HealthshotTimeQuery': (9231136, 91, '76994e947131f53df1f6cf8f0f64fe7dfe4bfbca826c2b927cf32465aa091e20'),
 'HealthshotDamageScale': (9047188, 36, '5a8c0ae7afb9ef4b1d2f41059e6cbc144410cf8900fb63acb655003522be8685'),
 'ArmorRatioInitial': (8933264, 24, '5039b340b11dc207b03d9044eddc7a275ae19d381cb968b606ff2f0d7017da2c'),
 'ArmorBonusInitial': (9047645, 18, '64f0fe0e3a260f8b25dd04667242adaded39b91de3060165122ed21391a8b6a1')}


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
    reg, arr = 0x81d6b8, 0x81d510
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
    wrappers = {'DamageScaleCtBody': (0x85bb2, 0x85bbb, 'mp_damage_scale_ct_body'),
        'DamageScaleCtHead': (0x85c52, 0x85c5b, 'mp_damage_scale_ct_head'),
        'DamageScaleTBody': (0x85cf2, 0x85cfb, 'mp_damage_scale_t_body'),
        'DamageScaleTHead': (0x85d92, 0x85d9b, 'mp_damage_scale_t_head'),
        'HeadshotOnly': (0x85b70, 0x85b77, 'mp_damage_headshot_only')}
    for name, (name_at, wrapper_at, native_name) in wrappers.items():
        if string(rip(name_at)) != native_name or rip(wrapper_at) != CONSTANTS[name]:
            raise SystemExit(f'Changed named damage rule wrapper: {name}')
    # Bind rule consumers to the same wrappers; slot +8 holds current storage.
    for at, wrapper in [(0x89e829,'DamageScaleCtBody'), (0x89e832,'DamageScaleTBody'),
                        (0x89e853,'DamageScaleCtHead'), (0x89e85c,'DamageScaleTHead'),
                        (0x89ea54,'HeadshotOnly')]:
        if rip(at) != CONSTANTS[wrapper] + CONSTANTS['CvarValuePointer']:
            raise SystemExit('Damage rule registration/consumer disagreement')
    if rip(0x177b7c4) != CONSTANTS['UnknownHitboxPointer']:
        raise SystemExit('Changed unknown trace-hitbox sentinel storage')
    # Full windows establish these operand widths and semantics.
    if image[0xaaed89] != CONSTANTS['TraceHitbox'] or image[0xaaed9f] != CONSTANTS['HitboxGroup']:
        raise SystemExit('Changed trace hitbox/group operands')
    if struct.unpack_from('<f', image, 0x1b17034)[0] != 500.0 or struct.unpack_from('<f', image, 0x1ac8080)[0] != 0.5:
        raise SystemExit('Changed native falloff or ordinary armor constants')
    if struct.unpack_from('<d', image, 0x1c10868)[0] != 1.25 or struct.unpack_from('<d', image, 0x1c10860)[0] != 0.75:
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
    if imports.get(0x1aaf528) != ('tier0.dll', 'V_powf'):
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
