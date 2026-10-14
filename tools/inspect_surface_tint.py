"""Read-only installed shader tint evidence; never opens or modifies CS2.

Requires the official Source2Viewer-CLI 20.0 and dictionary retained under
logs/research/vrf20 (MIT LICENSE retained there), plus the research-only Python
zstandard package. The CLI's DATA reader supports this KV3 metadata; its VFX
decompiler rejects VCS72, so this probe reads metadata and original DXBC only.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
from material_vpk_probe import entries
from material_token_probe import token

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game')
ASSETS = {
    'generic': ('shaders/vfx/generic_pc_50_ps.vcs', 'd3496f6b90df7f1fd70640d6c9c28345d3e2914f32a3fc715bf7ff43676cdc6f'),
    'lightmapped': ('shaders/vfx/csgo_lightmappedgeneric_pc_50_ps.vcs', 'c45b654fb7d7cc53dd48827c8f3428bc0797c6a322a6de6bd87bb04fd4b3773d'),
}


def extract(index, target):
    for filename, archive, offset, length, head, start in entries(index):
        if filename != target:
            continue
        container = index if archive == 0x7fff else index.with_name(index.name.replace('_dir.vpk', f'_{archive:03}.vpk'))
        with container.open('rb') as stream:
            stream.seek(offset + (start if archive == 0x7fff else 0))
            data = head + stream.read(length)
        if len(data) != len(head) + length:
            raise ValueError('Truncated installed asset')
        return data
    raise ValueError(f'Asset not found: {target}')


def objects(text):
    # Metadata objects contain no brace characters in strings in these checked
    # installed files. Only scan direct objects, preserving nested arrays intact.
    at = 0
    while True:
        begin = text.find('{', at)
        if begin < 0:
            return
        depth = 0
        for end in range(begin, len(text)):
            if text[end] == '{':
                depth += 1
            elif text[end] == '}':
                depth -= 1
                if not depth:
                    yield text[begin:end + 1]
                    at = end + 1
                    break
        else:
            raise ValueError('Incomplete metadata object')


def parameters(metadata):
    section = metadata[metadata.index('m_variableDescriptionArray'):metadata.index('m_textureChannelProcessorArray')]
    result = []
    for block in objects(section):
        name = re.search(r'm_szName = "(.*?)"', block)
        source = re.search(r'm_sourceType = (\d+)', block)
        source_name = re.search(r'm_pSourceString = "(.*?)"', block)
        if not name or not source:
            raise ValueError('Unexpected parameter metadata')
        result.append({'name': name[1], 'source_type': int(source[1]), 'attribute': source_name[1] if source_name else ''})
    return result


def disassemble(dxbc):
    compiler = ctypes.WinDLL('d3dcompiler_47.dll')
    method = compiler.D3DDisassemble
    method.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    method.restype = ctypes.c_long
    blob = ctypes.c_void_p()
    source = ctypes.create_string_buffer(dxbc)
    hr = method(source, len(dxbc), 0, None, ctypes.byref(blob))
    if hr < 0 or not blob:
        raise ValueError(f'D3DDisassemble HRESULT {hr}')
    table = ctypes.cast(blob, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    pointer = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(table[3])
    size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(table[4])
    release = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(table[2])
    try:
        return ctypes.string_at(pointer(blob), size(blob)).decode().rstrip('\0')
    finally:
        release(blob)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT)
    parser.add_argument('--research', type=Path, default=ROOT / 'logs/research')
    args = parser.parse_args()
    args.research.mkdir(parents=True, exist_ok=True)
    cli = args.research / 'vrf20/Source2Viewer-CLI.exe'
    if hashlib.sha256(cli.read_bytes()).hexdigest() != 'a4bb4f9945db985efb2cad90a90d06867de5ba7e0447a2a4a69a07dace781028':
        raise ValueError('Research CLI changed; independently inspect its provenance.')
    output = {}
    for label, (filename, expected) in ASSETS.items():
        data = extract(args.game / 'csgo/shaders_pc_dir.vpk', filename)
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f'Installed {filename} changed; repeat shader investigation.')
        asset_path = args.research / f'surface_{label}_ps.vcs'
        asset_path.write_bytes(data)
        metadata = subprocess.run([str(cli), '-i', str(asset_path), '-b', 'DATA'], check=True,
                                  capture_output=True, text=True).stdout
        (args.research / f'surface_{label}_ps.metadata.txt').write_text(metadata)
        descriptions = parameters(metadata)
        output[label] = {'sha256': expected, 'tint_parameters': [dict(index=i, **p) for i, p in enumerate(descriptions) if 'Tint' in p['name'] or p['name'] == 'g_vFlatOverlayColor']}
        if label == 'generic':
            static_data = metadata[metadata.index('m_staticComboData'):metadata.index('m_staticComboIDs')]
            ids_text = metadata[metadata.index('m_staticComboIDs ='):metadata.index('m_byteCodeData =')]
            ids = [int(n) for n in re.findall(r'\b\d+\b', ids_text)]
            bindings = {}
            for identifier, block in zip(ids, objects(static_data)):
                if identifier not in (0, 4):
                    continue
                words = re.search(r'm_dynamicComboVars =\s*\[([^]]+)', block)
                decoded = [(int(n) >> 16, int(n) & 65535) for n in re.findall(r'\d+', words[1])]
                bindings[str(identifier)] = sorted({(descriptions[index]['name'], offset) for index, offset in decoded
                                                   if descriptions[index]['name'] in ('g_vColorTint', 'g_vFlatOverlayColor', 'g_nToolsVisMode')})
            output[label]['static_combo_constant_bindings'] = bindings
            sys.path.insert(0, str(args.research / 'python-deps'))
            import zstandard
            dictionary_path = args.research / 'vrf20/vfx_zstd_2bc2fa87.bin'
            dictionary = dictionary_path.read_bytes()
            if hashlib.sha256(dictionary).hexdigest() != 'e7ab9ea47f213132dce97bcf18c77c5fed91bea765af1ceb4b580ed0bf660f5b':
                raise ValueError('Research shader dictionary changed.')
            offset = struct.unpack_from('<I', data)[0]
            compression, length, packed_length = struct.unpack_from('<iII', data, offset)
            if compression != -3:
                raise ValueError('Unexpected installed shader compression')
            unpacked = zstandard.ZstdDecompressor(dict_data=zstandard.ZstdCompressionDict(dictionary)).decompress(
                data[offset + 12:offset + 12 + packed_length])
            if len(unpacked) != length or unpacked[:4] != b'DXBC':
                raise ValueError('Unexpected original shader bytecode')
            bytecode_size = struct.unpack_from('<I', unpacked, 24)[0]
            assembly = disassemble(unpacked[:bytecode_size])
            (args.research / 'surface-generic-ps0.asm').write_text(assembly)
            tint_mul = 'mul r2.xyz, r2.xyzx, cb0[2].xyzx'
            if tint_mul not in assembly:
                raise ValueError('Original generic shader tint consumer changed')
            output[label]['original_shader_color_multiply'] = tint_mul
    output['context_tokens'] = {name: hex(token(name)) for name in ('g_vColorTint', 'FlatOverlayColor', 'g_nToolsVisMode')}
    output['conclusion'] = 'ordinary generic color tint is an artist material constant; dynamic FlatOverlayColor is only present in tools-visualization variants; no verified general frame-owned tint override'
    output['live_observation'] = False
    print(json.dumps(output, indent=2))


if __name__ == '__main__':
    main()
