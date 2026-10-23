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
CLIENT_CHECKS = {'native_animatable_descriptor_name': (11298204, '4c8d0d45e51701'),
 'native_skeleton_interface_assignment': (11298265, '4885f6488d8630010000480f44c548898310010000'),
 'native_serialized_owner_assignment': (11298392,
                                        '488b4e304885c97433488b49104885c9742a448b4110baff7f00008b4930418bc083e101c1e80f2bc1418bc823cac1e00f4183f8ff0f45d10bc2eb05b8ffffffff8983c0000000')}
SCENE_CHECKS = {'generate_entry': (513984, '488bc4488958204c8940184889501048894808'),
 'fourth_argument_list': (514041, '4d8be9'),
 'third_argument_args': (514052, '498bf0'),
 'second_argument_object': (514063, '4c8bf2'),
 'scene_view_argument': (514143, '488b4e10'),
 'packet_identity_and_tuple': (517591,
                               '894350498d493048894b40488b4da848894b304c89134c8953084c894b184c8953204c895328'),
 'native_material_variant': (518011,
                             '4c8963204c8963288843614d85e474214885d2741c498b04244883c230498bccff50304c8b8d380100004533d248894328'),
 'native_packet_stride': (517236, '496bcf70'),
 'animatable_descriptor_static_initialization': (14242, '488d05e7245e00488905b01e6700'),
 'animatable_descriptor_registration': (942635, '4c8d352ef45800'),
 'aggregate_descriptor_registration': (943918, '4c8d056be55800488bcb488d15b98d5000e86c0fffff'),
 'native_packet_render_order': (517947, '8b4640894358'),
 'full_sort_render_order_first': (1111526, '3beb74080f9cc0e9c5010000'),
 'full_sort_descriptor_tie_break': (1111538, '498b47184c8b49184c3b481874080f92c0'),
 'full_sort_partition_descriptor': (1114136, '488b01488b5818488b4170483b581874080f92c0'),
 'batch_sort_descriptor_first': (1119183, '4c3b501874080f92c0'),
 'batch_sort_partition_descriptor': (1121863, '488b41b6488b5818488b41264c8b7818493bdf74080f92c0'),
 'batch_sort_render_order_last': (1119579, '3b5f580f9cc0'),
 'scene_layer_sort_policy': (972029, '488b4548a801742d'),
 'scene_layer_no_sort_policy': (972082, 'a8020f8534020000')}


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
    name = 0xAC659C+7+struct.unpack_from('<i', client, 0xAC659F)[0]
    if bytes(client[name:client.find(0, name)]) != b'AnimatableSceneObjectDesc':
        raise SystemExit('Native client scene descriptor name differs')
    animatable_descriptor = 0x37A9+7+struct.unpack_from('<i',scene,0x37AC)[0]
    registered_animatable = 0xE622B+7+struct.unpack_from('<i',scene,0xE622E)[0]
    aggregate_descriptor = 0xE672E+7+struct.unpack_from('<i',scene,0xE6731)[0]
    aggregate_name = 0xE6738+7+struct.unpack_from('<i',scene,0xE673B)[0]
    if (animatable_descriptor != registered_animatable or
        animatable_descriptor != 0x675660 or aggregate_descriptor != 0x674CA0 or
        aggregate_descriptor >= animatable_descriptor or
        struct.unpack_from('<Q',scene,aggregate_descriptor)[0]-scene_base != 0x5E4518 or
        bytes(scene[aggregate_name:scene.find(0,aggregate_name)]) != b'AggregateSceneObjectDesc'):
        raise SystemExit('Native aggregate/animatable descriptor registration order differs')
    constants = {
        'SceneTimestamp': stamp, 'SceneImageSize': len(scene), 'Generate': generate,
        'DescriptorVtable': table, 'OwnerHandle': struct.unpack_from('<I',client,0xAC669B)[0],
        'SkeletonInterface': struct.unpack_from('<I',client,0xAC65EA)[0],
        'SkeletonInterfaceMember': struct.unpack_from('<I',client,0xAC65DF)[0],
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
            'native_ordering': {
                'aggregate_descriptor':hex(aggregate_descriptor),
                'animatable_descriptor':hex(animatable_descriptor),
                'batch_sort':'descriptor unsigned ascending; aggregate before animatable within same layer',
                'full_sort':'signed packet+0x58 ascending first; descriptor unsigned ascending for equal order',
                'no_sort':'layer flag2 bypasses sorting; no ordering guarantee',
                'packet_order_changed':False,
            }, 'live_observation': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=GAME)
    parser.add_argument('--header', type=Path, default=ROOT/'include/player_chams_offsets.h')
    parser.add_argument('--material-header', type=Path, default=ROOT/'include/player_chams_material_offsets.h')
    args = parser.parse_args()
    print(json.dumps(verify(args.game,args.header,args.material_header),indent=2))
