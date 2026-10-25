"""Verify installed cosmetic camera recoil contracts, file-only.

Uses standard-library Python. Does not launch/open a process or alter game files.
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
    'visual_punch': (0x853c10, 0x299, '24d24f8cd529be19326c80191fb3e0f61dc9d6ebcbd666ce85b6672b04334e00'),
    'camera_base': (0x883090, 0x2ce, '95afe2b22a608f8eeaf45f54caa7a6ada9e095cebf750a15052c8d093e15617a'),
    'camera': (0x883360, 0x4ca, '58163fe3d271efbfbbedaba6768e31f735be4e005f1d5a7defa3df0dcb70cd58'),
    'angle_scale': (0x82d480, 0x26, 'ba98b540c3a4ea5804a27c9ed654fa7c314899505c90a9d82eb131e38f403700'),
    'camera_angle_add': (0x8a7000, 0x2e, '061740a187393fbf223ec7d750b3bd61f1b34e4f9d40760447bddef7488d61a5'),
    'camera_dispatch': (0x883830, 0xfc, '0615b3cd81364d406144dd05a3207fa898d396699c5914dd80a58a24df31f42a'),
    'camera_base_dispatch': (0xa653a0, 0x7d, '7945ed88468766340e8980ad7f866af5fd19ea5350830f93237649b722075007'),
    'gameplay_punch_wrapper': (0x8cd850, 30, 'dce07ab01bb46e80a2a5d0d2d4cba639038eb5d801e75e4c39c12b4f7c06b275'),
    'shot_punch_add': (0x80f168, 73, '91c99d54d6b1f68428b72812f4bde6756fb1a48a4baee3470b63aa4bdcedf8c7'),
    'punch_service_constructor': (0x84b480, 0x149, 'a2f6e917f26d3b431acfb12e4c59aa5571b63fa1d2a12a1034793fcdbefd50e2'),
    'pawn_punch_service_constructor': (0xc716b0, 0x4c, '6bda6ad7f7024cd201e77e3bb04d0ff329696c2df05d03c4ad6799016763e15a'),
    'service_owner_initializer': (0xa85420, 0x21, '2e6e2b7b61863faf0d117a85ed52a80082742c56478c5a94ea20704ba4c29d27'),
    'local_eye_angles': (0xc76160, 0xfe, 'a42623131c08266c6add930faaae50c89bcb2a8afcb2f0236bb872fc6324f54a'),
    'pawn_eye_wrapper': (0x93d320, 0x123, '614492aea98166df5c19a0af9694e11db341ab058eb61ced2e77268157800cc8'),
    'pawn_physical_angles': (0x952870, 0x19, 'e121676694f647940d958184279f017a3fad19c00fc0bf74a8e8f333fbe8602c'),
}
ARRAYS = {
    'VisualPunchBytes': (0x853c10, 0x27),
    'CameraBaseCallerBytes': (0x883232, 0x53),
    'CameraCallerBytes': (0x8835a4, 0x37),
    'CameraBaseCoreBytes': (0x883090, 0x16),
    'CameraCoreBytes': (0x883360, 0x14),
    'AngleAddBytes': (0x8a7000, 0x2e),
    'ViewPunchBaseCallerBytes': (0x88321d, 0x10),
    'ViewPunchCallerBytes': (0x883586, 0xf),
}


def verify(image, base, header):
    def u32(at): return struct.unpack_from('<I', image, at)[0]
    def ptr(at): return struct.unpack_from('<Q', image, at)[0] - base
    def rip(at, length, displacement): return at + length + struct.unpack_from('<i', image, at + displacement)[0]
    def call(at):
        if image[at] != 0xe8: raise SystemExit(f'Changed direct call at {at:#x}')
        return rip(at, 5, 1)
    def string(at):
        if not 0 < at < len(image): raise SystemExit('Invalid schema string address')
        end = image.find(0, at)
        if end < 0 or end - at > 1024: raise SystemExit('Invalid schema string')
        return bytes(image[at:end]).decode('ascii')
    for name, (at, size, digest) in WINDOWS.items():
        if hashlib.sha256(image[at:at + size]).hexdigest() != digest:
            raise SystemExit(f'Changed cosmetic recoil instruction window: {name}')
    registration, array, count = 0x223bac8, 0x223ad40, 105
    if string(ptr(registration)) != 'C_CSPlayerPawn' or ptr(registration + 0x28) != array or struct.unpack_from('<H', image, registration + 0x1c)[0] != count:
        raise SystemExit('Changed C_CSPlayerPawn schema registration')
    fields = {string(ptr(array + i * 32)): u32(array + i * 32 + 16) for i in range(count)}
    visual = call(0x88323e)
    if visual != call(0x8835b3):
        raise SystemExit('Camera cores no longer share the visual punch evaluator')
    if fields['m_pAimPunchServices'] != u32(0x883235) or fields['m_pAimPunchServices'] != u32(0x8835af):
        raise SystemExit('Named punch service differs from camera caller operands')
    # RCX is the service; RDX is caller-owned stack output; RAX returns RDX.
    # The getter executes its native evaluator/memoization before this return.
    if image[visual + 0x1b:visual + 0x21] != bytes.fromhex('488bf2488bf9') or image[0x853e9b:0x853e9e] != bytes.fromhex('488bc6'):
        raise SystemExit('Changed visual punch pointer ABI')
    if image[0x853c60:0x853c65] != bytes.fromhex('48837f3800') or image[0x853c7a:0x853c7e] != bytes.fromhex('488b5738'):
        raise SystemExit('Changed service owning-pawn member')
    table = primary_vtable(image, base, b'.?AVCCSPlayer_AimPunchServices@@')
    if call(0xc716e0) != 0x84b480 or rip(0x84b490, 7, 3) != table or image[0x84b497:0x84b49a] != bytes.fromhex('488903'):
        raise SystemExit('Changed actually constructed punch service primary vtable')
    if u32(0xc716f0) != fields['m_pAimPunchServices'] or call(0xc716f7) != 0xa85420 or image[0xa8543a:0xa8543e] != bytes.fromhex('48895138'):
        raise SystemExit('Changed pawn-owned punch service assignment/owner initialization')
    pawn_table = primary_vtable(image, base, b'.?AVC_CSPlayerPawn@@')
    if ptr(pawn_table + 0x570) != 0xc76160 or ptr(pawn_table + 0x578) != 0x952870 or call(0xc761b5) != 0x93d320:
        raise SystemExit('Changed physical camera eye-angle getter chain')
    if u32(0x952872) != 0x13b0 or u32(0x95287a) != 0x13a8:
        raise SystemExit('Changed underlying physical view-angle copy')
    pawn_array, pawn_count = ptr(0x21f67e8 + 0x28), struct.unpack_from('<H', image, 0x21f67e8 + 0x1c)[0]
    pawn_fields = {string(ptr(pawn_array + i * 32)): u32(pawn_array + i * 32 + 16) for i in range(pawn_count)}
    if pawn_fields.get('v_angle') != u32(0x95287a) or pawn_fields.get('m_pCameraServices') != u32(0x93d336):
        raise SystemExit('Physical camera inputs differ from named base-pawn schema')
    camera_registration, camera_array, camera_count = 0x222d3e8, 0x222d150, 20
    if string(ptr(camera_registration)) != 'CPlayer_CameraServices' or ptr(camera_registration + 0x28) != camera_array or struct.unpack_from('<H', image, camera_registration + 0x1c)[0] != camera_count:
        raise SystemExit('Changed named camera view-punch schema')
    camera_fields = {string(ptr(camera_array + i * 32)): u32(camera_array + i * 32 + 16) for i in range(camera_count)}
    if any(camera_fields.get(k) != v for k, v in {'m_vecCsViewPunchAngle': 0x48, 'm_nCsViewPunchAngleTick': 0x54, 'm_flCsViewPunchAngleTickRatio': 0x58}.items()):
        raise SystemExit('Changed camera view-punch angle/time members')
    if image[0x8831b4] != camera_fields['m_vecCsViewPunchAngle'] or image[0x883520] != camera_fields['m_vecCsViewPunchAngle']:
        raise SystemExit('Camera core copied impulse differs from named view-punch field')
    for name, expected, core in [('CCSPlayerBase_CameraServices', 0x1c0ece0, 0x883090), ('CCSPlayer_CameraServices', 0x1c0f4a8, 0x883360)]:
        camera_table = primary_vtable(image, base, ('.?AV' + name + '@@').encode())
        if camera_table != expected or ptr(camera_table + 0x110) != core or ptr(camera_table + 0x108) != 0x883830:
            raise SystemExit(f'Changed native camera service virtual consumers: {name}')
    if call(0x88390c) != 0xa653a0 or image[0xa65416:0xa6541d] != bytes.fromhex('48ffa010010000'):
        raise SystemExit('Changed camera view dispatch into the verified core virtual slot')
    # The scoped getter is exclusively a visual consumer. Gameplay's current
    # and shot punch remain different entry points (already independently frozen).
    if call(0x8cd860) != 0x8534e0 or call(0x80f17f) != 0x853410 or visual in {call(0x8cd860), call(0x80f17f)}:
        raise SystemExit('Changed separation of visual and gameplay punch')
    if call(0x8835c8) != 0x82d480 or call(0x8835d6) != 0x8a7000:
        raise SystemExit('Changed temporary visual angle scale/add chain')
    scales = [struct.unpack_from('<f', image, rip(at, 8, 4))[0] for at in (0x883243, 0x8835b8)]
    if scales != [struct.unpack('<f', struct.pack('<f', v))[0] for v in (.45, .45)]:
        raise SystemExit('Changed cosmetic visual punch scaling')
    expected = {
        'VisualPunch': visual,
        'CameraBaseReturn': 0x88323e + 5, 'CameraReturn': 0x8835b3 + 5,
        'PunchServices': fields['m_pAimPunchServices'], 'ServiceOwner': image[0x853c7d], 'PunchVtable': table,
        'CameraBaseCaller': 0x883232, 'CameraCaller': 0x8835a4,
        'CameraServices': pawn_fields['m_pCameraServices'],
        'CameraBaseVtable': primary_vtable(image, base, b'.?AVCCSPlayerBase_CameraServices@@'),
        'CameraVtable': primary_vtable(image, base, b'.?AVCCSPlayer_CameraServices@@'),
        'CameraBaseCore': ptr(primary_vtable(image, base, b'.?AVCCSPlayerBase_CameraServices@@') + 0x110),
        'CameraCore': ptr(primary_vtable(image, base, b'.?AVCCSPlayer_CameraServices@@') + 0x110),
        'AngleAdd': call(0x883228), 'ViewPunchBaseCaller': 0x88321d, 'ViewPunchCaller': 0x883586,
        'ViewPunchBaseReturn': 0x883228 + 5, 'ViewPunchReturn': 0x883590 + 5,
    }
    if expected['AngleAdd'] != call(0x883590) or image[0x8a702d] != 0xc3:
        raise SystemExit('Changed complete void three-pointer temporary angle-add contract')
    # Shared camera dispatch receives six outputs but moves stack argument six
    # into R9 before tail-calling the four-argument camera core (world FOV out).
    if image[0xa653f7:0xa653ff] != bytes.fromhex('4c8b4c24584c8bc6') or image[0xa65416:0xa6541d] != bytes.fromhex('48ffa010010000'):
        raise SystemExit('Changed native camera core four-pointer ABI/stack FOV routing')
    source = re.sub(r'//[^\n]*|/\*.*?\*/', '', header.read_text(encoding='utf-8-sig'), flags=re.S)
    for name, value in expected.items():
        matches = re.findall(rf'\b{name}\s*=\s*(0[xX][\da-fA-F]+|\d+)', source)
        if len(matches) != 1 or int(matches[0], 0) != value: raise SystemExit(f'Header {name} differs from native evidence')
    for name, (at, size) in ARRAYS.items():
        match = re.search(rf'\b{name}\[\]\s*\{{([^}}]*)\}}', source, re.S)
        actual = bytes(int(v.strip(), 0) for v in match[1].split(',') if v.strip()) if match else b''
        if actual != image[at:at + size]: raise SystemExit(f'Header {name} differs from native evidence')
    return expected, scales


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll'))
    parser.add_argument('--header', type=Path, default=ROOT / 'include/cosmetic_recoil_offsets.h')
    args = parser.parse_args()
    raw, image, base, _ = mapped(args.client)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_HASH: raise SystemExit('Client changed: repeat cosmetic recoil investigation')
    constants, scales = verify(image, base, args.header)
    print(json.dumps({'client_sha256': digest, 'constants': {k: hex(v) for k, v in constants.items()},
        'windows': WINDOWS, 'visual_scales': scales,
        'abi': ['float[3]*(AimPunchServices*, float[3]*output), original RAX preserved',
            'void(CameraServices*, float[3]*origin, float[3]*angles, float*fov)',
            'void(const float[3]*left, const float[3]*right, float[3]*output)'],
        'scope': 'File-only verification of cosmetic camera outputs; operator observation recorded separately; gameplay state preserved'}, indent=2))


if __name__ == '__main__': main()
