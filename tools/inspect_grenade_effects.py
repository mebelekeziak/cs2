"""Verify installed smoke/flash rendering contracts; disk-only, stdlib Python."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_HASH = 'ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5'
WINDOWS = {'smoke_draw': (13769312, 238, '7d962520f8b68d6f4f407c17d50bbd545fc3678ba2bc1cda85a5b805919163fd'),
 'flash_resolve': (18956000, 119, '5c6ddef4e47ac2dcec1692bfca1bb44a177bc171d7f7feb28511ecc1797a9120'),
 'smoke_eye_opacity': (13246432, 1367, 'c55109cc3339e5016a9760d58a18737c17a633129f1917204f0ca0cfbbc6dc10'),
 'smoke_opacity_consumer': (13015347, 82, 'ad0c98f022d68e8a3f1f5cb4754aab0943b908e89b399bf10d9396c89afd2ce6'),
 'flash_alpha_producer': (18726416, 107, 'b75b716f4791c2391dfecaa1a5e504f47bfd584541fe167ae0e1dabd56097d35')}


def verify(image, base, header):
    def u32(at): return struct.unpack_from('<I', image, at)[0]
    def ptr(at): return struct.unpack_from('<Q', image, at)[0] - base
    def call(at):
        if image[at] != 0xE8: raise SystemExit(f'Changed call opcode at {at:#x}')
        return at + 5 + struct.unpack_from('<i', image, at + 1)[0]
    tables = {
        'SmokeDescriptor': primary_vtable(image, base, b'.?AVCSmokeVolumeSceneObjectDesc@@'),
    }
    if tables != {'SmokeDescriptor': 0x1CAA228}:
        raise SystemExit('Changed smoke/flash descriptor RTTI')
    expected = dict(tables,
        SmokeDrawSlot=1,
        SmokeDraw=ptr(tables['SmokeDescriptor'] + 8),
        FlashSetFloat4=call(0x11DBE46), FlashProducer=0x11DBE10,
        FlashOverlayReturn=0x11DBE4B, FlashScreenshotReturn=0x11DBE7B,
        FlashOverlayKey=u32(0x11DBE25), FlashScreenshotKey=u32(0x11DBE59),
        SmokeOpacity=call(0xC69936), SmokeOpacityCaller=0xC69933, SmokeOpacityReturn=0xC6993B)
    if expected['SmokeDraw'] != 0xD21A60 or expected['FlashSetFloat4'] != 0x16AE00 or expected['SmokeOpacity'] != 0xCA1FE0:
        raise SystemExit('Changed smoke/flash render targets')
    if call(0xD21AE7) != 0xD22F90 or call(0xD21B30) != 0xD21B50:
        raise SystemExit('Changed smoke DrawMask/normal pass dispatch')
    # Caller receives XMM0 and naturally publishes it to the same member used
    # by the native previous-opacity read; these are not injected field writes.
    if u32(0xC69948) != 0x1530 or u32(0xC69981) != 0x1530:
        raise SystemExit('Changed native smoke opacity publication')
    if u32(0x11DBE33) != 0x1504 or u32(0x11DBE4F) != 0x1500:
        raise SystemExit('Changed flash white/screenshot alpha inputs')
    if call(0x11DBE46) != 0x16AE00 or call(0x11DBE76) != 0x16AE00:
        raise SystemExit('Changed flash alpha parameter producer')
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at + size]).hexdigest() != digest:
            raise SystemExit(f'Changed native rendering window: {name}')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in expected.items():
        matches = re.findall(rf'\b{name}\s*=\s*(0[xX][\da-fA-F]+|\d+)\s*;', source)
        if len(matches) != 1 or int(matches[0], 0) != value:
            raise SystemExit(f'Header differs: {name}')
    for name, at, size in [('SmokeDrawBytes', 0xD21A60, 24), ('FlashSetFloat4Bytes', 0x16AE00, 15),
                           ('FlashProducerBytes', 0x11DBE10, 0x6B),
                           ('SmokeOpacityBytes', 0xCA1FE0, 15), ('SmokeOpacityCallerBytes', 0xC69933, 17)]:
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
