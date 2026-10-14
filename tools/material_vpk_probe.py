"""Read-only minimal VPK index/extraction for installed material shader evidence."""
import argparse
import re
import struct
from pathlib import Path

def entries(path):
    data=path.read_bytes(); magic,version,tree=struct.unpack_from('<III',data)
    if magic!=0x55aa1234 or version not in (1,2): raise ValueError('Not a VPK index')
    header=12 if version==1 else 28; at=header
    def string():
        nonlocal at
        end=data.index(0,at); s=data[at:end].decode(); at=end+1; return s
    while (extension:=string()):
        while (folder:=string()):
            while (name:=string()):
                crc,preload,archive,offset,length,terminator=struct.unpack_from('<IHHIIH',data,at);at+=18
                if terminator!=0xffff:raise ValueError('Bad VPK entry')
                head=data[at:at+preload];at+=preload
                filename=(folder+'/' if folder!=' ' else '')+name+'.'+extension
                yield filename,archive,offset,length,head,header+tree

def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('index',type=Path)
    parser.add_argument('--match',default='sky|reflectivity|complex|csgo_lightmapped|lightmapped')
    parser.add_argument('--extract');parser.add_argument('--out',type=Path);args=parser.parse_args()
    for filename,archive,offset,length,head,start in entries(args.index):
        if args.extract==filename:
            file=args.index if archive==0x7fff else args.index.with_name(args.index.name.replace('_dir.vpk',f'_{archive:03}.vpk'))
            with file.open('rb')as stream:stream.seek(offset+(start if archive==0x7fff else 0));content=head+stream.read(length)
            if args.out:args.out.write_bytes(content)
            else:print('\n'.join(s.decode(errors='replace')for s in re.findall(rb'[ -~]{4,120}',content)))
            break
        elif not args.extract and re.search(args.match,filename,re.I) and filename.endswith(('.vcs','.vmat_c')):
            print(filename,archive,offset,length)

if __name__=='__main__':main()
