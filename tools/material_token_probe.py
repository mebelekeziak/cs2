"""Read-only installed CUtlStringToken algorithm projection and token reference lookup."""
import argparse
import struct
from pathlib import Path
from verify_world_materials import mapped

def token(name):
    data=name.lower().encode('ascii'); m=0x5bd1e995; mask=0xffffffff
    value=0x31415926 ^ len(data); at=0
    while len(data)-at >= 4:
        key=struct.unpack_from('<I',data,at)[0]; key=key*m&mask; key^=key>>24; key=key*m&mask
        value=(value*m&mask)^key; at+=4
    if at<len(data):
        value ^= int.from_bytes(data[at:],'little'); value=value*m&mask
    value^=value>>13; value=value*m&mask; value^=value>>15
    return value

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('names',nargs='+'); args=parser.parse_args()
    for name in args.names:
        print(name,hex(token(name)))
