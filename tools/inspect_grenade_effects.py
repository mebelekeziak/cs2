"""Verify installed smoke/flash rendering contracts; disk-only, stdlib Python."""
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
    'smoke_draw': (0xD20410, 0xEE, '612409cbd3e21fa6e1b7d11a103bd97a3bb3a2b746d7ca49ad4e5d2b55be7647'),
    'flash_resolve': (0x1211C80, 0x77, 'd80924f32261780a93f665fd17d3000e943d03b7aad20c42110615d842120e54'),
    'smoke_eye_opacity': (0xCA06E0, 0x557, '5b5e77933f94c041a4afd7d211d81d87d9b245af0bd96e2d5b03799a3fccb8f0'),
    'smoke_opacity_consumer': (0xC689C3, 0x52, 'd1847e479ad8a944b32dfb4426a255f2aeec0ae6335c20f3396539e69fee43f6'),
    'flash_alpha_producer': (0x11D9BE0, 0x6B, '8f3e335170ea1544f0e279a8ebdd0cb24704deafa73e7a8316c1d5062be6eb5b'),
}


def verify(image, base, header):
    def u32(at): return struct.unpack_from('<I', image, at)[0]
    def ptr(at): return struct.unpack_from('<Q', image, at)[0] - base
    def call(at):
        if image[at] != 0xE8: raise SystemExit(f'Changed call opcode at {at:#x}')
        return at + 5 + struct.unpack_from('<i', image, at + 1)[0]
    tables = {
        'SmokeDescriptor': primary_vtable(image, base, b'.?AVCSmokeVolumeSceneObjectDesc@@'),
    }
    if tables != {'SmokeDescriptor': 0x1CA82E8}:
        raise SystemExit('Changed smoke/flash descriptor RTTI')
    expected = dict(tables,
        SmokeDrawSlot=1,
        SmokeDraw=ptr(tables['SmokeDescriptor'] + 8),
        FlashSetFloat4=call(0x11D9C16), FlashProducer=0x11D9BE0,
        FlashOverlayReturn=0x11D9C1B, FlashScreenshotReturn=0x11D9C4B,
        FlashOverlayKey=u32(0x11D9BF5), FlashScreenshotKey=u32(0x11D9C29),
        SmokeOpacity=call(0xC689C6), SmokeOpacityCaller=0xC689C3, SmokeOpacityReturn=0xC689CB)
    if expected['SmokeDraw'] != 0xD20410 or expected['FlashSetFloat4'] != 0x16AE20 or expected['SmokeOpacity'] != 0xCA06E0:
        raise SystemExit('Changed smoke/flash render targets')
    if call(0xD20497) != 0xD21940 or call(0xD204E0) != 0xD20500:
        raise SystemExit('Changed smoke DrawMask/normal pass dispatch')
    # Caller receives XMM0 and naturally publishes it to the same member used
    # by the native previous-opacity read; these are not injected field writes.
    if u32(0xC689D8) != 0x1530 or u32(0xC68A11) != 0x1530:
        raise SystemExit('Changed native smoke opacity publication')
    if u32(0x11D9C03) != 0x1504 or u32(0x11D9C1F) != 0x1500:
        raise SystemExit('Changed flash white/screenshot alpha inputs')
    if call(0x11D9C16) != 0x16AE20 or call(0x11D9C46) != 0x16AE20:
        raise SystemExit('Changed flash alpha parameter producer')
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at + size]).hexdigest() != digest:
            raise SystemExit(f'Changed native rendering window: {name}')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in expected.items():
        matches = re.findall(rf'\b{name}\s*=\s*(0[xX][\da-fA-F]+|\d+)\s*;', source)
        if len(matches) != 1 or int(matches[0], 0) != value:
            raise SystemExit(f'Header differs: {name}')
    for name, at, size in [('SmokeDrawBytes', 0xD20410, 24), ('FlashSetFloat4Bytes', 0x16AE20, 15),
                           ('FlashProducerBytes', 0x11D9BE0, 0x6B),
                           ('SmokeOpacityBytes', 0xCA06E0, 15), ('SmokeOpacityCallerBytes', 0xC689C3, 17)]:
        match = re.search(rf'\b{name}\[\]\s*\{{([^}}]*)\}}', source, re.S)
        actual = bytes(int(v.strip(), 0) for v in match[1].split(',') if v.strip()) if match else b''
        if actual != image[at:at + size]: raise SystemExit(f'Header bytes differ: {name}')
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll'))
    parser.add_argument('--header', type=Path, default=ROOT / 'include/grenade_effect_offsets.h')
    args = parser.parse_args()
    raw, image, base, _ = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH: raise SystemExit('Changed client: repeat smoke/flash rendering investigation')
    constants = verify(image, base, args.header)
    print(json.dumps({'client_sha256': digest, 'constants': {k: hex(v) for k, v in constants.items()},
        'windows': WINDOWS, 'runtime_observed': False,
        'abi': ['smoke: uintptr_t(desc*,ctx*,packets*,int,arg5*,view*,stats*)',
                'flash writer: uintptr_t(collection*,uint32 key,const float4*)', 'smoke opacity: float(pawn*)'],
        'scope': 'Static installed rendering evidence; no entity/voxel writes'}, indent=2))


if __name__ == '__main__': main()
