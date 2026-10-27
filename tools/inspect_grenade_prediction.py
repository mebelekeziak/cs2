"""Verify grenade launch and projectile contracts from installed AMD64 files.

Read-only installed-file verification; does not open or start a game process.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable, DEFAULT
from inspect_grenade_native import verify as verify_native

ROOT = Path(__file__).resolve().parents[1]
CLIENT_SHA256 = 'ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5'
SERVER_SHA256 = '2a229dd46c8af0b52c1f49a3e343b99d9ffe946c102c56102a45b00f6a52349c'
SERVER_WINDOWS = {
    'molotov_ignition_and_removal': (0x9BDC90, 0x8D0, 'bf8fd228879dd18fe1b45e8119d5352d0c9a4a55b57fefc64b203a0c25a65634'),
    'generic_explosion_tick_producer': (0x9BE780, 0x160, 'd893f2324501db326a9b1850849d6e8dabe0f29f97baa412f4189f26df97ba57'),
    'flash_spawn': (0x3ae390, 0xeb, '8ab4bbb7dee0dcb2476941ae99c200aa734d7cb3ea4cdf084b522a1d0f194556'),
    'flash_constructor': (0x3aa230, 0x14e, 'a3bc4b6b4e58cb05f187157198917e8b7017538075bec05285b14986bad5f669'),
    'he_creator': (0x3aac40, 0x26c, '74441489edbb1b47b0784786851258bd0353dbc683cf6c28c0f6f5de8c457d68'),
    'projectile_spawn': (0x9D8730, 0x2A1, '9b83d44d1b4ce800f6b7a9793e95e8508a9463f34e4ef04a35af2ae009d91676'),
    'grenade_bounce': (0x9D3E60, 0x11CF, '069a94c7ead56614fed36c6677d647bdfa07e015413468d5d9230649c46501f5'),
    'physics_filter': (0xBE3250, 0x15B, '0741b84eb57deef8a577d2c3ce949233028cb0d70abab864795b775925b2e59f'),
    'collision_group_getter': (0xCFBAA0, 5, '0dde14a5b2a736702537b43277c29b103b791d1e7fbb32556ed647b8687a9cf5'),
    'collision_group_setter': (0xD13880, 0xDA, '5327cf3c68cf9471cd56e0c16896c0966eaac0958a8d2230aa9f4babe852efb0'),
    'collision_setup': (0xD147B0, 0x18B, '2f239ceef326fd7b094604ea4be1fdfe4d8c4488f50967dc81ac7b5b614280c1'),
    'gravity_step': (0xE94020, 0x186, '3aa987266add32a5b6599b4ff3fde748d4512dfeb929921a6d14177b87541a8a'),
    'clip_velocity': (0xE94610, 0x8A, '73cad7b2c6a3fe9ab45bd417e1bc089174d6df66a667ab1bf9b7810d2d61c52a'),
    'smoke_rest_think': (0x9DA000, 0x143, '45818f2652ba916f3ed7b5363acb30b7b49513745d97d756cbea375b7b56455c'),
    'decoy_rest_think': (0x9D9D40, 0x60, '67a7e555d3111fa26821e34de39ca8e22320bdeaa6b618943cc876e1fd7c960f'),
}
WINDOWS = {
    'projectile_constructor': (0x7C4DB0, 0xF5, '3022b3a4777147c1c9b54bdb2c4824f371eb1b2a63f7c23d106f955ef8e97921'),
    'projectile_effect_start': (0x7D93E0, 0x17C, 'bab28cfad1160334533191008f07500251868051eddb800be987359f0e770e6e'),
    'smoke_effect_update': (0x7D9560, 0x17C, '4b5770514528a4a57d900a437cef16890a394378a9c00f4c9cf7ffc8a0468b29'),
    'smoke_deploy_start': (0x7D8EF0, 0x84, 'c488b5f915b7a83c2b9f4ce702a5187a5e733428d9d6b189d2fde5ffd2f4b437'),
    'absolute_velocity_query': (0x219ac0, 0xcd, '8a0bfa8e908626ba7d5647649e2684c404429dc8bf4bc7ab065aec5154b79a6b'),
    'world_group_clock': (0x75e050, 0x78, 'b874b2f990e5f2f34b82bf517732cc02955b91237c91b8de3b3628bbdbc29990'),
    'thrower_resolver': (0x724b70, 0x142, 'afd424dbc54c6e0134ee75f2084c4a520cff5b3c2cf7ef5a23aed5215357a9c3'),
    'launch_query': (0x7C7940, 0x1C7, 'a8282c493fd0e181cb0179a346c9d23ab812eb53551eaf2aaa799229afc5faaf'),
    'launch_builder': (0x7C7B10, 0x40A, '4a4b136385e1ce5fb9933cd2427f141a7f53ac61a3a17d5c3ff155c2d9f5b5fb'),
    'source_query': (0x8CDFB0, 0x279, 'd24884f8ddc732dbaa60ae06ec1df33bf708e5379a7be36caeffe65ba8a591a7'),
    'spawn_clock': (0x7DE3F0, 0x44, '5a2908c36ef87fa1a15cfe1732207fb61a13860cab9e64c2348a8246b8332e6d'),
    'throw_velocity_getter': (0x7CEB60, 0x40, 'ec1c327fa386663bd26677a5ce9be7272b7cb01091af875da16260232018e962'),
    'molotov_fuse_registration': (0x990F0, 0x97, '2deaf12434c1a331ba7d808dbe38e2a148ccb5ddfaff1dc33acf51d4a44e2b71'),
    'molotov_slope_registration': (0x9A480, 0xB3, '9dd657e969715a40e22b254f1ada93e1ead3c59652a138f02ba7adb5fec8f424'),
}
SCHEMA = {
    'CEntityIdentity': (0x226D2F8, 0x226D170, 12, 0x70, {'m_worldGroupId': 'IdentityWorldGroup'}),
    'C_BaseCSGrenade': (0x221A2A8, 0x221A0D0, 14, 0x1FE0, {
        'm_bIsHeldByPlayer': 'Held', 'm_bPinPulled': 'PinPulled',
        'm_fThrowTime': 'ThrowTime', 'm_flThrowStrength': 'ThrowStrength'}),
    'C_BaseCSGrenadeProjectile': (0x221AA18, 0x221A800, 16, 0x1348, {
        'm_vInitialPosition': 'InitialPosition', 'm_vInitialVelocity': 'InitialVelocity',
        'm_nBounces': 'Bounces', 'm_nExplodeEffectTickBegin': 'ExplodeTick',
        'm_bExplodeEffectBegan': 'ExplodeBegan', 'm_flSpawnTime': 'ProjectileSpawnTime'}),
    'C_BaseGrenade': (0x2213578, 0x22133E0, 12, 0x12B0, {
        'm_hThrower': 'Thrower', 'm_hOriginalThrower': 'OriginalThrower',
        'm_flDetonateTime': 'DetonateTime', 'm_bIsLive': 'ProjectileLive'}),
    'C_BaseEntity': (0x21F6E78, 0x21F5550, 82, 0x600, {
        'm_hOwnerEntity': 'Owner', 'm_vecAbsVelocity': 'AbsVelocity', 'm_flGravityScale': 'GravityScale',
        'm_flActualGravityScale': 'ActualGravityScale', 'm_flElasticity': 'Elasticity'}),
    'C_MolotovProjectile': (0x221A3C8, 0x221A390, 1, 0x1370, {'m_bIsIncGrenade': 'IncendiaryFlag'}),
    'C_SmokeGrenadeProjectile': (0x221A798, 0x221A620, 11, 0x1500, {
        'm_nSmokeEffectTickBegin': 'SmokeTickBegin', 'm_bDidSmokeEffect': 'SmokeDidEffect',
        'm_bSmokeEffectSpawned': 'SmokeEffectSpawned'}),
}
WEAPONS = ['C_HEGrenade', 'C_Flashbang', 'C_SmokeGrenade', 'C_DecoyGrenade', 'C_MolotovGrenade', 'C_IncendiaryGrenade']
PROJECTILES = ['C_HEGrenadeProjectile', 'C_FlashbangProjectile', 'C_SmokeGrenadeProjectile', 'C_DecoyProjectile', 'C_MolotovProjectile']


def verify(game, header):
    raw, image, base, nt = mapped(game / 'csgo/bin/win64/client.dll')
    if hashlib.sha256(raw).hexdigest() != CLIENT_SHA256:
        raise SystemExit('Changed client; independently reverify grenade prediction.')
    for name, (at, size, expected) in WINDOWS.items():
        if hashlib.sha256(image[at:at+size]).hexdigest() != expected:
            raise SystemExit('Changed complete grenade window: ' + name)
    def u32(p): return struct.unpack_from('<I', image, p)[0]
    def ptr(p): return struct.unpack_from('<Q', image, p)[0] - base
    def string(p):
        if not 0 < p < len(image): raise SystemExit('Invalid schema string')
        end = image.find(0, p, min(p+256, len(image)))
        if end < 0: raise SystemExit('Unterminated schema string')
        return bytes(image[p:end]).decode('ascii')
    values = {'ClientTimestamp': struct.unpack_from('<I', raw, nt+8)[0], 'ClientImageSize': len(image)}
    for name, (registration, array, count, size, fields) in SCHEMA.items():
        if (string(ptr(registration)) != name or ptr(registration+0x28) != array or
            struct.unpack_from('<H', image, registration+0x1C)[0] != count or u32(registration+0x18) != size):
            raise SystemExit('Changed grenade schema: ' + name)
        actual = {string(ptr(array+i*32)): u32(array+i*32+16) for i in range(count)}
        values.update({symbol: actual[field] for field, symbol in fields.items()})
    # Native launch callers pass five arguments. The return describes stashed
    # source selection, and is deliberately not treated as success/failure.
    for at in [0x7E0B82, 0x7E0BA4, 0x10D96CF]:
        if image[at] != 0xE8 or at+5+struct.unpack_from('<i', image, at+1)[0] != 0x7C7940:
            raise SystemExit('Changed launch caller')
    values['GetAbsVelocity'] = 0x8CE1F3+5+struct.unpack_from('<i', image, 0x8CE1F4)[0]
    values['Launch'] = 0x7E0B82+5+struct.unpack_from('<i', image, 0x7E0B83)[0]
    # Fully decoded launch builder: shape bounds and exact filter inputs.
    values['LaunchHullExtent'] = -struct.unpack_from('<f', image, 0x1BD5DFC)[0]
    values['LaunchMask'] = struct.unpack_from('<Q', image, 0x7C7D77)[0]
    values['LaunchLayer'] = image[0x7C7D9A]
    # Client named cvar registrations; float defaults are not substituted for
    # valid live values. Registration input RCX is the wrapper address.
    def rip(at, displacement, size): return at+size+struct.unpack_from('<i', image, at+displacement)[0]
    if string(rip(0x99102, 3, 7)) != 'molotov_throw_detonate_time' or string(rip(0x9A4F6, 3, 7)) != 'weapon_molotov_maxdetonateslope':
        raise SystemExit('Changed Molotov cvar names')
    values['MolotovFuse'] = rip(0x99168, 3, 7)
    values['MolotovMaxSlope'] = rip(0x9A502, 3, 7)
    server_raw, server, _, _ = mapped(game/'csgo/bin/win64/server.dll')
    if hashlib.sha256(server_raw).hexdigest() != SERVER_SHA256:
        raise SystemExit('Changed server file; independently reverify grenade physics.')
    for name, (at, size, expected) in SERVER_WINDOWS.items():
        if hashlib.sha256(server[at:at+size]).hexdigest() != expected:
            raise SystemExit('Changed complete server grenade window: '+name)
    def sf(at): return struct.unpack_from('<f', server, at)[0]
    values.update({
        'HullExtent': sf(0x9D881C),
        'GrenadeMask': struct.unpack_from('<Q', server, 0x9D8835)[0],
        'GrenadeLayer': struct.unpack_from('<I', server, 0x9D8785)[0],
        'GrenadeInteractsAs': (struct.unpack_from('<Q', server, 0x9D884B)[0] & ~(1<<20)) | (1<<17),
        'GrenadeFlags39': server[0xBE32D7] | server[0xBE3392],
        'InitialGravityScale': sf(0x1795D70),
        'InitialElasticity': sf(0x9BD549),
        'MaximumElasticity': sf(0x1795D78),
        'Overbounce': sf(0x1786584),
        'ClipEpsilon': sf(0x178ADF0),
        'GroundNormalZ': sf(0x17B2E24),
        'SoftGroundNormalZ': sf(0x1795D58),
        'StopSpeed': sf(0x17B84C0)**0.5,
        'FastFloorSpeedSquared': sf(0x18EF870),
        'FastFloorDotThreshold': sf(0x178657C),
        'HighExplosiveFuse': sf(0x17A9C80),
        'FlashFuse': sf(0x3AA354),
        'SmokeMinimumAge': sf(0x18EF818),
        'SmokeRestSpeed': sf(0x1795D58),
        'DecoyFirstThink': sf(0x1786584),
        'DecoyThinkInterval': sf(0x178ADF8),
        'DecoyRestSpeed': sf(0x178ADF8),
    })
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in values.items():
        matches = re.findall(r'\b'+name+r'\s*=\s*(0[xX][0-9A-Fa-f]+|[0-9]+(?:\.[0-9]+)?)[fF]?\s*;', source)
        if len(matches) != 1: raise SystemExit('Missing/duplicate grenade header symbol: '+name)
        parsed = int(matches[0], 0) if matches[0].lower().startswith('0x') else float(matches[0])
        if isinstance(value, float):
            if struct.pack('<f', parsed) != struct.pack('<f', value): raise SystemExit('Header mismatch: '+name)
        elif parsed != value: raise SystemExit('Header mismatch: '+name)
    for symbol, entry in [('LaunchBytes', 'Launch'), ('GetAbsVelocityBytes', 'GetAbsVelocity')]:
        fingerprint = re.search(symbol+r'\[\]\s*\{([^}]+)\}', source)
        actual_bytes = bytes(int(v, 0) for v in re.findall(r'0[xX][0-9A-Fa-f]+|[0-9]+', fingerprint.group(1))) if fingerprint else b''
        if len(actual_bytes) < 16 or actual_bytes != image[values[entry]:values[entry]+len(actual_bytes)]:
            raise SystemExit('Header fingerprint mismatch: '+symbol)
    tables = {}
    for symbol, names in [('WeaponTables', WEAPONS), ('ProjectileTables', PROJECTILES)]:
        match = re.search(symbol+r'\[\]\s*\{(.*?)\};', source, re.S)
        records = [(int(a, 0), int(k)) for a, k in re.findall(r'\{\s*(0[xX][0-9A-Fa-f]+|[0-9]+)\s*,\s*([0-9]+)\s*\}', match.group(1))] if match else []
        expected = [(primary_vtable(image, base, ('.?AV'+name+'@@').encode()), k) for k, name in enumerate(names)]
        if records != expected: raise SystemExit('Header RTTI tables mismatch: '+symbol)
        tables[symbol] = expected
    native = verify_native(game)
    return {'client_sha256': CLIENT_SHA256, 'server_sha256': SERVER_SHA256, 'live_observation': False, 'contracts': values, 'tables': tables, 'native_preview': native,
            'complete_windows': list(WINDOWS), 'server_windows': list(SERVER_WINDOWS),
            'native_launch_return': 'stashed source selected; outputs valid on either return'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT)
    parser.add_argument('--header', type=Path, default=ROOT/'include/grenade_prediction_offsets.h')
    args = parser.parse_args()
    print(json.dumps(verify(args.game, args.header), indent=2))


if __name__ == '__main__': main()
