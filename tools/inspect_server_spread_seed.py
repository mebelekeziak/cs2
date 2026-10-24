"""Inspect installed server/client spread-seed differences without opening processes.

Exact installed-file evidence only. Does not connect to a server, read runtime
Cvars, launch CS2, call native code, or modify game files. This standalone audit
is intentionally separate from the client compatibility-component count.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from verify_sky_draw import mapped, primary_vtable
from inspect_aim_spread import imports

ROOT = Path(__file__).resolve().parents[1]
GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game')
PINS = {
    'server': '2a229dd46c8af0b52c1f49a3e343b99d9ffe946c102c56102a45b00f6a52349c',
    'client': 'ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5',
}
WINDOWS = {'server': {'Seed': [2798896, 184, 'b12b2945b717d6d5e050b86ff2ff13b943e48c5cd02b8021d7619c4534aa1851'],
            'Quantize': [2774816, 34, '4dca5234508f462019c2708f0b25e80d92e1119db3ac9eae41378be96721f165'],
            'Normalize': [2074976, 102, 'fa3c85f596a09380addc03aa22dc1125f90df6c10f5f28906f8ad7fcabd91704'],
            'Floor': [23895140, 124, '0e870f1f5a14a13b9a4d920e76a62cc322ebc292c0f0798c56ce77fb84ce13f8'],
            'CustomCommandSeed': [13125696,
                                  265,
                                  '8153ac90878df2d881fa38de75fa7d5db87c9f93f99645191ba15d6fb64e82b9'],
            'CvarRegistration': [892720,
                                 166,
                                 '6488ad231974f781594bbf06875ff30fc84463b2d13b4cfc2290595b7536ddc0'],
            'BaselineSeedSetter': [4137264,
                                   7,
                                   '798881255ec1426201f48b16847393f77aa90145919775546a9bae5e2ca0af3e'],
            'AlternateSeedSetter': [4137280,
                                    7,
                                    '307407ba77e961543b2ebbd3d7ebf0ad286b73d4c8d357c67d3482b204b341b0'],
            'PrimarySeedCaller': [10479638,
                                  192,
                                  '3c49f849ad58177069a5f74487a1ea25092c5ba5d6a52cd44c1a726e344afd6a'],
            'SecondarySeedCaller': [10507875,
                                    188,
                                    'd1c162a323fe088078d5c61bfe0818b9e0844cb8bc6c313e48cabebb91bca2ec'],
            'SpreadSeedLoadAndIncrement': [2800066,
                                           185,
                                           '50c02c1c4988d1775d8d22cb667017a034238e8205fa96f92c7212845ff1e5ba'],
            'SpreadSeedArguments': [2800478,
                                    71,
                                    '85e482a0003ad609f9ae1538036eb6ad6d100172b2860db3c6a127f2912b9aff'],
            'Distribution': [2801760,
                             304,
                             '998ebc698e49ba537d67be9a07e880c3ba98fc2a5acfa1a3ce7581ae051bbca3'],
            'SeedSynchronizedFlag': [11155792,
                                     75,
                                     '4e844d4cf59cc7c605ee2f78f3410d8476c79550a3e861aaed2d81b09edebda1'],
            'ControllerPrediction': [11969998,
                                     65,
                                     'eee53dad10dc58e57598fc8bd57bb695473017564aac546e18a00b0d4ab81268'],
            'CommandRunnerBegin': [13115872,
                                   43,
                                   '18775d16e6e984d1cb06fa15e98402e54a13afb2d754993fa4456a44f2742871'],
            'CommandRunnerEnd': [13116317,
                                 23,
                                 '1afac63a310dfcfc45fb4cb1ae06457727931560053b6b3974464036f1ea6c1d'],
            'DerivedSeedEnd': [11312687,
                               26,
                               'f11813aaa6c56782bf3b733fd47ff66acfa069828bfa1e587e750b28af0a28ad'],
            'DefaultSeedEnd': [13064064,
                               30,
                               'a42e0a7ecdd3d42255db61364bd0e9fcffa0aba336531f4f7a2d2565bb92e3fc']},
 'client': {'Seed': [13782384, 166, 'bcc8a4dbc010d695587995487c4873cc9cf6953ce314c804a0ed40760c300d13']}}

def require(value, message):
    if not value:
        raise SystemExit(message)

def string(image, at):
    require(0 < at < len(image), 'Invalid native string address')
    end = image.find(0, at, min(at + 1024, len(image)))
    require(end >= at, 'Unterminated native string')
    return bytes(image[at:end]).decode('ascii')

def schema(image, base, registration, name):
    pointer = lambda at: struct.unpack_from('<Q', image, at)[0] - base
    require(string(image, pointer(registration)) == name, 'Changed schema class name: ' + name)
    count = struct.unpack_from('<H', image, registration + 0x1c)[0]
    array = pointer(registration + 0x28)
    require(0 < count < 300 and 0 < array < len(image), 'Invalid schema field array')
    return {string(image, pointer(array + i * 0x20)):
        struct.unpack_from('<I', image, array + i * 0x20 + 0x10)[0] for i in range(count)}

def verify(images):
    for module, (raw, image, base, nt) in images.items():
        require(hashlib.sha256(raw).hexdigest() == PINS[module],
            'Changed ' + module + '.dll: repeat independent server-seed investigation')
        for name, (at, size, digest) in WINDOWS[module].items():
            require(hashlib.sha256(image[at:at + size]).hexdigest() == digest,
                'Changed native server-seed contract: ' + module + '/' + name)
    raw, image, base, nt = images['server']
    def u32(at): return struct.unpack_from('<I', image, at)[0]
    def rip(at, displacement=3, size=7):
        return at + size + struct.unpack_from('<i', image, at + displacement)[0]
    def call(at):
        require(image[at] == 0xe8, 'Expected native call')
        return rip(at, 1, 5)
    def iat(at):
        require(image[at:at + 2] == b'\xff\x15', 'Expected imported native call')
        return rip(at, 2, 6)
    name = rip(0xd9fb0)
    description = rip(0xd9f99)
    wrapper = rip(0xd9fb7)
    require(string(image, name) == 'sv_usercmd_custom_random_seed', 'Changed custom-seed Cvar name')
    require(string(image, description) == 'When enabled server will populate an additional random seed independent of the client',
        'Changed custom-seed Cvar description')
    require(call(0xd9fbe) == 0x16a4a30 and rip(0xc84896) == wrapper + 8,
        'Custom-seed registration and consumer do not agree')
    require(image[0xd9f94:0xd9f99] == bytes.fromhex('c644243401') and
        image[0xd9f43:0xd9f47] == bytes.fromhex('89442430') and
        image[0xd9f3a:0xd9f3c] == bytes.fromhex('33c0'), 'Changed boolean default creation parameters')
    registration_flags = u32(0xd9fa7)
    require(registration_flags == 0x80000, 'Changed registration flags')
    baseline = rip(0x3f2130, 2, 6)
    alternate = rip(0x3f2140, 2, 6)
    require(rip(0x2ab546, 2, 6) == baseline and rip(0x2ab53d, 2, 6) == alternate,
        'Native seed selector and independent setters disagree')
    require(call(0xc84891) == 0x3f2130 and call(0xc8492c) == 0x3f2140,
        'Command-seed producer does not reach seed setters')
    require(call(0x9fe824) == 0x2ab530 and call(0xa05671) == 0x2ab530 and
        call(0x9fe8d1) == 0x2ab5f0 and call(0xa0571a) == 0x2ab5f0 and
        call(0x2abba0) == 0x2ac060, 'Changed shot seed-to-distribution call chain')
    server_imports = imports(image, raw, nt)
    wanted = {
        0xc848a7: 'Plat_FloatTime', 0xc848c9: 'RandomInt',
        0xc848fc: '?Reset@CSHA1@@QEAAXXZ', 0xc84912: '?Update@CSHA1@@QEAAXPEBXI@Z',
        0xc8491d: '?Final@CSHA1@@QEAAXXZ',
        0x2ab5a9: '?Reset@CSHA1@@QEAAXXZ', 0x2ab5bf: '?Update@CSHA1@@QEAAXPEBXI@Z',
        0x2ab5ca: '?Final@CSHA1@@QEAAXXZ', 0x2ac0e4: 'RandomSeed',
    }
    for site, symbol in wanted.items():
        require(server_imports.get(iat(site)) == ('tier0.dll', symbol), 'Changed named import: ' + symbol)
    require(u32(0xc84904) == 16 and u32(0x2ab5b1) == 12, 'Changed custom/ordinary SHA payload widths')
    controller = schema(image, base, 0x1e1e3e8, 'CBasePlayerController')
    cs_controller = schema(image, base, 0x1e1a328, 'CCSPlayerController')
    pawn = schema(image, base, 0x1de47d8, 'CBasePlayerPawn')
    require(controller['m_bPredict'] == u32(0xc84863) == 0x5ad and
        pawn['m_hController'] == u32(0xc8bf99) == 0xc98,
        'Changed movement-service controller resolution/prediction predicate')
    require(rip(0xaa3968, 2, 6) == baseline and rip(0xaa396e, 2, 6) == alternate and
        cs_controller['m_bFireBulletsSeedSynchronized'] == u32(0xaa395f) == 0xa39,
        'Changed independently named seed-synchronization status producer')
    require(string(image, rip(0xb6a5da)) == 'cl_predict' and
        u32(0xb6a5f8) == controller['m_bPredict'], 'Changed prediction field user-info source')
    movement = primary_vtable(image, base, b'.?AVCCSPlayer_MovementServices@@')
    require(struct.unpack_from('<Q', image, movement + 0x150)[0] == base + 0xc84840 and
        struct.unpack_from('<Q', image, movement + 0x158)[0] == base + 0xac9dc0,
        'Changed named movement-service lifecycle slots')
    require(call(0xac9e3c) == 0xaa3950 and call(0xac9e44) == 0xc75780,
        'Changed status publication and seed cleanup ordering')
    cr, ci, cb, cn = images['client']
    client_controller = schema(ci, cb, 0x2222498, 'CCSPlayerController')
    require(client_controller['m_bFireBulletsSeedSynchronized'] == 0x97c,
        'Changed client synchronization-status schema')
    return {
        'module_sha256': PINS,
        'server_custom_seed': {'name_rva': hex(name), 'wrapper_rva': hex(wrapper),
            'registration_default_bool': True, 'registration_flags_raw': hex(registration_flags),
            'runtime_value_observed': False, 'controller_predicate': 'CBasePlayerController::m_bPredict',
            'controller_predicate_offset': hex(controller['m_bPredict']),
            'producer': 'SHA1(double Plat_FloatTime + constant, int32 RandomInt(0, INT_MAX), int32 zero), first LE32',
            'baseline_seed_global_rva': hex(baseline), 'alternate_seed_global_rva': hex(alternate),
            'selector': 'alternate != baseline returns alternate; equality hashes quantized pitch, yaw and shot tick',
            'stream_seed': 'selected result + 1, modulo 2^32'},
        'synchronization_status': {'server_member': hex(cs_controller['m_bFireBulletsSeedSynchronized']),
            'client_member': hex(client_controller['m_bFireBulletsSeedSynchronized']),
            'meaning_verified': 'server publishes whether baseline and alternate globals are equal',
            'seed_value_replication_established': False},
        'native_windows': WINDOWS,
        'limitations': ['Installed files only; active server build, runtime Cvar and prediction flags are unobserved.',
            'No proof that the custom-seed branch ran in any supplied session.',
            'No universal dedicated-versus-listen discriminator was established.',
            'The client helper has no input from the independently generated server command seed.',
            'The replicated synchronization boolean does not supply the server seed value.'],
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=GAME)
    parser.add_argument('--server', type=Path)
    parser.add_argument('--client', type=Path)
    args = parser.parse_args()
    images = {module: mapped(path or args.game / 'csgo/bin/win64' / (module + '.dll'))
        for module, path in [('server', args.server), ('client', args.client)]}
    print(json.dumps(verify(images), indent=2))

if __name__ == '__main__':
    main()
