"""Verify exact installed indexed-shot spread seed, time and isolated RNG contracts.
Read-only installed-file inspection. Does not launch CS2 or invoke native code.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_sky_draw import mapped
ROOT = Path(__file__).resolve().parents[1]
CONSTANTS = {'CurrentTick': 11201136,
 'Tier0Timestamp': 0x6ADA3248,
 'Tier0ImageSize': 0x401000,
 'HistoryFraction': 108,
 'HistoryTick': 104,
 'HistoryTimePresent': 6144,
 'RandomGenerate': 0x15E1B0,
 'RandomCtor': 1435552,
 'RandomFloat': 1435776,
 'RandomPattern': 39586936,
 'RandomStateSize': 136,
 'Seed': 13782384,
 'ShotPunch': 8729616,
 'TickConvert': 7717712,
 'Value': 88,
 'ValuePointer': 8}
WINDOWS = {'client': {'ConvertTick': [7717712,
                            65,
                            'cf70ac45d976b144c00276abc0784b58af03fbf748aaba67539bf84e6d4b4837'],
            'ConvertTickInner': [12224144,
                                 163,
                                 'a48aeb15159920e67bc4dda510e94138e9d9a527bb2b9c5030c5c4ac8c2544fc'],
            'CurrentTick': [11201136,
                            63,
                            '4a1dadaf4e59dd4c286fc7263a416a98a30653491160766776d4c9e2551591d4'],
            'CurrentTickDispatch': [12230736,
                                    75,
                                    'f8c5be703c088ce52d418081254fcf3c4c83768a1936bc3e7bc01693ccff9ece'],
            'CurrentTickInner': [7725040,
                                 93,
                                 'eca1ddc5e526f0078cc191c26ae39285831b900fc3f6bbfb88043c033921b4cd'],
            'Floor': [27308632,
                      124,
                      '24529eaf2111f4fe4bd359ffd17c3a9852543e0e5fc0939abf68a1209a56c3ad'],
            'HistoryTimeProducer': [13614352,
                                    117,
                                    '02d6233834d62514bc95cdfb1075a82652c40aa1034e9567a00ccbbcb3d06b1f'],
            'IndexedTimeConsumer': [8447874,
                                    205,
                                    '347471ce9f9ab9002b9b7bf6802af71401f14a67b3d0407f0f4ab3f8037d68e0'],
            'NormalizeAngle': [3519088,
                               102,
                               '6d17163ddcf7234228479700c4fe4301399dd9f1c7138e37bef1430d452dfccb'],
            'PatternRegistration': [1005824,
                                    210,
                                    '67e5d7af84d3eb854a783f388ef0ba2533b7e268ca7ec271c43228d050961a37'],
            'PunchEvaluate': [8709856,
                              2446,
                              '5bb435e5c457bebbee2b449f6be708f469f836e21d84086978853f7cda7ac8ac'],
            'Quantize': [13754240,
                         34,
                         '27e4f06438e2035e6e202e6660291b3eb1c4f45a1b86481229aeba546f30ecb6'],
            'Seed': [13782384,
                     166,
                     'bcc8a4dbc010d695587995487c4873cc9cf6953ce314c804a0ed40760c300d13'],
            'SeedIncrementAndSpreadConsumer': [13782560,
                                               2064,
                                               'deccc2ab56ee261597234aeab7b8988668b5d34ca9a56eb1b2792306b1d86296'],
            'SeedInputConsumer': [8407915,
                                  223,
                                  '12326217c2839b9ce3e2c2fac780b95b999126d59fe94f54b133d53acbfc2059'],
            'ShotPunch': [8729616,
                          206,
                          '8607d641d2416e400811a15c3336cf9257d957b0d5ed613ebc863b0de78288ef'],
            'ShotPunchConsumer': [8450408,
                                  73,
                                  '91c99d54d6b1f68428b72812f4bde6756fb1a48a4baee3470b63aa4bdcedf8c7'],
            'SingleProjectileFallback': [8186480,
                                         198,
                                         '0b5fb705e53eac1458d052ec676bd6f4d4f6746262915d3d3868f6168e664901'],
            'SpreadDistribution': [13784624,
                                   1420,
                                   'ad1abc396b82ec66dba50abd83371b0b2b9db84d718c3d044e241f80c90ff349']},
 'tier0': {'GlobalFloat': [1430048,
                           239,
                           '27a0c65df80190b33eb274685641c51a4622101db098eec5a5e9077bd44e4fda'],
           'GlobalSeed': [1429856,
                          188,
                          '0ce535ff8c144ad2e48e4880d8915a9bf474f467f3be1e39656ff105504d0eca'],
           'RandomConstructor': [1435552,
                                 22,
                                 '395496840a581b32cf038840cca7b33853923bbbc8e6d526b60d0e7f2726e05d'],
           'RandomFloat': [1435776,
                           79,
                           'eab3848aeecedf5c8a15517ca2c163b9e9b3b7989b423308efdab83d57a20d36'],
           'RandomGenerate': [1434032,
                              1488,
                              '73daa3ac32330e64c8d8469223a66a6dff3e0077ec5aaa98bce304df1bf734f1'],
           'ShaFinal': [367808,
                        896,
                        '776e50291c69f55d1b49d4e2064682a22337993fdebd4d615b52e3a9b9f07234'],
           'ShaReset': [360288,
                        48,
                        'cfb1ff1ec1852f8a7879a0b683bc593634d1522645a6ee33f498f00ef0ad677a'],
           'ShaUpdate': [366304,
                         240,
                         '449e0037e49b800dc333801b2a5fdbd53ac67fe326e05aa24a2c3c2e95539fc1']}}
PINS = {'client': 'ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5',
 'tier0': 'f96f4f9666be66ee3dd42a015b305d954b5dd5582b0608a74e50548df426584d'}
PREFIXES = {'RandomGenerate': ['tier0', 0x15E1B0, 24],
 'CurrentTick': ['client', 11201136, 24],
 'RandomCtor': ['tier0', 1435552, 22],
 'RandomFloat': ['tier0', 1435776, 24],
 'Seed': ['client', 13782384, 24],
 'ShotPunch': ['client', 8729616, 24],
 'TickConvert': ['client', 7717712, 24]}

def string(image, at):
    end=image.find(0,at,min(at+1024,len(image)))
    if at<0 or end<at: raise SystemExit('Invalid contract string')
    return bytes(image[at:end]).decode('ascii')
def exports(image, raw, nt):
    table=struct.unpack_from('<I',raw,nt+24+112)[0]
    _,count,functions,names,ordinals=struct.unpack_from('<IIIII',image,table+20)
    return {string(image,struct.unpack_from('<I',image,names+i*4)[0]):
        struct.unpack_from('<I',image,functions+struct.unpack_from('<H',image,ordinals+i*2)[0]*4)[0] for i in range(count)}
def imports(image,raw,nt):
    result={}; start=struct.unpack_from('<I',raw,nt+24+112+8)[0]
    for d in range(start,start+8192,20):
        original,_,_,name,iat=struct.unpack_from('<IIIII',image,d)
        if not original and not iat: break
        for i in range(10000):
            target=struct.unpack_from('<Q',image,(original or iat)+i*8)[0]
            if not target: break
            result[iat+i*8]=(string(image,name), '#ordinal' if target>>63 else string(image,target+2))
    return result
def verify(images, header):
    for mod,(raw,image,base,nt) in images.items():
        if hashlib.sha256(raw).hexdigest()!=PINS[mod]: raise SystemExit(f'{mod} changed: repeat indexed-shot spread investigation')
        for name,(at,size,digest) in WINDOWS[mod].items():
            if hashlib.sha256(image[at:at+size]).hexdigest()!=digest: raise SystemExit(f'Changed {mod} native contract: {name}')
    raw,image,base,nt=images['client']
    def call(at):
        if image[at]!=0xE8: raise SystemExit(f'Expected native call at {at:#x}')
        return at+5+struct.unpack_from('<i',image,at+1)[0]
    def rip(at): return at+7+struct.unpack_from('<i',image,at+3)[0]
    expected=dict(CONSTANTS)
    expected.update(Seed=call(0x804B99), TickConvert=call(0x80E7A6),CurrentTick=call(0x80E7C8),ShotPunch=call(0x80F17F),
        HistoryTick=image[0x80E79F],HistoryFraction=image[0x80E7A5],
        HistoryTimePresent=(1<<image[0xCFBD70])|(1<<image[0xCFBD77]),RandomPattern=rip(0xF59B3))
    if image[0xCFBD7A]!=expected['HistoryTick'] or image[0xCFBD84]!=expected['HistoryFraction']:
        raise SystemExit('Independent history time producer/consumer disagree')
    if string(image,rip(0xF59AC))!='weapon_accuracy_shotgun_spread_patterns' or rip(0xD2589C)!=expected['RandomPattern']+8:
        raise SystemExit('Named native pattern-order cvar disagrees with consumer')
    if call(0xAAEA8F)!=0x75DFF0 or call(0x75C380)!=0xBA8690 or call(0xD253CB)!=0xD25630:
        raise SystemExit('Changed indexed shot time/spread call chain')
    im=imports(image,raw,nt)
    wanted={0x1AAF8D0:'?Reset@CSHA1@@QEAAXXZ',0x1AAEEB0:'?Update@CSHA1@@QEAAXPEBXI@Z',0x1AAF8C8:'?Final@CSHA1@@QEAAXXZ',
        0x1AAEEA0:'RandomSeed',0x1AAF670:'RandomFloat'}
    if any(im.get(at)!=('tier0.dll',name) for at,name in wanted.items()): raise SystemExit('Changed named SHA1/RNG imports')
    traw,timage,_,tnt=images['tier0']; ex=exports(timage,traw,tnt)
    expected['Tier0Timestamp']=struct.unpack_from('<I',traw,tnt+8)[0]
    expected['Tier0ImageSize']=struct.unpack_from('<I',traw,tnt+24+56)[0]
    for key,name in [('RandomCtor','??0?$CUniformRandomStreamImpl@VCThreadNullMutex@@@@QEAA@H@Z'),
        ('RandomFloat','?RandomFloat@?$CUniformRandomStreamImpl@VCThreadNullMutex@@@@QEAAMMM@Z')]:
        if ex.get(name)!=expected[key]: raise SystemExit('Changed named isolated RNG export: '+key)
    for a in (0x15E894,0x15D295):
        if a+5+struct.unpack_from('<i',timage,a+1)[0]!=0x15E1B0: raise SystemExit('Global/local RNG generator disagreement')
    if struct.unpack_from('<f',timage,0x337808)[0]!=2**-31 or struct.unpack_from('<f',timage,0x337840)[0]!=0.9999998807907104:
        raise SystemExit('Changed native RNG scaling constants')
    source=re.sub(r'//[^\n]*|/\*.*?\*/','',header.read_text(encoding='utf-8-sig'),flags=re.S)
    values={name:int(v,0) for name,v in re.findall(r'\bptrdiff_t\s+(\w+)\s*=\s*(0[xX][\da-fA-F]+|\d+)\s*;',source)}
    if values!=expected: raise SystemExit('Header constants differ from native spread/time/RNG contract')
    for name,(mod,at,n) in PREFIXES.items():
        m=re.search(r'\b'+name+r'Bytes\[\]\s*\{([^}]*)\}',source)
        b=bytes(int(v.strip(),0) for v in m[1].split(',') if v.strip()) if m else b''
        if b!=images[mod][1][at:at+n]: raise SystemExit('Changed runtime fingerprint: '+name)
    return expected

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game'))
    p.add_argument('--client',type=Path);p.add_argument('--tier0',type=Path)
    p.add_argument('--header',type=Path,default=ROOT/'include/aim_spread_offsets.h')
    a=p.parse_args(); images={'client':mapped(a.client or a.game/'csgo/bin/win64/client.dll'),'tier0':mapped(a.tier0 or a.game/'bin/win64/tier0.dll')}
    result=verify(images,a.header)
    print(json.dumps({'module_sha256':PINS,'constants':{k:hex(v) for k,v in result.items()},'native_windows':WINDOWS,
        'seed':'LE32(SHA1(float32(floor(2*normalize(pitch))/2), float32(floor(2*normalize(yaw))/2), int32(shotTick))[:4])+1',
        'scope':'Static installed-file evidence only; indexed primary-fire histories; native function pointers/exports; isolated owned RNG state; no live gameplay claim'},indent=2))
if __name__=='__main__':main()
