"""Read-only, stdlib-only verification of native final local-light GPU packing."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from verify_sky_draw import mapped, require_bytes, primary_vtable

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT)
    parser.add_argument('--header', type=Path, default=ROOT / 'include/world_lighting_offsets.h')
    args = parser.parse_args()
    raw, image, base, nt = mapped(args.game / 'bin/win64/scenesystem.dll')
    digest = hashlib.sha256(raw).hexdigest()
    if digest != '0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b':
        raise SystemExit('Changed scenesystem.dll: investigate before changing lighting.')
    # These identities and instruction locations are frozen independently of the
    # implementation header. A header edit cannot redefine the expected binary.
    scopes = {
        'final_gpu_pack': (0x1B14B0, 0x1B244F, 'bc4994c5c07b3dc4906b337bd56e8ae672b82b60a0cb15674c9089ca5bfa9ee1'),
        'gpu_array_upload': (0x1B2450, 0x1B2612, 'c59b6411baa962e62ba94057c298380d2bd2a88748676470b1bae4a699d20d01'),
        'selected_sun_removal': (0x1A7FD0, 0x1A814B, 'c24ab7cd649d275f8c4f7e15da610b2db9c0f15ed343787aa1f79a58948aad0c'),
        'registered_light_gather': (0x19FE30, 0x1A0F7C, '1413e159d1aca46744a5cda7223b190e34e2d8017d65b70b1c39bef0ba1b1c43'),
    }
    for label, (start, end, expected) in scopes.items():
        if hashlib.sha256(image[start:end]).hexdigest() != expected:
            raise SystemExit(f'Changed complete {label} function scope')
    checks = {
        'pack_entry': (0x1B14B0, '488bc45553488da8e8fdffff4881ec08030000'),
        'vtable_thunk': (0x1B14A0, 'e90b000000'),
        'output_argument': (0x1B14CA, '498bd8'),
        'input_argument': (0x1B14D5, '488bfa'),
        'binner_argument': (0x1B14EF, '4c8bf9'),
        'perview_light_data': (0x1B15DF, '488d4f18'),
        'native_transform': (0x1B1674, 'e8774f2f00'),
        'rgb_inputs': (0x1B177B, 'f30f105d04f30f104d08f30f10550c'),
        'rgb_output': (0x1B17BB, 'f20f119ba0000000f30f1193a8000000'),
        'final_native_blue': (0x1B18C6, 'f30f1193a8000000'),
        'final_native_green': (0x1B18CE, 'f30f118ba4000000'),
        'final_native_red': (0x1B18D9, 'f30f1183a0000000'),
        'only_return': (0x1B2445, '4881c4080300005b5dc3'),
        'gpu_output_stride': (0x1B2519, '4c69c0e0010000'),
        'cpu_input_stride': (0x1B2520, '4869d040060000'),
        'output_array': (0x1B2527, '4c03442448'),
        'input_array': (0x1B252C, '480393b0e30000'),
        'pack_virtual_call': (0x1B2533, '41ff5170'),
        'return_ignored': (0x1B2537, 'ffc7'),
        'upload_array': (0x1B2550, '4c8b442448'),
        'upload_size': (0x1B2561, '4c69c8e0010000'),
        'upload_call': (0x1B2572, '41ff9280030000'),
        'registered_source_rgb': (0x19FF0D, 'f3410f1085a4010000'),
        'registered_record_copy': (0x1A002E, 'e8ddcfeeff'),
        'registered_record_enqueue': (0x1A0F1C, 'e81f8b0200'),
        'sun_existing_removed': (0x1A8064, 'ff8ea8e30000'),
        'sun_fallback_removed': (0x1A812B, 'ff8ea8e30000'),
        'sun_selection_call': (0x1A9B59, 'e872e4ffff'),
        'ordinary_upload_call': (0x1B4569, 'e8e2deffff'),
    }
    require_bytes(image, checks)
    table = primary_vtable(image, base, b'.?AVCLightBinnerGPU@@')
    if table != 0x5FE678:
        raise SystemExit('Changed CLightBinnerGPU primary RTTI table')
    thunk = struct.unpack_from('<Q', image, table + 0x70)[0] - base
    if thunk != 0x1B14A0:
        raise SystemExit('Changed CLightBinnerGPU packing vtable target')
    # Decode branch targets, strides, slot and RGB displacements from installed
    # operands, rather than reading expected values from the header itself.
    observed = {
        'Timestamp': struct.unpack_from('<I', raw, nt + 8)[0],
        'ImageSize': len(image),
        'PackLocalLight': thunk + 5 + struct.unpack_from('<i', image, thunk + 1)[0],
        'PackThunk': thunk,
        'PackReturn': 0x1B2533 + 4,
        'BinnerVtable': table,
        'PackSlot': image[0x1B2536],
        'GpuStride': struct.unpack_from('<I', image, 0x1B251C)[0],
        'GpuRadiance': struct.unpack_from('<I', image, 0x1B17BF)[0],
        'OutputStrideInstruction': 0x1B2519,
        'RadianceStore': 0x1B17BB,
    }
    if struct.unpack_from('<I', image, 0x1B17C7)[0] != observed['GpuRadiance'] + 8:
        raise SystemExit('GPU RGB output no longer forms one contiguous float3')
    if struct.unpack_from('<I', image, 0x1B2564)[0] != observed['GpuStride']:
        raise SystemExit('Upload stride differs from individual GPU output stride')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', args.header.read_text(), flags=re.S)
    for name, value in observed.items():
        match = re.search(rf'\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)', source)
        if not match or int(match[1], 0) != value:
            raise SystemExit(f'Header {name} differs: {value:#x}')
    for name, address in [('PackBytes', 0x1B14B0), ('ThunkBytes', 0x1B14A0),
                          ('CallBytes', 0x1B2533), ('StrideBytes', 0x1B2519), ('RadianceBytes', 0x1B17BB)]:
        match = re.search(rf'\b{name}\[\]\s*\{{([^}}]+)\}}', source)
        blob = bytes(int(v, 0) for v in re.findall(r'0[xX][0-9a-fA-F]+|\d+', match[1])) if match else b''
        if not blob or image[address:address + len(blob)] != blob:
            raise SystemExit(f'Header bytes {name} differ')
    print(json.dumps({'sha256': digest, 'verified_function_scopes': list(scopes), 'verified': list(checks),
                      'constants': {k: hex(v) for k, v in observed.items()},
                      'legacy_cubemap_hook_removed': True, 'live_visual_observation': False}, indent=2))


if __name__ == '__main__':
    main()
