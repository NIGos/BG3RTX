"""Minimal read-only LSPK + LSF reader for audit purposes (BG3 LSPK v18, LSF v5-v7)."""
import struct, sys, os, json, io, lz4.block, lz4.frame

DATA = r"D:\SteamLibrary\steamapps\common\Baldurs Gate 3\Data"

def pak_index(pak):
    with open(pak, "rb") as f:
        hdr = f.read(40)
        magic, ver, off, size = struct.unpack_from("<4sIQI", hdr, 0)
        f.seek(off); n, cs = struct.unpack("<II", f.read(8))
        raw = lz4.block.decompress(f.read(cs), uncompressed_size=n * 272)
    idx = {}
    for i in range(n):
        e = raw[i*272:(i+1)*272]
        name = e[:256].split(b"\0", 1)[0].decode("utf-8", "replace")
        lo, hi, part, fl, disk, us = struct.unpack_from("<IHBBII", e, 256)
        idx[name] = (lo | (hi << 32), part, fl, disk, us)
    return idx

def pak_read(pak, ent):
    off, part, fl, disk, us = ent
    p = pak if part == 0 else pak[:-4] + f"_{part}.pak"
    with open(p, "rb") as f:
        f.seek(off); d = f.read(disk)
    m = fl & 0x0F
    if m == 0: return d
    if m == 1:
        import zlib; return zlib.decompress(d)
    if m == 2:
        try: return lz4.block.decompress(d, uncompressed_size=us)
        except Exception: return lz4.frame.decompress(d)
    if m == 3:
        import zstandard; return zstandard.ZstdDecompressor().decompress(d, max_output_size=us)
    raise ValueError(m)

def _dec(d, usize, flags, chunked):
    if usize == 0: return b""
    m = flags & 0x0F
    if m == 0: return d
    if m == 1:
        import zlib; return zlib.decompress(d)
    if m == 2:
        if chunked: return lz4.frame.decompress(d)
        return lz4.block.decompress(d, uncompressed_size=usize)
    if m == 3:
        import zstandard; return zstandard.ZstdDecompressor().decompress(d, max_output_size=usize)
    raise ValueError(m)

TYPES = {1:"B",2:"h",3:"H",4:"i",5:"I",6:"f",7:"d",8:"2i",9:"3i",10:"4i",11:"2f",12:"3f",13:"4f",
         14:"4f",15:"9f",16:"12f",17:"12f",18:"16f",19:"?",24:"Q",26:"q",27:"b",32:"q"}

def parse_lsf(b):
    assert b[:4] == b"LSOF", b[:4]
    ver = struct.unpack_from("<I", b, 4)[0]
    p = 8
    if ver >= 5: p += 8  # engine version int64
    else: p += 4
    has_keys = ver >= 6
    fields = ["su","sd"] + (["ku","kd"] if has_keys else []) + ["nu","nd","au","ad","vu","vd"]
    vals = struct.unpack_from("<" + "I"*len(fields), b, p); p += 4*len(fields)
    md = dict(zip(fields, vals))
    cflags, _, _, mfmt = struct.unpack_from("<BBHI", b, p); p += 8
    chunked = ver >= 2
    def take(uk, dk, ch):
        nonlocal p
        u, dsz = md[uk], md[dk]
        sz = dsz if dsz else u
        raw = b[p:p+sz]; p += sz
        return raw if dsz == 0 else _dec(raw, u, cflags, ch)
    names_raw = take("su","sd", False)
    nodes_raw = take("nu","nd", chunked)
    attrs_raw = take("au","ad", chunked)
    vals_raw = take("vu","vd", chunked)
    keys_raw = take("ku","kd", chunked) if has_keys else b""
    # names
    q = 0; nb = struct.unpack_from("<I", names_raw, q)[0]; q += 4
    names = []
    for _ in range(nb):
        cnt = struct.unpack_from("<H", names_raw, q)[0]; q += 2
        bucket = []
        for _ in range(cnt):
            l = struct.unpack_from("<H", names_raw, q)[0]; q += 2
            bucket.append(names_raw[q:q+l].decode("utf-8","replace")); q += l
        names.append(bucket)
    nm = lambda h: names[h >> 16][h & 0xFFFF]
    ext = (mfmt == 1)
    nodes = []
    if ext:
        for i in range(len(nodes_raw)//16):
            h, par, sib, fa = struct.unpack_from("<IiiI", nodes_raw, i*16)
            nodes.append(dict(name=nm(h), parent=par, first=struct.unpack("<i",struct.pack("<I",fa))[0], attrs={}, children=[]))
        attrs = []
        for i in range(len(attrs_raw)//16):
            h, tl, nxt, off = struct.unpack_from("<IIiI", attrs_raw, i*16)
            attrs.append((nm(h), tl & 0x3F, tl >> 6, nxt, off))
        for nd in nodes:
            a = nd["first"]
            while a >= 0:
                n_, t, l, nxt, off = attrs[a]
                nd["attrs"][n_] = decode_val(t, vals_raw[off:off+l]); a = nxt
    else:
        for i in range(len(nodes_raw)//12):
            h, fa, par = struct.unpack_from("<Iii", nodes_raw, i*12)
            nodes.append(dict(name=nm(h), parent=par, first=fa, attrs={}, children=[]))
        off = 0
        for i in range(len(attrs_raw)//12):
            h, tl, ni = struct.unpack_from("<IIi", attrs_raw, i*12)
            t, l = tl & 0x3F, tl >> 6
            nodes[ni]["attrs"][nm(h)] = decode_val(t, vals_raw[off:off+l]); off += l
    for i, nd in enumerate(nodes):
        if nd["parent"] >= 0: nodes[nd["parent"]]["children"].append(i)
    return ver, nodes

def decode_val(t, d):
    try:
        if t in TYPES:
            v = struct.unpack("<" + TYPES[t], d[:struct.calcsize("<"+TYPES[t])])
            return v[0] if len(v) == 1 else [round(x, 5) if isinstance(x, float) else x for x in v]
        if t in (20, 21, 22, 23, 29, 30):
            return d.split(b"\0",1)[0].decode("utf-8","replace")
        if t == 31:
            return d.hex()
        if t in (28, 33):
            # BG3 translated string: u16 version, u32 len, handle
            try:
                ver = struct.unpack_from("<H", d, 0)[0]; l = struct.unpack_from("<I", d, 2)[0]
                return "TS:" + d[6:6+l].split(b"\0",1)[0].decode("utf-8","replace")
            except Exception: return "TS?"
        return f"<t{t} {len(d)}B>"
    except Exception as ex:
        return f"<err t{t}>"

def to_tree(nodes, i=0, depth=0, maxdepth=99):
    nd = nodes[i]
    out = {"_": nd["name"], **nd["attrs"]}
    if depth < maxdepth and nd["children"]:
        out["children"] = [to_tree(nodes, c, depth+1, maxdepth) for c in nd["children"]]
    return out

if __name__ == "__main__":
    pak, path = sys.argv[1], sys.argv[2]
    pp = os.path.join(DATA, pak)
    idx = pak_index(pp)
    b = pak_read(pp, idx[path])
    if b[:4] != b"LSOF":
        sys.stdout.write(b[:4000].decode("utf-8","replace")); sys.exit()
    ver, nodes = parse_lsf(b)
    roots = [i for i, n in enumerate(nodes) if n["parent"] < 0]
    print(json.dumps({"ver": ver, "n": len(nodes), "roots": [to_tree(nodes, r) for r in roots]}, indent=1)[: int(sys.argv[3]) if len(sys.argv) > 3 else 20000])
