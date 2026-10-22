"""Verify installed native sniper scope parameter contracts, disk-only and stdlib-only."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_HASH = '5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49'
WINDOWS = {
    'producer': (0x8E15E0, 0x2E4, '8de0edcf271d12a3a87e5a8a69a2caac4ca5d7374e3800f728d98d6b1b2f0aa5'),
    'render_caller': (0x11DA2DE, 0x44, 'c3ea1d2912caf9e07cc05d423052466c7188575b281d30e43907df907b477879'),
    'fov_caller': (0xCEFC7B, 0x39, 'af14ef681772ee1cb0b1d7a452a86ef69a1fff4d7d54300bd3d932b2c3e72061'),
    'consumer': (0x11EC870, 0x453, 'd659a1ca91a26af3cde49ed3e8407b36ffe5c8d40a7038dc518b05a5fad360a6'),
    'graph_call': (0x11EAEED, 0x39, '2d78489f3f374451f579e10bf04dcf6697964aafce680e6dad618886a632aba8'),
    'iron_sight_gate': (0x82FE70, 0x117, '5f6b982cb8de095f43b1ff25eec2d0bbbd80209c58d3566e67c059508c722405'),
    'iron_sight_draw_prefix': (0x832170, 0x92, '1c590037023d1f53ae5c7cc139ab458f3864508c900b80a4be3a34efe271bfd6'),
    'iron_sight_draw_epilogue': (0x8335CD, 0x10, '6405f54f93b46f5ad5dd99fc90b97f5db285a9c5f185e72b0093aed1caa90c84'),
    'iron_sight_graph_epilogue': (0x11FAA2D, 0x10, '8e2b5c25e732943f8f82566296d8aaa7329e77f209d48faf571684a4fde57840'),
    'iron_sight_graph': (0x11F9940, 0x10FD, '96325fbdee288211a00de6eaa45e2e0d339b8bd8df4c379300cffaa328a69959'),
    'view_scope_copy': (0x11CD6F6, 0xE, 'ed719318d7ea37d912fa42b9382115a6467f760c31bb259c48766114d82ebda9'),
    'sniper_stencil': (0x11FCAC5, 0x83, 'ca93af7edbf011a8acae879046d10cbb46a532146b869dfd63ef365bf6ea2f1b'),
    'sniper_crop': (0x11EE94B, 0x3F, 'b0b11ec54e52a8ba927f1d2ea48e2d1e0b12943e4fe3e385efe780140c9a3a5c'),
}


def verify_render_data(image, base):
    # October 21 recertification: these instructions stayed at the same RVAs,
    # while their individual native data targets moved. Check the decoded
    # targets' identities as well as the complete updated instruction windows.
    names = {
        0x11EC8E0: ('488d15', b'postprocess_target'),
        0x11EC98B: ('488d15', b'Health Boost Effect'),
        0x11ECB0F: ('488d15', b'Legacy Sniper Scope'),
        0x11F9A08: ('488d05', b'IronSightQuarterRes1'),
        0x11F9A1F: ('488d05', b'IronSightQuarterRes2'),
        0x11F9AA0: ('4c8d0d', b'IronSightResolve'),
        0x11F9D5C: ('488d15', b'IronSight: Blur X'),
        0x11FA235: ('488d15', b'IronSight: Blur Y'),
        0x11FA7B0: ('488d15', b'IronSightToScreen'),
        0x11FCB15: ('488d15', b'SniperZoomStencil'),
    }
    targets = {}
    for at, (opcode, name) in names.items():
        if image[at:at + 3] != bytes.fromhex(opcode):
            raise SystemExit(f'Changed render-data name instruction at {at:#x}')
        target = at + 7 + struct.unpack_from('<i', image, at + 3)[0]
        if image[target:target + len(name) + 1] != name + b'\0':
            raise SystemExit(f'Changed render-data identity: {name.decode()}')
        targets[name.decode()] = hex(target)
    at = 0x11FCAF4
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
    expected = dict(Producer=call(0x11DA30C), RenderCaller=0x11DA302,
                    Consumer=call(0x11EAF1A), ScopeGate=0x11ECA98,
                    RenderReturn=0x11DA30C + 5, OtherReturn=0xCEFCaa + 5,
                    DrawScope=image[0x8E171C],
                    IronSightGate=call(0x8321DD), IronSightDrawCaller=0x8321DA,
                    IronSightGraphCaller=0x11F996E, IronSightDrawReturn=0x8321DD + 5,
                    IronSightGraphReturn=0x11F9971 + 5, IronSightController=-i32(0x82FE82))
    if expected['IronSightGate'] != 0x82FE70 or call(0x11F9971) != expected['IronSightGate']:
        raise SystemExit('Changed shared iron sight render gate')
    if expected['IronSightController'] != 0x1BD0 or -i32(0x8321BE) != expected['IronSightController']:
        raise SystemExit('Changed embedded iron sight controller ownership')
    def pointer(at): return struct.unpack_from('<Q', image, at)[0] - base
    def string(at):
        if not 0 < at < len(image): raise SystemExit('Invalid schema pointer')
        end = image.find(0, at)
        if end < 0 or end-at > 1024: raise SystemExit('Invalid schema string')
        return bytes(image[at:end])
    if (string(pointer(0x221AC98)) != b'C_CSWeaponBase' or pointer(0x221AC98 + 0x28) != 0x221AF80 or
            struct.unpack_from('<H', image, 0x221AC98 + 0x1C)[0] != 57):
        raise SystemExit('Changed weapon schema registration')
    fields = {string(pointer(0x221AF80 + index*32)): i32(0x221AF80 + index*32 + 16) for index in range(57)}
    if fields.get(b'm_IronSightController') != expected['IronSightController'] or fields.get(b'm_iIronSightMode') != 0x1C80:
        raise SystemExit('Changed embedded iron sight schema fields')
    if (image[0x8321E2:0x8321E6] != bytes.fromhex('84c00f84') or
            0x8321E4 + 6 + i32(0x8321E6) != 0x8335CD or
            image[0x11F9976:0x11F997A] != bytes.fromhex('84c00f84') or
            0x11F9978 + 6 + i32(0x11F997A) != 0x11FAA2D):
        raise SystemExit('Changed native false-result early exits before resource allocation')
    if expected['Producer'] != call(0xCEFCaa) or expected['Producer'] != 0x8E15E0:
        raise SystemExit('Changed shared native visual producer')
    if expected['Consumer'] != 0x11EC870 or expected['DrawScope'] != 4:
        raise SystemExit('Changed scope output/consumer')
    # Independently decoded stack output and immediate native copies. This
    # distinguishes the render consumer from the unrelated FOV caller.
    if i32(0x11DA305) != 0x980 or i32(0x11DA314) != 0x984 or i32(0x11DA31E) != 0x176:
        raise SystemExit('Changed temporary scope output ownership/copy')
    view_scope = 0x176 + 0x40  # Current view object starts at native RBP-0x40.
    if any(i32(at) != view_scope for at in [0x11CD6F9, 0x11CD6FF, 0x11FCAC8, 0x11EE94D]):
        raise SystemExit('Changed copied view scope flag / modern stencil/crop consumers')
    if (image[0x11FCACC:0x11FCACE] != bytes.fromhex('747a') or
            image[0x11EE951:0x11EE953] != bytes.fromhex('7427') or
            string(lea(0x11FCB15)) != b'SniperZoomStencil'):
        raise SystemExit('Changed native sniper stencil/crop bypass')
    if image[0x11EC8A1:0x11EC8A5] != bytes.fromhex('f30f1006') or image[0x11EC8A5:0x11EC8A9] != bytes.fromhex('4c8d7604'):
        raise SystemExit('Changed independent boost/scope inputs')
    if 0x11ECA9B + 6 + i32(0x11ECA9D) != 0x11ECC91:
        raise SystemExit('Changed branch around legacy scope parameter/draw setup')
    verify_render_data(image, base)
    for at, name in [(0x11EC98B, b'Health Boost Effect'), (0x11ECB0F, b'Legacy Sniper Scope')]:
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
    for name, at, size in [('Producer', 0x8E15E0, 17), ('RenderCaller', 0x11DA302, 32),
                           ('Consumer', 0x11EC870, 0x4C), ('ScopeGate', 0x11ECA98, 9),
                           ('IronSightGate', 0x82FE70, 15), ('IronSightDrawCaller', 0x8321DA, 16),
                           ('IronSightGraphCaller', 0x11F996E, 16)]:
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
