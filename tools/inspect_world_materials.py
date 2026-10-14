"""Read-only PE string/RIP-reference investigation; no process opened or modified."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'logs/research/python-deps'))
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

def mapped(path):
    raw = path.read_bytes()
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    count, optsize = struct.unpack_from('<H', raw, pe+6)[0], struct.unpack_from('<H', raw, pe+20)[0]
    base = struct.unpack_from('<Q', raw, pe+24+24)[0]
    image = bytearray(struct.unpack_from('<I', raw, pe+24+56)[0])
    code = []
    for n in range(count):
        s = pe+24+optsize+40*n
        size, va, rawsize, offset = struct.unpack_from('<4I', raw, s+8)
        image[va:va+rawsize] = raw[offset:offset+rawsize]
        if struct.unpack_from('<I', raw, s+36)[0] & 0x20000000:
            code.append((va, rawsize))
    return raw, image, base, code

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--string', action='append', default=[])
    parser.add_argument('--disasm', action='append', default=[], help='RVA:size')
    parser.add_argument('--refs', action='append', default=[], help='Target RVA')
    parser.add_argument('--function', action='append', default=[], help='Disassemble containing x64 unwind function')
    parser.add_argument('--calls', action='append', default=[], help='Direct rel32 call target RVA')
    parser.add_argument('--vtable', action='append', default=[], help='Pointer slot RVA; find preceding RTTI locator')
    parser.add_argument('--rtti', action='append', default=[], help='RTTI exact class suffix name')
    args = parser.parse_args()
    raw, image, base, code = mapped(args.binary)
    print(json.dumps({'path': str(args.binary), 'sha256': hashlib.sha256(raw).hexdigest(), 'base':hex(base), 'code':code}))
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    for name in args.rtti:
        encoded = f'.?AV{name}@@'.encode()+b'\0'
        typename = image.find(encoded)-16
        if typename < 0:
            continue
        needle = struct.pack('<I',typename)
        at = image.find(needle)
        while at >= 0:
            col = at-12
            if col > 0 and struct.unpack_from('<I',image,col)[0] == 1 and struct.unpack_from('<I',image,col+20)[0] == col:
                ptr = image.find(struct.pack('<Q',base+col))
                if ptr >= 0:
                    table = ptr+8
                    print('RTTI',name,'VTABLE',hex(table),'SLOT20',hex(struct.unpack_from('<Q',image,table+0x20)[0]-base))
            at = image.find(needle,at+1)
    for value in args.vtable:
        slot = int(value,0)
        for offset in range(0,0x180,8):
            col = struct.unpack_from('<Q',image,slot-offset)[0]-base
            if 0 < col < len(image)-24 and struct.unpack_from('<I',image,col)[0] == 1 and struct.unpack_from('<I',image,col+20)[0] == col:
                typename = struct.unpack_from('<I',image,col+12)[0]+16
                print('VTABLE',hex(slot-offset+8),'SLOT',hex(offset-8),'TYPE',bytes(image[typename:image.find(0,typename)]).decode(errors='replace'))
                break
    for value in args.calls:
        target = int(value,0)
        for va,size in code:
            at = image.find(b'\xe8',va,va+size)
            while at >= 0:
                if at+5+struct.unpack_from('<i',image,at+1)[0] == target:
                    print('CALL',hex(target),hex(at))
                at = image.find(b'\xe8',at+1,va+size)
    pe = struct.unpack_from('<I',raw,0x3c)[0]
    pdata, pdata_size = struct.unpack_from('<II',raw,pe+24+112+3*8)
    for value in args.function:
        address = int(value,0)
        for at in range(pdata,pdata+pdata_size,12):
            begin,end,unwind = struct.unpack_from('<III',image,at)
            if begin <= address < end:
                print('FUNCTION',hex(begin),hex(end),hex(unwind))
                args.disasm.append(f'{begin}:{end-begin}')
                break
    targets = {int(v,0) for v in args.refs}
    for name in args.string:
        encoded = name.encode() + b'\0'
        at = image.find(encoded)
        while at >= 0:
            print('STRING', repr(name), hex(at))
            targets.add(at)
            at = image.find(encoded, at+1)
    if targets:
        # Decode only near the exact rel32 displacement occurrences, avoiding
        # false linear decoding from padding/data in executable sections.
        for target in sorted(targets):
            for va, size in code:
                for at in range(va, va+size-4):
                    disp = struct.unpack_from('<i',image,at)[0]
                    if target-at-4 == disp:
                        for start in range(max(va,at-8),at):
                            ins = next(md.disasm(bytes(image[start:at+8]),start,count=1),None)
                            if ins and any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP and ins.address+ins.size+op.mem.disp==target for op in ins.operands):
                                print('REF',hex(target),hex(ins.address),ins.mnemonic,ins.op_str)
            ptr = struct.pack('<Q',base+target)
            at = image.find(ptr)
            while at >= 0:
                print('POINTER',hex(target),hex(at))
                at = image.find(ptr,at+1)
    for value in args.disasm:
        address, size = (int(v,0) for v in value.split(':'))
        for ins in md.disasm(bytes(image[address:address+size]),address):
            print(hex(ins.address), ins.bytes.hex(), ins.mnemonic, ins.op_str)

if __name__ == '__main__':
    main()
