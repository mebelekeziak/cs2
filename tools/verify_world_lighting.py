"""Read-only, stdlib-only proof of installed native local-light radiance path."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from verify_sky_draw import mapped, require_bytes

ROOT=Path(__file__).resolve().parents[1]
DEFAULT=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,default=DEFAULT)
    parser.add_argument('--header',type=Path,default=ROOT/'include/world_lighting_offsets.h')
    args=parser.parse_args()
    raw,image,base,nt=mapped(args.game/'bin/win64/scenesystem.dll')
    digest=hashlib.sha256(raw).hexdigest()
    if digest!='bd770af13a0b1352d87de26cbcfef9e36ef809771ae6a2e5314d7bc6b341f831':
        raise SystemExit('Changed scenesystem.dll: investigate before changing lighting.')
    checks={
        'generator_entry':(0x4A3CE0,'488bc44488482055488da868f7ffff4881ec90090000'),
        'fifth_float':(0x4A3D43,'f30f598dc0080000'),
        'generation_call':(0x1A205C,'e87f1c3000'),
        'original_source_argument':(0x1A1F88,'4c8bc6'),
        'source_rgb':(0x4A3F45,'f20f104604f2410f1186b4000000'),
        'source_b':(0x4A3F53,'8b460c'),
        'output_b':(0x4A3F5E,'418986bc000000'),
        'output_stride':(0x1A2426,'4981c52c010000'),
        'generated_record_transfer':(0x1A2234,'488d4b18498bd5e8d0adeeff'),
        'perview_record_rgb_transfer':(0x8D099,'f20f1082b4000000f20f1181b4000000'),
        'gpu_pack_light_data':(0x1B15DF,'488d4f18'),
        'gpu_pack_transform':(0x1B1674,'e8774f2f00'),
        'gpu_pack_data_rgb_copy':(0x8BF39,'f20f1082b4000000f20f1181b4000000'),
        'gpu_pack_rgb_inputs':(0x1B177B,'f30f105d04f30f104d08f30f10550c'),
        'gpu_pack_rg_store':(0x1B17BB,'f20f119ba0000000'),
        'gpu_pack_b_store':(0x1B17C3,'f30f1193a8000000'),
        'return_count':(0x4A4C76,'4584c9b801000000b9060000000f45c1'),
        'type_switch':(0xCFF67,'486302'),
        'omni_label':(0xCFFBB,'488d1596df5100'),
        'spot_label':(0xD00EB,'488d1556de5100'),
        'directional_label':(0xD021B,'488d1586dd5100'),
        'ortho_label':(0xD0354,'488d153ddc5100'),
        'area_entry':(0x1B0FB0,'48895c2410488974241855574156488bec4881ec80000000'),
        'area_call_a':(0x1B005B,'e8500f0000'),
        'area_call_b':(0x1B00D6,'e8d50e0000'),
        'area_stack_gpu_record':(0x1B003F,'488d9560010000'),
        'area_gpu_stride':(0x1B004B,'488d0cc048c1e104'),
        'area_rgb_source':(0x1B1313,'f20f1086a0020000'),
        'area_rg_store':(0x1B131B,'f2410f114670'),
        'area_b_source':(0x1B1321,'8b86a8020000'),
        'area_b_store':(0x1B1327,'41894678'),
    }
    require_bytes(image,checks)
    table=[struct.unpack_from('<I',image,0xD058C+i*4)[0] for i in range(6)]
    if table[1:5]!=[0xCFF8C,0xD01EC,0xD00BC,0xD031C]:
        raise SystemExit('Changed light type classification jump table')
    observed={
        'Timestamp':struct.unpack_from('<I',raw,nt+8)[0], 'ImageSize':len(image),
        'GenerateSubLights':0x1A2061+struct.unpack_from('<i',image,0x1A205D)[0],
        'PerViewReturn':0x1A2061,
        'SubLightStride':struct.unpack_from('<I',image,0x1A2429)[0],
        'Radiance':struct.unpack_from('<I',image,0x4A3F4F)[0],
        'MaxSubLights':struct.unpack_from('<I',image,0x4A4C7F)[0],
        'PackAreaLight':0x1B0060+struct.unpack_from('<i',image,0x1B005C)[0],
        'AreaReturnA':0x1B0060,'AreaReturnB':0x1B00DB,
        'AreaStride':0x90,'AreaRadiance':image[0x1B1320],
        'Omni':1,'Directional':2,'Spot':3,'Ortho':4,
    }
    source=re.sub(r'//[^\n]*|/\*.*?\*/','',args.header.read_text(),flags=re.S)
    for name,value in observed.items():
        match=re.search(rf'\b{name}\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)',source)
        if not match or int(match[1],0)!=value: raise SystemExit(f'Header {name} differs: {value:#x}')
    for name,address in [('GeneratorBytes',observed['GenerateSubLights']),('CallBytes',observed['PerViewReturn']-5),
                         ('AreaBytes',observed['PackAreaLight']),('AreaCallABytes',observed['AreaReturnA']-5),
                         ('AreaCallBBytes',observed['AreaReturnB']-5)]:
        match=re.search(rf'\b{name}\[\]\s*\{{([^}}]+)\}}',source)
        blob=bytes(int(v,0) for v in re.findall(r'0[xX][0-9a-fA-F]+|\d+',match[1])) if match else b''
        if not blob or image[address:address+len(blob)]!=blob: raise SystemExit(f'Header bytes {name} differ')
    print(json.dumps({'sha256':digest,'verified':list(checks),'constants':{k:hex(v) for k,v in observed.items()},'live_visual_observation':False},indent=2))

if __name__=='__main__':main()
