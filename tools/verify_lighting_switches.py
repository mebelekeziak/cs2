"""Read-only frozen installed-light-gather verification; does not open a process."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, require_bytes

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT)
    parser.add_argument('--header', type=Path, default=ROOT / 'include/world_lighting_switch_offsets.h')
    args = parser.parse_args()
    raw, image, _, nt = mapped(args.game / 'bin/win64/scenesystem.dll')
    digest = hashlib.sha256(raw).hexdigest()
    # 1.41.9.1 recertification compared every frozen site and the complete
    # historical gather disassembly before changing this pin.
    if digest != '040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5':
        raise SystemExit('Changed scenesystem.dll: repeat complete light-gather ABI and lifecycle investigation.')
    checks = {
        'named_registration': (0xe840, '488d15c9cb5e00488d0dc24f9100'),
        'point_entry': (0x19f300, '4889542410555741564883ec50'),
        'point_gate': (0x19f30d, '488b05044578004d8bf0488bfa488be9807858000f843d020000'),
        'point_native_ret': (0x19f564, '4883c450415e5f5dc3'),
        'area_entry': (0x19fe30, '48895424105541544156488dac24d0f5ffff4881ec300b0000'),
        'area_gate': (0x19fe49, '488b05c83978004d8bf04c8be1807858000f840f110000'),
        'area_native_ret': (0x1a0f6f, '4881c4300b0000415e415c5dc3'),
        'frame_clear': (0x1a898b, 'c783d0e3000000000000'),
        'point_direct_caller': (0x1c0cc0, '498b10488b5228e834e6fdff32c0'),
        'area_direct_caller': (0x1c1e9f, '488b164c8bc6488bcb488b5228e87fdffdff'),
    }
    require_bytes(image, checks)
    string = 0xe840 + 7 + struct.unpack_from('<i', image, 0xe843)[0]
    if bytes(image[string:image.find(0, string)]) != b'lb_enable_lights':
        raise SystemExit('Native gate is not the named light-enable switch.')
    point_value = 0x19f30d + 7 + struct.unpack_from('<i', image, 0x19f310)[0]
    area_value = 0x19fe49 + 7 + struct.unpack_from('<i', image, 0x19fe4c)[0]
    wrapper = 0xe847 + 7 + struct.unpack_from('<i', image, 0xe84a)[0]
    if point_value != wrapper + 8 or area_value != point_value:
        raise SystemExit('Native light gates do not read the registered value slot.')
    header = re.sub(r'//[^\n]*|/\*.*?\*/', '', args.header.read_text(), flags=re.S)
    observed = {
        'Timestamp': struct.unpack_from('<I', raw, nt + 8)[0], 'ImageSize': len(image),
        'GatherPointSpot': 0x19f300, 'GatherArea': 0x19fe30,
        'PointGate': 0x19f30d, 'AreaGate': 0x19fe49, 'FrameClear': 0x1a898b,
    }
    for name, value in observed.items():
        match = re.search(rf'\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*;', header)
        if not match or int(match[1], 0) != value:
            raise SystemExit(f'Header constant {name} differs from installed evidence.')
    for name, address in [('PointBytes', observed['GatherPointSpot']), ('AreaBytes', observed['GatherArea']),
                          ('PointGateBytes', observed['PointGate']), ('AreaGateBytes', observed['AreaGate']),
                          ('FrameClearBytes', observed['FrameClear'])]:
        match = re.search(rf'\b{name}\[\]\s*\{{([^}}]+)\}}', header)
        expected = bytes(int(v, 0) for v in re.findall(r'0[xX][0-9a-fA-F]+|\d+', match[1])) if match else b''
        if not expected or image[address:address + len(expected)] != expected:
            raise SystemExit(f'Header byte evidence {name} differs.')
    print(json.dumps({'scenesystem_sha256': digest, 'verified_code': list(checks),
        'source_constants': {k: hex(v) for k, v in observed.items()},
        'native_gate': 'lb_enable_lights', 'value_pointer_slot': hex(point_value),
        'abi': 'void(RCX=binner,RDX=scene light,R8=context); no incoming XMM/R9/stack args',
        'lifecycle': 'native per-view gather list cleared before native scene-light dispatch',
        'live_observation': False}, indent=2))


if __name__ == '__main__':
    main()
