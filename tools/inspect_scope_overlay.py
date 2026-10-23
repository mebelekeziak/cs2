"""Verify installed native sniper scope parameter contracts, disk-only and stdlib-only."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_HASH = 'ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5'
WINDOWS = {
    'producer': (0x8e2410, 0x2E4, '987708df429b348a1e28699892378c1ba081e7c40ba70e8efe37694ec61ef25f'),
    'render_caller': (0x11dc50e, 0x44, 'f1d0c2095cc47002909152b4049b690dceadf5d35d3c2493ce883f2bd162a771'),
    'fov_caller': (0xcf12db, 0x39, '6b2d322e7f471dc6017464fde3a98006e330f31046eea34b86444bcc683c3d26'),
    'consumer': (0x11eeaa0, 0x453, '8bee6be8893d0a2bb4b9d12fedcee47cf61c2dad4923e11646a8897278e83649'),
    'graph_call': (0x11ed11d, 0x39, 'd8f9883f58be097dbb2fb7366547e8b4ca4de13f4eb3ce8f4f22218f9edff1fb'),
    'iron_sight_gate': (0x830ca0, 0x117, '25da0e6ec21b59f06b2856a0aacc110c54e431aae0413a2c4a3eb8aa2ffa62d4'),
    'iron_sight_draw_prefix': (0x832fa0, 0x92, '0c4ccf74339d36462786e9b69b794f521411b86210dbe4b079e02408cd4a8c4f'),
    'iron_sight_draw_epilogue': (0x8343fd, 0x10, '6405f54f93b46f5ad5dd99fc90b97f5db285a9c5f185e72b0093aed1caa90c84'),
    'iron_sight_graph_epilogue': (0x11fcc8d, 0x10, '8e2b5c25e732943f8f82566296d8aaa7329e77f209d48faf571684a4fde57840'),
    'iron_sight_graph': (0x11fbba0, 0x10FD, '4c335d68dd45703459794cb71a0951624416e71ae793b429c6dae2abed6ab49e'),
    'view_scope_copy': (0x11cf926, 0xE, 'ed719318d7ea37d912fa42b9382115a6467f760c31bb259c48766114d82ebda9'),
    'sniper_stencil': (0x11fed25, 0x83, '4e13218b48b82ccbd7b7671fd7df8ebb4634a3b6146b1383232b2797a2c70041'),
    'sniper_crop': (0x11f0bab, 0x3F, 'b0b11ec54e52a8ba927f1d2ea48e2d1e0b12943e4fe3e385efe780140c9a3a5c'),
}


def verify_render_data(image, base):
    # October 23 rediscovery: identify relocated data through decoded producers,
    # in addition to freezing the complete new instruction windows.
    names = {
        0x11eeb10: ('488d15', b'postprocess_target'),
        0x11eebbb: ('488d15', b'Health Boost Effect'),
        0x11eed3f: ('488d15', b'Legacy Sniper Scope'),
        0x11fbc68: ('488d05', b'IronSightQuarterRes1'),
        0x11fbc7f: ('488d05', b'IronSightQuarterRes2'),
        0x11fbd00: ('4c8d0d', b'IronSightResolve'),
        0x11fbfbc: ('488d15', b'IronSight: Blur X'),
        0x11fc495: ('488d15', b'IronSight: Blur Y'),
        0x11fca10: ('488d15', b'IronSightToScreen'),
        0x11fed75: ('488d15', b'SniperZoomStencil'),
    }
    targets = {}
    for at, (opcode, name) in names.items():
        if image[at:at + 3] != bytes.fromhex(opcode):
            raise SystemExit(f'Changed render-data name instruction at {at:#x}')
        target = at + 7 + struct.unpack_from('<i', image, at + 3)[0]
        if image[target:target + len(name) + 1] != name + b'\0':
            raise SystemExit(f'Changed render-data identity: {name.decode()}')
        targets[name.decode()] = hex(target)
    at = 0x11fed54
    if image[at:at + 3] != bytes.fromhex('488d0d'):
        raise SystemExit('Changed native sniper stencil renderer instruction')
    table = at + 7 + struct.unpack_from('<i', image, at + 3)[0]
    if table != primary_vtable(image, base, b'.?AVCSniperZoomStencilRenderer@@'):
        raise SystemExit('Changed native sniper stencil renderer RTTI binding')
    targets['CSniperZoomStencilRenderer'] = hex(table)
    return targets


def verify(image, header, base=0x180000000):
    def i32(at): return struct.unpack_from('<i', image, at)[0]
    def call(at):
        if image[at] != 0xE8: raise SystemExit(f'Changed call at {at:#x}')
        return at + 5 + i32(at + 1)
    def lea(at):
        if image[at:at + 3] != bytes.fromhex('488d15'):
            raise SystemExit(f'Changed name LEA at {at:#x}')
        return at + 7 + i32(at + 3)
    expected = dict(Producer=call(0x11dc53c), RenderCaller=0x11dc532,
                    Consumer=call(0x11ed14a), ScopeGate=0x11eecc8,
                    RenderReturn=0x11dc53c + 5, OtherReturn=0xcf130a + 5,
                    DrawScope=image[0x8e254c],
                    IronSightGate=call(0x83300d), IronSightDrawCaller=0x83300a,
                    IronSightGraphCaller=0x11fbbce, IronSightDrawReturn=0x83300d + 5,
                    IronSightGraphReturn=0x11fbbd1 + 5, IronSightController=-i32(0x830cb2))
    if expected['IronSightGate'] != 0x830ca0 or call(0x11fbbd1) != expected['IronSightGate']:
        raise SystemExit('Changed shared iron sight render gate')
    if expected['IronSightController'] != 0x1BD0 or -i32(0x832fee) != expected['IronSightController']:
        raise SystemExit('Changed embedded iron sight controller ownership')
    def pointer(at): return struct.unpack_from('<Q', image, at)[0] - base
    def string(at):
        if not 0 < at < len(image): raise SystemExit('Invalid schema pointer')
        end = image.find(0, at)
        if end < 0 or end-at > 1024: raise SystemExit('Invalid schema string')
        return bytes(image[at:end])
    if (string(pointer(0x221ceb8)) != b'C_CSWeaponBase' or pointer(0x221ceb8 + 0x28) != 0x221d1b0 or
            struct.unpack_from('<H', image, 0x221ceb8 + 0x1C)[0] != 57):
        raise SystemExit('Changed weapon schema registration')
    fields = {string(pointer(0x221d1b0 + index*32)): i32(0x221d1b0 + index*32 + 16) for index in range(57)}
    if fields.get(b'm_IronSightController') != expected['IronSightController'] or fields.get(b'm_iIronSightMode') != 0x1C80:
        raise SystemExit('Changed embedded iron sight schema fields')
    # The visual producer's scoped pawn member moved in 1.41.9.2. Establish
    # this from named metadata and its actual operand, not a class-wide delta.
    pawn_registration, pawn_array = 0x223BAC8, 0x223AD40
    if (string(pointer(pawn_registration)) != b'C_CSPlayerPawn' or
            pointer(pawn_registration + 0x28) != pawn_array or
            struct.unpack_from('<H', image, pawn_registration + 0x1C)[0] != 105):
        raise SystemExit('Changed scope-producer pawn schema registration')
    pawn_fields = {string(pointer(pawn_array + index*32)): i32(pawn_array + index*32 + 16)
                   for index in range(105)}
    if (image[0x8E251C:0x8E251E] != bytes.fromhex('80bb') or
            i32(0x8E251E) != pawn_fields.get(b'm_bIsScoped') or
            pawn_fields.get(b'm_bIsScoped') != 0x1EA0):
        raise SystemExit('Changed native scope producer scoped member')
    if (image[0x833012:0x833016] != bytes.fromhex('84c00f84') or
            0x833014 + 6 + i32(0x833016) != 0x8343fd or
            image[0x11fbbd6:0x11fbbda] != bytes.fromhex('84c00f84') or
            0x11fbbd8 + 6 + i32(0x11fbbda) != 0x11fcc8d):
        raise SystemExit('Changed native false-result early exits before resource allocation')
    if expected['Producer'] != call(0xcf130a) or expected['Producer'] != 0x8e2410:
        raise SystemExit('Changed shared native visual producer')
    if expected['Consumer'] != 0x11eeaa0 or expected['DrawScope'] != 4:
        raise SystemExit('Changed scope output/consumer')
    # Independently decoded stack output and immediate native copies. This
    # distinguishes the render consumer from the unrelated FOV caller.
    if i32(0x11dc535) != 0x980 or i32(0x11dc544) != 0x984 or i32(0x11dc54e) != 0x176:
        raise SystemExit('Changed temporary scope output ownership/copy')
    view_scope = 0x176 + 0x40  # Current view object starts at native RBP-0x40.
    if any(i32(at) != view_scope for at in [0x11cf929, 0x11cf92f, 0x11fed28, 0x11f0bad]):
        raise SystemExit('Changed copied view scope flag / modern stencil/crop consumers')
    if (image[0x11fed2c:0x11fed2e] != bytes.fromhex('747a') or
            image[0x11f0bb1:0x11f0bb3] != bytes.fromhex('7427') or
            string(lea(0x11fed75)) != b'SniperZoomStencil'):
        raise SystemExit('Changed native sniper stencil/crop bypass')
    if image[0x11eead1:0x11eead5] != bytes.fromhex('f30f1006') or image[0x11eead5:0x11eead9] != bytes.fromhex('4c8d7604'):
        raise SystemExit('Changed independent boost/scope inputs')
    if 0x11eeccb + 6 + i32(0x11eeccd) != 0x11eeec1:
        raise SystemExit('Changed branch around legacy scope parameter/draw setup')
    verify_render_data(image, base)
    for at, name in [(0x11eebbb, b'Health Boost Effect'), (0x11eed3f, b'Legacy Sniper Scope')]:
        target = lea(at)
        if image[target:target + len(name) + 1] != name + b'\0':
            raise SystemExit('Changed health boost / legacy scope pass identity')
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at + size]).hexdigest() != digest:
            raise SystemExit(f'Changed scope evidence window: {name}')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in expected.items():
        found = re.findall(rf'\b{name}\s*=\s*(0[xX][\da-fA-F]+|\d+)\s*;', source)
        if len(found) != 1 or int(found[0], 0) != value:
            raise SystemExit(f'Header differs: {name}')
    for name, at, size in [('Producer', 0x8e2410, 17), ('RenderCaller', 0x11dc532, 32),
                           ('Consumer', 0x11eeaa0, 0x4C), ('ScopeGate', 0x11eecc8, 9),
                           ('IronSightGate', 0x830ca0, 15), ('IronSightDrawCaller', 0x83300a, 16),
                           ('IronSightGraphCaller', 0x11fbbce, 16)]:
        match = re.search(rf'\b{name}Bytes\[\]\s*\{{([^}}]*)\}}', source, re.S)
        actual = bytes(int(v.strip(), 0) for v in match[1].split(',') if v.strip()) if match else b''
        if actual != image[at:at + size]: raise SystemExit(f'Header bytes differ: {name}')
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll'))
    parser.add_argument('--header', type=Path, default=ROOT / 'include/scope_overlay_offsets.h')
    args = parser.parse_args()
    raw, image, base, _ = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH: raise SystemExit('Changed client: repeat native scope overlay investigation')
    constants = verify(image, args.header, base)
    print(json.dumps({'client_sha256': digest, 'constants': {k: hex(v) for k, v in constants.items()},
                      'windows': WINDOWS, 'abi': ['void(pawn*, temporary visual output*)', 'bool(embedded iron sight controller*)'],
                      'runtime_observed': False,
                      'scope': 'Legacy sniper scope boolean and exact iron sight draw/graph gate results; health boost/FOV/state callers preserved'}, indent=2))


if __name__ == '__main__': main()
