"""Frozen installed-file verification of workspace skybox loading and local draw copies.

No process access, game launch, installed-file writes, or external dependencies.
Shared resource/KV3 contracts are verified by inspect_model_swap.py and
verify_player_chams_materials.py in the normal compatibility bundle.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from verify_sky_draw import DEFAULT, ROOT, mapped, primary_vtable, require_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT)
    parser.add_argument('--header', type=Path, default=ROOT / 'include/world_skybox_offsets.h')
    args = parser.parse_args()
    pins = {
        'filesystem': ('bin/win64/filesystem_stdio.dll', '465be859c2cc231666301328939da3ed4f7b087ebdbaad325631b481717da183'),
        'engine': ('bin/win64/engine2.dll', 'e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29'),
        'scene': ('bin/win64/scenesystem.dll', '0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b'),
        'material': ('bin/win64/materialsystem2.dll', '2fe9750177006c24aacd0f86b14efbc00d04ca61d86f27f1b07b263eefb06d69'),
        'resource': ('bin/win64/resourcesystem.dll', '29d503822278305b197c5d4c299b77961f6c5b9c0ab989dae81723c8546fa75f'),
    }
    modules = {}
    for name, (relative, digest) in pins.items():
        raw, image, base, nt = mapped(args.game / relative)
        if hashlib.sha256(raw).hexdigest() != digest:
            raise SystemExit(f'Changed {name} binary: repeat independent skybox investigation.')
        modules[name] = raw, image, base, nt
    raw, fs, fs_base, fs_nt = modules['filesystem']
    engine = modules['engine'][1]
    scene = modules['scene'][1]
    material, material_base = modules['material'][1:3]
    resource, resource_base = modules['resource'][1:3]
    fs_table = primary_vtable(fs, fs_base, b'.?AVCFileSystem_Stdio@@')
    material_table = primary_vtable(material, material_base, b'.?AVCMaterial2@@')
    material_system_table = primary_vtable(material, material_base, b'.?AVCMaterialSystem2@@')
    resource_table = primary_vtable(resource, resource_base, b'.?AVCResourceSystem@@')
    require_bytes(fs, {
        'mount_entry': (0x53BF0, '48896c2410488974241848897c24204154415641574883ec50'),
        'mount_six_arguments': (0x53C09, '8b842498000000418be98bbc24900000004d8bf0894424304c8bfa897c24284c8be1c644242000e8bbf3ffff'),
        'tail_head_selection': (0x533F7, '83bd5802000000498d7370440f44f1'),
        'path_and_id_interning': (0x53195, '488d0de4111c00e857900d004533c9488d5424304c8bc3488d0d6d111c00ff15e7231000'),
    })
    require_bytes(engine, {
        'native_default_mount_args': (0x20E0D2, '33d28954242889542420488d942440010000488b0d2d5e52004c8d050ef4400041b901000000488b01ff90f8000000'),
    })
    path_id_rva = 0x20E0F2 + struct.unpack_from('<i', engine, 0x20E0EE)[0]
    if engine[path_id_rva:path_id_rva + 9] != b'USRLOCAL\0':
        raise SystemExit('Changed native filesystem mount path ID relationship')
    require_bytes(scene, {
        'final_source_only': (0x154FCD, '4963c14c8bf2486bc87049897b184a8b7c01a8'),
        'scene_material_binding': (0x155029, '488b9fd00000004885db0f847d060000'),
        'binding_live_and_value': (0x155039, '8b432085c00f8e72060000488b0b4885c90f8466060000'),
        'sky_material_mode': (0x155050, '488b01498d57304c89a42490040000ff5028'),
        'source_transform_copy_call': (0x155076, '488d4f30488d5424604c89ac2488040000e844663400'),
        'scene_allocation': (0x1557EE, 'ba10010000'),
        'synchronous_transform_copy': (0x49B6D0, '8b014533c089028b41108942048b41208942084489420c8b41048942108b41148942148b41248942184489421c8b41088942208b41188942248b41288942284489422cc3'),
    })
    if struct.unpack_from('<Q', material, material_table + 0x28)[0] - material_base != 0xB8D0:
        raise SystemExit('Changed material sky-variant method')
    # Fresh runtime materials have a native registered binding whose strong
    # count starts at zero. A caller must validate identity and acquire its
    # first reference before applying the renderer's positive-count rule.
    for label, image, start, end, digest in (
        ('material_creator', material, 0x3ADE0, 0x3AF48, 'ae6d100c6686911ef89ab4abd54ace44f9fc0c769dc48072fcd30f58941b3a2a'),
        ('material_registration_wrapper', material, 0x44F20, 0x44F99, '047550e1f61a7f0e8f2e33e598040cc7f0a238f8e4c0ae330c3ad11fd9d790e7'),
        ('resource_registration', resource, 0x17630, 0x178D4, '814431543877c7df3d5590a0ae42b1e3aa80ed05d95db33a2f5cae4c2ff73a5e'),
        ('new_binding_initialization', resource, 0x173C0, 0x1762F, '017cca0924394e11a078d0c23fb9bd1ed4a54b550a4bfd293a8d11d092526f55'),
    ):
        if hashlib.sha256(image[start:end]).hexdigest() != digest:
            raise SystemExit(f'Changed skybox ownership contract: {label}')

    def call(image, at):
        if image[at] != 0xE8:
            raise SystemExit(f'Expected direct native ownership call at {at:#x}')
        return at + 5 + struct.unpack_from('<i', image, at + 1)[0]

    creator = struct.unpack_from('<Q', material, material_system_table + 29 * 8)[0] - material_base
    creator_table = 0x3AE30 + struct.unpack_from('<i', material, 0x3AE2C)[0]
    registration_slot = struct.unpack_from('<I', material, 0x44F71)[0]
    registration = struct.unpack_from('<Q', resource, resource_table + registration_slot)[0] - resource_base
    initializer = call(resource, 0x17898)
    if creator != 0x3ADE0 or creator_table != material_table or call(material, 0x3AEF9) != 0x44F20 or \
            registration_slot != 0xE0 or registration != 0x17630 or initializer != 0x173C0:
        raise SystemExit('Changed material creation-to-binding registration ownership chain')
    require_bytes(material, {
        'created_material_primary_table': (0x3AE29, '488d05b0ec0e00488903'),
        'material_binding_register_dispatch': (0x44F5E, '488b064c8bc5448b4c2450488bd7488bceff90e0000000488903'),
        'native_first_reference_acquisition': (0x3AD08, '48895f60f0ff4320'),
    })
    require_bytes(resource, {
        'initializer_zero_register': (0x173EC, '4533ff'),
        'initializer_zero_ref_value': (0x17411, '418bc7'),
        'initializer_atomic_zero_reference': (0x17428, '874220'),
        'created_material_value_store': (0x173DA, '488b842470010000488d1d61c70400488902'),
    })
    reference_member = resource[0x1742A]
    if reference_member != scene[0x15503B] or material[0x3AD0F] != reference_member:
        raise SystemExit('Native binding initialization, retention and rendering reference members disagree')
    # Complete native sky renderer has one packet read and no saved packet
    # array/source argument; later R8/R9 uses are overwritten local parameters.
    render_hash = hashlib.sha256(scene[0x154FB0:0x1556D9]).hexdigest()
    if render_hash != '56a35244d265ea3cf476debf6ef55e995993e277184f43b9ca6087db47c3faf5':
        raise SystemExit(f'Changed complete sky-render contract: {render_hash}')
    observed = {
        'FilesystemTimestamp': struct.unpack_from('<I', raw, fs_nt + 8)[0],
        'FilesystemImageSize': len(fs), 'FilesystemVtable': fs_table,
        'AddSearchPathSlot': struct.unpack_from('<I', engine, 0x20E0FD)[0],
        'SceneBytes': struct.unpack_from('<I', scene, 0x1557EF)[0],
        'SceneMaterial': struct.unpack_from('<I', scene, 0x15502C)[0],
        'BindingReferences': scene[0x15503B],
        'MaterialResourceType': int.from_bytes(b'vmat', 'little'),
        'TextureResourceType': int.from_bytes(b'vtex', 'little'),
    }
    observed['AddSearchPath'] = struct.unpack_from('<Q', fs, fs_table + observed['AddSearchPathSlot'])[0] - fs_base
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', args.header.read_text(), flags=re.S)
    for name, value in observed.items():
        match = re.search(rf'\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;', source)
        if not match or int(match.group(1), 0) != value:
            raise SystemExit(f'Header {name} differs from decoded evidence {value:#x}')
    match = re.search(r'\bAddSearchPathBytes\[\]\s*\{([^}]+)\}', source)
    blob = bytes(int(v, 0) for v in re.findall(r'0[xX][0-9a-fA-F]+|\d+', match[1])) if match else b''
    if not blob or fs[observed['AddSearchPath']:observed['AddSearchPath'] + len(blob)] != blob:
        raise SystemExit('Header AddSearchPathBytes differs from installed prologue')
    print(json.dumps({'source_constants': {k: hex(v) for k, v in observed.items()},
                      'module_sha256': {k: v[1] for k, v in pins.items()},
                      'fresh_binding_ownership': {'strong_count_initial_value': 0,
                          'strong_count_member': hex(reference_member), 'caller_must_retain_before_render': True,
                          'creator': hex(creator), 'creator_material_table': hex(creator_table),
                          'resource_registration': hex(registration), 'binding_initializer': hex(initializer)},
                      'complete_sky_render_sha256': render_hash, 'live_observation': False}, indent=2))


if __name__ == '__main__':
    main()
