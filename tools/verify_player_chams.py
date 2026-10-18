"""Read-only installed player scene ownership/material packet verification.

Reads installed PE files; never opens or launches a game process or edits offsets.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from verify_world_materials import mapped, HASHES
from inspect_cs2 import VERIFIED_CLIENT_SHA256
from verify_sky_draw import primary_vtable
from verify_player_chams_materials import verify as verify_materials

ROOT = Path(__file__).resolve().parents[1]
GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game')
GENERATE = '488bc4488958204c8940184889501048894808'
CLIENT_CHECKS = {
    'native_animatable_descriptor_name': (0xAC575C, '4c8d0d55d41701'),
    'native_skeleton_interface_assignment': (0xAC5799, '4885f6488d8630010000480f44c548898310010000'),
    'native_serialized_owner_assignment': (0xAC5818, '488b4e304885c97433488b49104885c9742a448b4110baff7f00008b4930418bc083e101c1e80f2bc1418bc823cac1e00f4183f8ff0f45d10bc2eb05b8ffffffff8983c0000000'),
}
SCENE_CHECKS = {
    'generate_entry': (0x7D7C0, GENERATE),
    'fourth_argument_list': (0x7D7F9, '4d8be9'),
    'third_argument_args': (0x7D804, '498bf0'),
    'second_argument_object': (0x7D80F, '4c8bf2'),
    'scene_view_argument': (0x7D85F, '488b4e10'),
    'packet_identity_and_tuple': (0x7E5D7, '894350498d493048894b40488b4da848894b304c89134c8953084c894b184c8953204c895328'),
    'native_material_variant': (0x7E77B, '4c8963204c8963288843614d85e474214885d2741c498b04244883c230498bccff50304c8b8d380100004533d248894328'),
    'native_packet_stride': (0x7E474, '496bcf70'),
}


def require(image, checks):
    for name, (site, encoded) in checks.items():
        expected = bytes.fromhex(encoded)
        if bytes(image[site:site+len(expected)]) != expected:
            raise SystemExit(f'Changed native player chams contract: {name} at {site:#x}')


def verify(game, header, material_header=None):
    client_raw, client, client_base, _ = mapped(game/'csgo/bin/win64/client.dll')
    scene_raw, scene, scene_base, stamp = mapped(game/'bin/win64/scenesystem.dll')
    hashes = {'client.dll': hashlib.sha256(client_raw).hexdigest(),
              'scenesystem.dll': hashlib.sha256(scene_raw).hexdigest()}
    if hashes['client.dll'] != VERIFIED_CLIENT_SHA256 or hashes['scenesystem.dll'] != HASHES['scenesystem.dll']:
        raise SystemExit('Installed player render binaries changed; reinvestigation required')
    require(client, CLIENT_CHECKS)
    require(scene, SCENE_CHECKS)
    table = primary_vtable(scene, scene_base, b'.?AVCAnimatableSceneObjectDesc@@')
    generate = struct.unpack_from('<Q', scene, table+0x20)[0]-scene_base
    name = 0xAC575C+7+struct.unpack_from('<i', client, 0xAC575F)[0]
    if bytes(client[name:client.find(0, name)]) != b'AnimatableSceneObjectDesc':
        raise SystemExit('Native client scene descriptor name differs')
    constants = {
        'SceneTimestamp': stamp, 'SceneImageSize': len(scene), 'Generate': generate,
        'DescriptorVtable': table, 'OwnerHandle': struct.unpack_from('<I',client,0xAC585B)[0],
        'SkeletonInterface': struct.unpack_from('<I',client,0xAC57AA)[0],
        'SkeletonInterfaceMember': struct.unpack_from('<I',client,0xAC579F)[0],
        'PacketSize': scene[0x7E477], 'PacketObject': scene[0x7E5F4],
        'PacketMaterial': scene[0x7E77E], 'PacketMode': scene[0x7E782],
        'PacketColor': scene[0x7E5D9], 'ViewInArgs': scene[0x7D862],
        'ViewMode': scene[0x7E797],
    }
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(), flags=re.S)
    for name, observed in constants.items():
        found = re.findall(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\s*;', source)
        if len(found) != 1 or int(found[0],0) != observed:
            raise SystemExit(f'Header mismatch: {name}, independently decoded {observed:#x}')
    body = re.findall(r'\bGenerateBytes\[\]\s*\{([^}]*)\}',source,re.S)
    if len(body) != 1 or bytes(int(v.strip(),0) for v in body[0].split(',') if v.strip()) != bytes.fromhex(GENERATE):
        raise SystemExit('Header GenerateBytes mismatch')
    materials = verify_materials(game, material_header or ROOT/'include/player_chams_material_offsets.h')
    return {'component': 'player_chams', 'verified': True, 'hashes': hashes,
            'materials': materials,
            'constants': {k:hex(v) for k,v in constants.items()},
            'client_checks': list(CLIENT_CHECKS), 'scene_checks': list(SCENE_CHECKS),
            'live_observation': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=GAME)
    parser.add_argument('--header', type=Path, default=ROOT/'include/player_chams_offsets.h')
    parser.add_argument('--material-header', type=Path, default=ROOT/'include/player_chams_material_offsets.h')
    args = parser.parse_args()
    print(json.dumps(verify(args.game,args.header,args.material_header),indent=2))
