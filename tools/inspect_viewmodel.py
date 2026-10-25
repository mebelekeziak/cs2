"""Verify installed native viewmodel parameter outputs and scoped FOV projection.

Reads installed files only. Standard library; no process access or game changes.
"""
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
    'complete_view_builder': (0xb9d7b0, 0x73c, 'eb1ae6c060abadec8a44f7186728cb93d46f8fb481046e6f2081d87d91740a86'),
    'parameters': (0x8cfa30, 0x18e, 'a72ed0757406b4179606fffb2d0ce4b45958d8c5c93dee12fd2f1ad83a9c17d0'),
    'fov_getter': (0xcf1270, 0x148, 'b5985c159e15186c82917f87bb9be45951ecf36e18797c36784cc95a0917bdb2'),
    'viewmodel_fov_producer': (0xb9db8b, 0x6b, '4d3074577d857b475e205cda19eb66f5ac06438e54d2f2af7448d02c8c278d3e'),
    'transform': (0x10ca7b4, 0x17a, '80b0d4c40e6f0028c7dd8fc18dcfdf0ca1e0b83aa7569515e12968ae491d56a3'),
    'viewmodel_projection': (0xc783c0, 0xdf, 'aa50de09303e56fab9ac2cb24b75ca68c62ba9c31b94e401304cf2f0ac9f7046'),
    'register_x': (0xb338e, 0x23, '536f24f257aa10952d2616b8fc47076473f5f3b7eff0c120ea6fed4168a045ab'),
    'register_y': (0xb344e, 0x23, 'f498f3f4e469490af2ade20f01cc83bd7ff7cf1384587d63adff8926cdcf509e'),
    'register_z': (0xb350e, 0x23, '5d31463affa827967efadbc715d399d29dffc4c2406bbc74f03154ccda1ae4fe'),
    'register_fov': (0xdfd3e, 0x2a, '7d07698ada93b1fc9b3a080b687b4e8e5f3045a832cc7227390896242a9aa1aa'),
    'projection_adjustment': (0xb9de1b, 0x5b, '006801a0c76161ec2f99d723dbaf57115bc22c4ddb84f34f07568e7e0be5c049'),
    'projection_math': (0x1751f40, 0x3a, 'd8ec7f342a9638b0c0bbfe488d6f4d2118da078f4326a0437f86716e1e8dff82'),
    'engine_aspect_getter': (0xb9dc92, 0x19, '49f553d26b86fe57321b78a0048d029216cddc06163ae19ae4d329cebf14116d'),
}


def verify(image, base, header):
    def u32(at): return struct.unpack_from('<I', image, at)[0]
    def rip(at, size=7, displacement=3):
        return at + size + struct.unpack_from('<i', image, at + displacement)[0]
    def call(at):
        if image[at] != 0xe8: raise SystemExit(f'Changed relative call at {at:#x}')
        return rip(at, 5, 1)
    def string(at):
        end = image.find(0, at)
        if not 0 < at <= end < at + 1024: raise SystemExit('Invalid cvar name')
        return bytes(image[at:end]).decode('ascii')
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at+size]).hexdigest() != digest:
            raise SystemExit(f'Changed native window: {name}')
    # Bind exact native cvar names to the wrappers used by the parameter getter.
    cvars = {}
    for name, registration, wrapper, consumer in [
        ('viewmodel_offset_x', 0xb338e, 0xb339a, 0x8cfad7),
        ('viewmodel_offset_y', 0xb344e, 0xb345a, 0x8cfae3),
        ('viewmodel_offset_z', 0xb350e, 0xb351a, 0x8cfaf4),
        ('viewmodel_fov', 0xdfd3e, 0xdfd4a, 0x8cfb06),
    ]:
        if string(rip(registration)) != name or rip(wrapper) != rip(consumer):
            raise SystemExit(f'Changed cvar producer/consumer: {name}')
        cvars[name] = hex(rip(wrapper))
    if call(0xcf12c2) != call(0x10ca7f0):
        raise SystemExit('Native parameter callers disagree')
    table = primary_vtable(image, base, b'.?AVClientModeCSNormal@@')
    getter = struct.unpack_from('<Q', image, table + 0xe0)[0] - base
    builder = struct.unpack_from('<Q', image, table + 15*8)[0] - base
    override = struct.unpack_from('<Q', image, table + 16*8)[0] - base
    if table != 0x1ca1b50 or getter != 0xcf1270 or builder != WINDOWS['complete_view_builder'][0] or override != 0xd04490:
        raise SystemExit('Changed normal-mode base viewmodel FOV slot')
    # Builder saves its mode in the incoming home slot, restores it to RSI,
    # then calls that mode's OverrideView before either viewmodel FOV write.
    if (image[builder:builder+7] != bytes.fromhex('4c8bdc49894b08') or
            image[0xb9da25:0xb9da41+6] != bytes.fromhex('488bb424100100004883c00848898340040000488b06488bd3488bceff9080000000')):
        raise SystemExit('Changed builder/OverrideView ordering or source')
    # Independent producer and projection consumer both identify viewmodel FOV.
    viewmodel_fov = u32(0xb9dbb8 + 3)
    if viewmodel_fov != u32(0xc7844e + 4) or viewmodel_fov == u32(0xc783c7 + 4):
        raise SystemExit('World and viewmodel FOV operands changed')
    expected = {
        'Builder': builder, 'ModeVtable': table, 'ModeSlot': image[0xb973b2],
        'Parameters': call(0xcf12c2), 'ViewmodelFov': viewmodel_fov,
        'ParameterFovReturn': 0xcf12c2 + 5,
        'ParameterTransformReturn': 0x10ca7f0 + 5,
        'FovProducer': WINDOWS['viewmodel_fov_producer'][0],
        'ViewAspect': u32(0xb9de48 + 4),
        'ViewFlags': u32(0xb9de1b + 2),
        'SkipAspectMask': image[0xb9de1b + 6],
        'AspectProducer': WINDOWS['projection_adjustment'][0],
    }
    aspect_scale = struct.unpack_from('<f', image, rip(0xb9de50, 8, 4))[0]
    if aspect_scale != 0.75 or call(0xb9de5b) != call(0xb9de6d) or call(0xb9de6d) != WINDOWS['projection_math'][0]:
        raise SystemExit('Changed native world/viewmodel aspect conversion')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in expected.items():
        matches = re.findall(rf'\b{name}\s*=\s*(0[xX][\da-fA-F]+|\d+)', source)
        if len(matches) != 1 or int(matches[0], 0) != value:
            raise SystemExit(f'Header {name} differs from native evidence')
    scale = re.findall(r'\bAspectScale\s*=\s*(\d+(?:\.\d+)?)f', source)
    if len(scale) != 1 or float(scale[0]) != aspect_scale:
        raise SystemExit('Header AspectScale differs from native evidence')
    for name, at, size in [('ParametersBytes', expected['Parameters'], 21),
                           ('BuilderBytes', expected['Builder'], 19),
                           ('FovProducerBytes', expected['FovProducer'], 0x6b),
                           ('AspectProducerBytes', expected['AspectProducer'], 0x5b)]:
        match = re.search(rf'\b{name}\[\]\s*\{{([^}}]*)\}}', source, re.S)
        actual = bytes(int(v.strip(), 0) for v in match[1].split(',') if v.strip()) if match else b''
        if actual != image[at:at+size]: raise SystemExit(f'Header {name} differs')
    return expected, cvars, aspect_scale


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll'))
    parser.add_argument('--header', type=Path, default=ROOT/'include/viewmodel_offsets.h')
    args = parser.parse_args()
    raw, image, base, _ = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH: raise SystemExit('Client changed: repeat viewmodel investigation')
    constants, cvars, scale = verify(image, base, args.header)
    print(json.dumps({'client_sha256': digest, 'constants': {k: hex(v) for k, v in constants.items()},
        'cvars': cvars, 'windows': WINDOWS,
        'aspect_projection_scale': scale,
        'abi': 'void(pawn*, float[3]*offsets, float*fov); supplied outputs only',
        'scope': 'Static installed-file evidence; live viewmodel appearance unobserved'}, indent=2))


if __name__ == '__main__': main()
