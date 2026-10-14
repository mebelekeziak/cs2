"""Freeze/check installed native world-material evidence without opening CS2.

Standard library only. Reads binary files and source constants; never updates
offsets, writes installed files, launches, loads, or opens a game process.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INSTALL = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64')
HASHES = {
    'scenesystem.dll': 'bd770af13a0b1352d87de26cbcfef9e36ef809771ae6a2e5314d7bc6b341f831',
    'worldrenderer.dll': 'e026fdb32408d1ac3062ab15beee7f87cc6072ee0c4c9b0910c423e0549512e9',
    'materialsystem2.dll': '336abe089468bd5b218f773f09da3942fa9346b7f49f24d9e3591722fc54c5e6',
}

def mapped(path):
    raw = path.read_bytes()
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    machine, count = struct.unpack_from('<HH', raw, pe+4)
    timestamp = struct.unpack_from('<I', raw, pe+8)[0]
    optsize = struct.unpack_from('<H', raw, pe+20)[0]
    base = struct.unpack_from('<Q', raw, pe+48)[0]
    size = struct.unpack_from('<I', raw, pe+80)[0]
    if machine != 0x8664:
        raise SystemExit(f'Expected AMD64: {path}')
    image = bytearray(size)
    for n in range(count):
        section = pe+24+optsize+n*40
        va, rawsize, offset = struct.unpack_from('<III',raw,section+12)
        image[va:va+rawsize] = raw[offset:offset+rawsize]
    return raw, image, base, timestamp

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, default=INSTALL)
    parser.add_argument('--header', type=Path, default=ROOT/'include/world_material_offsets.h')
    args = parser.parse_args()
    modules = {}
    for name, expected in HASHES.items():
        data = mapped(args.installation/name)
        if hashlib.sha256(data[0]).hexdigest() != expected:
            raise SystemExit(f'Installed {name} changed; reinvestigation required')
        modules[name] = data
    source = re.sub(r'//[^\n]*|/\*.*?\*/','',args.header.read_text(),flags=re.S)
    def literal(name):
        found = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\s*[,;]',source)
        if not found:
            raise SystemExit(f'Missing source literal: {name}')
        return int(found.group(1),0)
    raw,image,base,timestamp = modules['scenesystem.dll']
    def rip(site, opcode):
        prefix = bytes.fromhex(opcode)
        if bytes(image[site:site+len(prefix)]) != prefix:
            raise SystemExit(f'RIP operand evidence differs: {site:#x}')
        return site+len(prefix)+4+struct.unpack_from('<i',image,site+len(prefix))[0]
    def expect(site, encoded):
        expected = bytes.fromhex(encoded)
        if bytes(image[site:site+len(expected)]) != expected:
            raise SystemExit(f'Instruction evidence differs: {site:#x}')
    callback = struct.unpack_from('<Q',image,literal('AggregateVtable')+0x20)[0]-base
    fields = {
        'Timestamp': timestamp, 'ImageSize': len(image),
        'MaterialTimestamp': modules['materialsystem2.dll'][3],
        'MaterialImageSize': len(modules['materialsystem2.dll'][1]),
        'AggregateCallback': callback,
        'AggregateVtable': rip(0x3A88A,'488d05'),
        'FlatBinding': rip(0x42189,'488b0d'),
        'Type1Stream': rip(0x421A9,'488b2d'),
        'Type2Stream': rip(0x421B7,'488b2d'),
        'PacketSize': image[0x36B05+3],
        'Material': image[0x421D7+3], 'Mode': image[0x421DE+3],
        'Color': image[0x37214+3],
        'ViewInArgs': image[0x41F53+3], 'ViewFlags': image[0x42160+3],
        'ModeInVtable': image[0x421F1+2],
    }
    for name, observed in fields.items():
        if literal(name) != observed:
            raise SystemExit(f'Source {name}={literal(name):#x}, installed operand={observed:#x}')
    body = re.search(r'CallbackBytes\[\]\s*\{([^}]*)\}',source,re.S).group(1)
    prefix = bytes(int(v.strip(),0) for v in body.split(',') if v.strip())
    if bytes(image[callback:callback+len(prefix)]) != prefix:
        raise SystemExit('Callback entry prefix differs')
    # Four register arguments, append caller, callback return, material mode ABI,
    # packed RGBA constructor, read-only per-view selection and packet fields.
    checks = {
        0x41F49:'4d8bf9', 0x41F50:'498be8', 0x41F57:'488bfa', 0x41F5A:'4c8be9',
        0x42079:'33d2498bcfe82d49ffff', 0x42321:'c3',
        0x36AF6:'4863490c3b4f08', 0x36B11:'48636f103b6f20',
        0x36BBF:'ff4710', 0x36BDA:'488b4718',
        0x421EA:'488b01498d5630ff503048894328',
        0x42164:'480fbae018', 0x4216B:'480fbae020',
        0x42195:'488b09', 0x42198:'66834b6210',
        0x425E2:'488b4f20',
        0x7832D:'488b0574728a0040387058',
        0x49C97C:'8803', 0x49C9DA:'884301', 0x49CA4E:'884302', 0x49CA55:'884303',
    }
    for site, encoded in checks.items(): expect(site,encoded)
    name_rva = rip(0xBF390,'488d05')
    name = bytes(image[name_rva:image.find(0,name_rva)]).decode()
    if name != 'materials/dev/reflectivity_90.vmat' or rip(0xBF39E,'488d05') != literal('FlatBinding'):
        raise SystemExit('Native flat material binding/provenance differs')
    _,world,_,_ = modules['worldrenderer.dll']
    if bytes(world[0x1E966:0x1E969]) != bytes.fromhex('488d15'):
        raise SystemExit('World descriptor lookup differs')
    target = 0x1E96D+struct.unpack_from('<i',world,0x1E969)[0]
    if bytes(world[target:world.find(0,target)]) != b'AggregateSceneObjectDesc':
        raise SystemExit('World descriptor name differs')
    print(json.dumps({'verified':True,'hashes':HASHES,'decoded_source_fields':
        {k:hex(v) for k,v in fields.items()},'flat_material':name},indent=2))

if __name__ == '__main__': main()
