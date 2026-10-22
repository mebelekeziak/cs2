"""Verify landed grenade area contracts from installed AMD64 client PE only.

No game process, launch, injection or installed-file mutation is performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped, primary_vtable, require_bytes

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game')
SHA256 = '5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49'
SCHEMA = {
    'C_Inferno': (0x223FE48, 0x223FB30, 24, 0x86A0, {
        'm_firePositions': ('FirePositions', 0x1108),
        'm_bFireIsBurning': ('FireBurning', 0x1708),
        'm_fireCount': ('FireCount', 0x1A48),
        'm_bInPostEffectTime': ('FirePostEffect', 0x1A54),
        'm_maxFireHalfWidth': ('FireHalfWidth', 0x866C)}),
    'C_SmokeGrenadeProjectile': (0x2218958, 0x22185E0, 11, 0x1500, {
        'm_nSmokeEffectTickBegin': ('SmokeTickBegin', 0x1360),
        'm_bDidSmokeEffect': ('SmokeDidEffect', 0x1364),
        'm_vSmokeDetonationPos': ('SmokeDetonation', 0x1378),
        'm_bSmokeEffectSpawned': ('SmokeEffectSpawned', 0x13AA)})}
CHECKS = {
    'smoke_start_and_origin': (0x7D8060, '40534883ec208b9160130000488bd985d27508389164130000746380b9aa13000000755af20f1081781300008b8180130000f20f1181a0140000f20f10816c1300008981a81400008b8174130000f20f1181ac1400008981b414000066c781a913000000014881c1b8130000e8afbeffff'),
    'voxel_owner_buffer_occupancy_and_count': (0x7C5F30, '48638f00010000418bf6488b477048c1e1094903cf488b9cc8081000004885db7417f6c301740b8d142e488bcfe85e690100ffc648d1eb75e983c54049ffc781fd008000007cb9'),
    'render_occupancy': (0x7E0200, '49638f0001000048c1e1094803c8498b4770488b84c808100000418bca898d80000000488945b84885c00f84f6060000a8010f84d6060000448bca41c1e1064403c9'),
    'render_other_buffer_density': (0x7E02DC, 'b801000000412b87000100004863c848c1e10f418bc14803c8897da0498b47704803c9897da4f30f108cc808300000'),
    'morton_xyz_decode': (0x7E0242, '418bc125499224098bc8c1e90233c8418bc1d1e881e1c3300c032549922409448bc141c1e8044433c18bc8c1e9024181e00ff0000333c8418bc1c1e80281e1c3300c0325499224098bd1c1ea0433d18bc8c1e90281e20ff0000333c881e1c3300c038bc1c1e80433c1250ff000038bf8c1e70833f88bc2c1e00881e700ff000033c2c1e7082500ff00000bf8418bc0c1e8084133c00fb6c00bf8'),
    'voxel_world_coordinates': (0x7E0EFA, '418b0e0f57c0f30f100de4932f01488b5c2430488b6c24380fb6c1f3480f2ac08bc1c1e8080fb6c0c1e910f30f5c05fb85ce01f30f59c1f30f5886e8000000f30f5805f785ce01f30f1107660f6ec00f5bc00fb6c1f30f5c05d585ce01f30f59c1f30f5886ec000000f30f5805d185ce01f30f114704660f6ec0488bc70f5bc0f30f5c05ae85ce01f30f59c1f30f5886f0000000488b742440f30f5805a585ce01f30f114708'),
    'coordinate_grid_center': (0x7E0FCC, 'c7055285ce0100008041c7054c85ce0100008041c7054685ce0100008041'),
    'coordinate_cell_center': (0x7E1014, 'c7051a85ce0100002041c7051485ce0100002041c7050e85ce0100002041'),
    'fire_world_positions_and_count': (0xD51951, '898c2468010000398f481a00000f8e6f020000418b843c10110000418bf7f2410f10bc3c081100004d8bf7448baf60860000f2450f108c3c481700008984247001000089442438418b843c50170000'),
    'fire_burning_selection': (0xD519FA, '4638bc37081700007554'),
    'fire_visual_half_width': (0xD4F55F, 'f30f108b6c860000f30f1050fcf30f108374860000f30f5cd1'),
    'fire_default_half_width': (0xD410F8, 'c7836c8600000000f041c783708600000000a042'),
    'fire_post_effect': (0xD43953, '4038af541a00000f84b5000000'),
    'smoke_density_normalization': (0x7D90A7, 'f30f5e0d49123001'),
    'smoke_cell_size': (0x1ADA2EC, '0000a041'),
    'smoke_grid_dimensions': (0x7E00FB, 'ba20000000'),
    'fire_capacity': (0xD51A98, '83f840'),
}

def verify(image, base, nt, raw, header):
    require_bytes(image, CHECKS)
    def u32(p): return struct.unpack_from('<I', image, p)[0]
    def ptr(p): return struct.unpack_from('<Q', image, p)[0] - base
    def string(p):
        if not 0 < p < len(image): raise SystemExit('Invalid schema string pointer')
        end = image.find(0, p, min(p+256, len(image)))
        if end < 0: raise SystemExit('Unterminated schema string')
        return bytes(image[p:end]).decode('ascii')
    values = {'ClientTimestamp': struct.unpack_from('<I', raw, nt+8)[0], 'ClientImageSize': len(image)}
    for name, (registration, array, count, size, fields) in SCHEMA.items():
        if (string(ptr(registration)) != name or ptr(registration+0x28) != array or
                struct.unpack_from('<H', image, registration+0x1C)[0] != count or
                u32(registration+0x18) != size):
            raise SystemExit('Changed schema registration: '+name)
        actual = {string(ptr(array+i*32)): u32(array+i*32+16) for i in range(count)}
        for field, (symbol, offset) in fields.items():
            if actual.get(field) != offset: raise SystemExit('Changed schema field: '+field)
            values[symbol] = offset
    values.update({
        'InfernoVtable': primary_vtable(image, base, b'.?AVC_Inferno@@'),
        'SmokeVtable': primary_vtable(image, base, b'.?AVC_SmokeGrenadeProjectile@@'),
        'MaxFires': 64, 'SmokeSimulation': 0x13B8, 'SmokeVoxelOwner': 0x70,
        'SmokeBuffer': 0x100, 'SmokeOrigin': 0xE8, 'SmokeOccupancy': 0x1008,
        'SmokeDensity': 0x3008, 'SmokeOccupancyBytes': 0x1000,
        'SmokeDensityBufferBytes': 0x80000, 'SmokeDensityStride': 16,
        'SmokeGridSize': 32, 'SmokeVoxelCount': 32768,
        'SmokeCellSize': struct.unpack_from('<f', image, 0x1ADA2EC)[0],
        'SmokeGridCenter': struct.unpack_from('<f', image, 0x7E0FD2)[0],
        'SmokeCellCenter': struct.unpack_from('<f', image, 0x7E101A)[0]})
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in values.items():
        matches = re.findall(r'\b'+name+r'\s*=\s*(0[xX][0-9A-Fa-f]+|[0-9]+(?:\.[0-9]+)?)[fF]?\s*;', source)
        if len(matches) != 1 or (int(matches[0], 0) if matches[0].lower().startswith('0x') else float(matches[0])) != value:
            raise SystemExit('Header mismatch: grenade_area_offsets::'+name)
    return values

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT)
    parser.add_argument('--header', type=Path, default=ROOT/'include/grenade_area_offsets.h')
    args = parser.parse_args()
    path = args.game/'csgo/bin/win64/client.dll'
    raw, image, base, nt = mapped(path)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SHA256: raise SystemExit('Installed client changed; independently reverify grenade area contracts.')
    values = verify(image, base, nt, raw, args.header)
    print(json.dumps({'client': str(path), 'sha256': digest, 'machine': 'AMD64',
        'live_observation': False, 'header': str(args.header), 'contracts': values,
        'machine_checks': list(CHECKS)}, indent=2))

if __name__ == '__main__': main()
