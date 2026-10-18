"""Read-only frozen native KV3/material and installed player-shader verification.

Never opens or launches a game process, edits installed files, or rewrites offsets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable
from inspect_model_swap import exported, cstring
from inspect_surface_tint import extract


def need(condition, reason):
    if not condition:
        raise SystemExit(reason)


def verify(game, header):
    modules, reports = {}, {}
    for key, (path, expected) in IDENTITIES.items():
        raw, image, base, nt = mapped(game / path)
        digest = hashlib.sha256(raw).hexdigest()
        need(digest == expected, f'Changed {key}: repeat player-material investigation')
        modules[key] = raw, image, base, nt
        reports[key] = {'sha256': digest, 'timestamp': hex(struct.unpack_from('<I', raw, nt+8)[0]),
                        'image_size': hex(len(image))}
        for label, (rva, size, frozen) in WINDOWS[key].items():
            need(hashlib.sha256(image[rva:rva+size]).hexdigest() == frozen,
                 f'Changed native material window: {label}')
    cr, c, cb, cn = modules['client']
    mr, m, mb, mn = modules['material']
    tr, t, tb, tn = modules['tier0']
    u32 = lambda image, at: struct.unpack_from('<I', image, at)[0]
    ptr = lambda image, base, at: struct.unpack_from('<Q', image, at)[0] - base
    call = lambda image, at: at + 5 + struct.unpack_from('<i', image, at+1)[0]
    need(primary_vtable(m, mb, b'.?AVCMaterialSystem2@@') == 0x12D920,
         'Changed native material system RTTI')
    need(primary_vtable(m, mb, b'.?AVCMaterial2@@') == 0x129AD0,
         'Changed native material RTTI')
    creator = ptr(m, mb, 0x12D920 + 29*8)
    mode = ptr(m, mb, 0x129AD0 + 0x30)
    need(creator == 0x3ADE0 and mode == 0xBA30, 'Changed material virtual ABI')
    need(call(c, 0x226FD8) == 0x19B4860 and call(c, 0x226FE4) == 0x19C28E0 and
         call(c, 0x227115) == 0x19B4EB0, 'Changed native arena caller lifecycle')
    need(c[0x19B4EB0:0x19B4EB3] == bytes.fromhex('33d2e9') and
         0x19B4EB7 + struct.unpack_from('<i', c, 0x19B4EB3)[0] == 0x19B4F10,
         'Arena destructor no longer selects nondeleting path')
    arena_size = u32(c, 0x19B5007)
    need(arena_size == 0x1190 and c[0x19C2902:0x19C2906] == bytes.fromhex('488d4178'),
         'Changed arena size/root offset')
    generic = 0x1AD2840
    need(cstring(c, ptr(c, cb, generic)) == 'generic' and
         struct.unpack_from('<QQ', c, generic+8) == (0x469806E97412167C, 0xE73790B53EE6F2AF),
         'Changed native generic KV3 format ID')
    parser = exported(t, tr, tn,
        '?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z')
    need(parser == 0x124950 and t[0x124A45:0x124A4A] == bytes.fromhex('4885d27412'),
         'Changed parser export or optional error-string contract')
    need(call(m, 0x3AEAA) == 0xDC00 and call(m, 0x3AF15) == 0xFD00,
         'Changed synchronous material KV conversion/initialization calls')
    # Create's final bool is inverted into ShaderLoadMode_t. True selects
    # enum 0, exactly matching the native clone's ordinary asynchronous path.
    need(m[0x3AEFE:0x3AF15] == bytes.fromhex('440fb6842448010000488bcb804b0b084183f001488b16') and
         m[0x3AD97:0x3ADA0] == bytes.fromhex('4533c0498b16488bcf') and
         call(m, 0x3ADA0) == 0xFD00 and
         m[0xFD00:0xFD05] == bytes.fromhex('4489442418') and
         m[0x10316:0x10329] == bytes.fromhex('448b8c24c0000000488d5424404c8bc3498bcd') and
         call(m, 0x10329) == 0x10700 and
         m[0x10A74:0x10A81] == bytes.fromhex('83bdc8000000000f85b6010000'),
         'Changed CreateMaterial asynchronous shader-load selection/clone parity')
    callback_type = cstring(m, 0x14C950 + 16)
    need('IssueAsyncLoadRequests@CMaterial2' in callback_type and
         'MaterialStaticComboLoadRequest_t' in callback_type and 'ShaderLoadMode_t' in callback_type,
         'Changed native static-combo asynchronous request RTTI')
    # Native CreateMaterial: out RDX->RSI, name R8->RBP, root R9->R14;
    # conversion consumes that root before the call returns.
    need(m[0x3AE04:0x3AE12] == bytes.fromhex('488bf2bae00700004d8bf1498be8') and
         m[0x3AE99:0x3AEAA] == bytes.fromhex('4533c9c6442420014d8bc6488bd5488bcb'),
         'Changed CreateMaterial argument ABI')
    need(m[0xDC55:0xDC5C] == bytes.fromhex('488d4f10488bd3') and
         m[0x3ACE6:0x3ACEA] == bytes.fromhex('f0ff4a20') and
         m[0x3AD0C:0x3AD10] == bytes.fromhex('f0ff4320'),
         'Changed material name or binding reference-count ownership')
    scene = modules['scene'][1]
    need(scene[0x5C1AC:0x5C1B0] == bytes.fromhex('418b4350') and
         scene[0x5C1BB:0x5C1BE] == bytes.fromhex('894598') and
         scene[0x5C295:0x5C2A4] == bytes.fromhex('0f1045980f104da80f11000f114810'),
         'Changed packet tint to native GPU instance copy')
    constants = {'ClientTimestamp': u32(cr, cn+8), 'ClientImageSize': len(c),
        'MaterialTimestamp': u32(mr, mn+8), 'MaterialImageSize': len(m),
        'ArenaConstructor': call(c, 0x226FD8), 'ArenaGetRoot': call(c, 0x226FE4),
        'ArenaDestructor': call(c, 0x227115), 'ArenaSize': arena_size,
        'CreateMaterial': creator, 'MaterialSystemVtable': 0x12D920,
        'MaterialVtable': 0x129AD0, 'MaterialMode': mode, 'LoadKV3String': parser}
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(), flags=re.S)
    for name, observed in constants.items():
        matches = re.findall(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\s*;', source)
        need(len(matches) == 1 and int(matches[0], 0) == observed,
             f'Header mismatch: {name}; independently decoded {observed:#x}')
    for name, encoded in ENTRIES.items():
        matches = re.findall(rf'\b{name}Bytes\[\]\s*\{{([^}}]*)\}}', source, re.S)
        need(len(matches) == 1, f'Missing header byte array {name}')
        actual = bytes(int(v.strip(), 0) for v in matches[0].split(',') if v.strip())
        need(actual == bytes.fromhex(encoded), f'Changed header byte array {name}')
    shaders = verify_assets(game)
    return {'component': 'player_chams_materials', 'verified': True, 'modules': reports,
            'constants': {k: hex(v) for k, v in constants.items()}, 'shaders': shaders,
            'native_windows': {k: list(v) for k, v in WINDOWS.items()},
            'shader_load_mode': 'Create arg6=true -> native enum0 asynchronous; matches native clone',
            'live_observation': False}


def verify_assets(game):
    result = {}
    for mount, path, expected in ASSETS:
        content = extract(game / mount / ('shaders_pc_dir.vpk' if path.startswith('shaders/') else 'pak01_dir.vpk'), path)
        digest = hashlib.sha256(content).hexdigest()
        need(digest == expected, f'Changed installed shader/material input: {path}')
        result[path] = digest
    return result

ROOT = Path(__file__).resolve().parents[1]
GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game")
IDENTITIES = {'client': ('csgo/bin/win64/client.dll', '659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0'),
 'material': ('bin/win64/materialsystem2.dll',
              'b4d5c667eb65f13d9193d743a88eb1f9dfa623cc8f745cef764d9977969e38b6'),
 'tier0': ('bin/win64/tier0.dll', '990ea56bf1626d299ba54b64b423f2e409bd592bb737c7a5edf7972ad5c7143e')}
WINDOWS = {'client': {'ArenaConstructor': (26953824,
                                 190,
                                 '87de68c154a44d92f4d41a9f24aa75884284bc63d2e34861a9e331573a0e4ed4'),
            'ArenaDeletingDestructor': (26955536,
                                        268,
                                        '5335587a75dd3e884c2c1174c5d502ce966637b8d212ef06c0780a26262bd62f'),
            'ArenaDestructor': (26955440,
                                7,
                                '1a92b26d2497313aaa31f19cb07bf971ac24ccd47f6d2dd2923da36137a36890'),
            'ArenaGetRoot': (27011296,
                             43,
                             '842057cc67bf1d51b3b951e3f28f9d79b74446274114774e01615ac46d7c9b3c'),
            'ArenaPoolConstructor': (26954016,
                                     519,
                                     '585ee0944fadf50db411035f5541ca6c457773d6f5a136053d7ce243b9526e4f'),
            'NativeParserCaller': (2256833,
                                   345,
                                   'c41fe5fa47c5069ad6ec451ddc145f83bd07a403cb5d99ca021b7b9679675284')},
 'material': {'CreateMaterial': (241120,
                                 360,
                                 'eeb7aa376d5960285babaf17bca0a0a266391b67b2aa3d97f2d34519ca67ad23'),
              'MaterialInitialize': (64768,
                                     464,
                                     '089ad98d27215c293711096c7fd8766dfe38425de8d7878d7c28c2e60f87f619'),
              'MaterialKVConversion': (56320,
                                       1556,
                                       '8eae13a305e7fbb334eb5a04c341201fa1af8cebf8649062803038850c9f3523'),
              'MaterialMode': (47664,
                               320,
                               'dcb6a4e12ec2ba96f35f894dc12c0ab394b65ce68d436a344f97068bc9ae1c33')},
 'tier0': {'LoadKV3Buffer': (1198624,
                             529,
                             '7f36a85691566f52ef441b35aadc97aee3523e98b7d75b68128e03a7d0118534'),
           'LoadKV3String': (1198416,
                             208,
                             '1aa4615085f73ef6121d2eeda8eddadf2936daddf40924c4ba726b04117351ac')}}
ENTRIES = {'ArenaConstructor': '48895c2408574883ec308bda488bf9488bd1e8a9000000',
 'ArenaDestructor': '33d2e959000000',
 'ArenaGetRoot': '4883ec28f68130110000047515488d15dcb34900488d0de5b34900ff15c7af0e00cc488d41784883c428c3',
 'CreateMaterial': '48895c240848896c2410488974241848897c242041564881ec10010000',
 'LoadKV3String': '48895c2408574883ec704c8bd148c7c0ffffffff48ffc041803c000075f6'}

ASSETS = [('csgo',
  'shaders/vfx/csgo_character_pc_50_features.vcs',
  '18394bbc4562dd2bf00e1acb73b95a9ea334c2f96fc8686288f7126f884a8e40'),
 ('csgo',
  'shaders/vfx/csgo_character_pc_50_vs.vcs',
  '8297bd15a8773137555c038156422172fb6c9b5edd40b6744ddb445fe8cd1554'),
 ('csgo',
  'shaders/vfx/csgo_character_pc_50_ps.vcs',
  'dace4c087abcdcb72e536a77db5609a9a979e94a849e2171dfa8b32bce7053fd'),
 ('core',
  'materials/dev/primary_white_color_tga_21186c76.vtex_c',
  '4cb62dc84b372484a9003c4226747ca6c7c33b64369bbfa4c5a57704e5f181d0'),
 ('core',
  'materials/default/default_normal.vtex_c',
  '94d7d4de69efad0ec26a7a1beb0b1176b4cb1ce3c9e22ee6b83487004acc34f0'),
 ('csgo',
  'shaders/vfx/csgo_unlitgeneric_pc_50_features.vcs',
  '11ef90cb61aa0e6cf47811a828937fc610eaec7adece2a264aa60efe6529fb3c'),
 ('csgo',
  'shaders/vfx/csgo_unlitgeneric_pc_50_vs.vcs',
  '49253137b21a55ee3cdb8a7f7d57036c19bdc64168427cc3f5d5394fb479ac66'),
 ('csgo',
  'shaders/vfx/csgo_unlitgeneric_pc_50_ps.vcs',
  '3c3c2bdad0876533567335b6a5dc07329af5caee51cd7ff3cd13bf0b1c8a603a')]

IDENTITIES["scene"] = ('bin/win64/scenesystem.dll', '040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5')
WINDOWS["scene"] = {'NativePacketTintGPUConsumer': (376480, 3984, '8d6dd1a6cd6d0107a704771d9280b12102c7686879c8190c3a623730e4f44d37')}

WINDOWS["material"]["MaterialBindingRetain"] = (240624, 483, '25c7944649cd5a0265ef8626a834a1bd3d73fce0b959cab685520902b710552c')
WINDOWS["material"]["MaterialInitializeAndLoadMode"] = (0xFD00, 0x955, '5040c737e86ad08cf7e72607f13cbf5898f3e56cc9e156d1e2ac3c4c1d1b7bf2')
WINDOWS["material"]["IssueAsyncLoadRequests"] = (0x10700, 0x724, '25cf289113c324ca7db275cb351c99112a8e34764babd7ed5a46a7cacb58bd06')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=GAME)
    parser.add_argument('--header', type=Path, default=ROOT/'include/player_chams_material_offsets.h')
    args = parser.parse_args()
    print(json.dumps(verify(args.game, args.header), indent=2))
