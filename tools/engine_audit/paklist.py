import struct, sys, os, json, lz4.block
def read_lspk(path):
    with open(path,"rb") as f:
        hdr=f.read(40)
        magic,ver,off,size=struct.unpack_from("<4sIQI",hdr,0)
        flags,prio=hdr[20],hdr[21]
        nparts=struct.unpack_from("<H",hdr,38)[0]
        f.seek(off); n,cs=struct.unpack("<II",f.read(8))
        raw=lz4.block.decompress(f.read(cs),uncompressed_size=n*272)
    out=[]
    for i in range(n):
        e=raw[i*272:(i+1)*272]
        name=e[:256].split(b"\0",1)[0].decode("utf-8","replace")
        lo,hi,part,fl,disk,us=struct.unpack_from("<IHBBII",e,256)
        out.append((name,lo|(hi<<32),part,fl,disk,us))
    return dict(ver=ver,flags=flags,prio=prio,nparts=nparts,files=out)
d=sys.argv[1]; outd=sys.argv[2]
summ={}
for p in sorted(os.listdir(d)):
    if not p.endswith(".pak"): continue
    try:
        r=read_lspk(os.path.join(d,p))
    except Exception as ex:
        summ[p]=str(ex); continue
    with open(os.path.join(outd,p+".txt"),"w",encoding="utf-8") as f:
        for e in r["files"]: f.write(f"{e[0]}\t{e[2]}\t{e[3]}\t{e[4]}\t{e[5]}\n")
    summ[p]=dict(ver=r["ver"],flags=r["flags"],nparts=r["nparts"],n=len(r["files"]))
print(json.dumps(summ,indent=0))
