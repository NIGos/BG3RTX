"""Read header of one Granite .gts and one .gtp from VirtualTextures*.pak (partial reads only)."""
import sys, os, struct, uuid, collections
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
from lsf import DATA, pak_index
pak = os.path.join(DATA, "VirtualTextures.pak"); idx = pak_index(pak)
def rd(name, off, n):
    o, part, fl, disk, us = idx[name]; assert fl & 0xF == 0
    p = pak if part == 0 else pak[:-4] + f"_{part}.pak"
    with open(p, "rb") as f: f.seek(o + off); return f.read(n)
G = "Generated/Public/VirtualTextures/Albedo_Normal_Physical_0.gts"
h = rd(G, 0, 512)
fmt = "<III16sIQIQiiiIIQIIIQIIIIIIIIIQIQIQQIIII"
v = struct.unpack_from(fmt, h, 0)
names = "Magic Version Unused GUID NumLayers LayersOffset NumLevels LevelsOffset TileWidth TileHeight TileBorder I2 NumFlatTileInfos FlatTileInfoOffset I6 I7 NumPackedTileIDs PackedTileIDsOffset M N O P Q R S PageSize NumPageFiles PageFileMetadataOffset FourCCListSize FourCCListOffset ParameterBlockHeadersCount ParameterBlockHeadersOffset ThumbnailsOffset XJJ XKK XLL XMM".split()
H = dict(zip(names, v)); H["Magic"] = struct.pack("<I", H["Magic"]); H["GUID"] = str(uuid.UUID(bytes_le=H["GUID"]))
for k, x in H.items(): print(f"  {k} = {x}")
L = rd(G, H["LayersOffset"], 8 * H["NumLayers"])
DT = {0:"R8G8B8_SRGB",1:"R8G8B8A8_SRGB",2:"X8Y8Z0_TANGENT",3:"R8G8B8_LINEAR",4:"R8G8B8A8_LINEAR",5:"X8",6:"X8Y8",7:"X8Y8Z8",8:"X8Y8Z8W8"}
for i in range(H["NumLayers"]):
    dt, dc = struct.unpack_from("<II", L, 8 * i); print(f"  layer{i}: DataType={dt}({DT.get(dt)}) default={dc:#010x}")
Lv = rd(G, H["LevelsOffset"], 16 * H["NumLevels"])
print("  levels:", [struct.unpack_from("<II", Lv, 16*i) for i in range(H["NumLevels"])])
PB = rd(G, H["ParameterBlockHeadersOffset"], 20 * H["ParameterBlockHeadersCount"])
codecs = collections.Counter(); fourcc = collections.Counter(); first = None
CN = {0:"Uniform",1:"Color420",2:"Normal",3:"RawColor",4:"Binary",8:"Half",9:"BC",10:"MultiChannel",11:"ASTC"}
for i in range(H["ParameterBlockHeadersCount"]):
    bid, codec, size, foff = struct.unpack_from("<IIIQ", PB, 20 * i); codecs[CN.get(codec, codec)] += 1
    if codec == 9:
        blk = rd(G, foff, size)
        ver = struct.unpack_from("<H", blk, 0)[0]; c1 = blk[2:18].split(b"\0")[0]; c2 = blk[18:34].split(b"\0")[0]
        B, C1, C2, F3, DTp, D, FCC = struct.unpack_from("<IBBBBHI", blk, 34)
        fourcc[(c1, c2, struct.pack("<I", FCC), DTp)] += 1
print("  param blocks by codec", codecs, "BC variants", fourcc.most_common(8))
PF = rd(G, H["PageFileMetadataOffset"], 536 * min(3, H["NumPageFiles"]))
for i in range(min(3, H["NumPageFiles"])):
    nm = PF[536*i:536*i+512].decode("utf-16-le", "replace").split("\0")[0]; npg = struct.unpack_from("<I", PF, 536*i+512)[0]
    print("  pagefile", nm, "pages", npg)
FC = rd(G, H["FourCCListOffset"], min(H["FourCCListSize"], 400)); print("  fourcc list head", FC[:200])
gtp = [k for k in idx if k.endswith(".gtp")][0]
gh = rd(gtp, 0, 64); print(gtp, idx[gtp][4] or idx[gtp][3], "GTP magic", gh[:4], "ver", struct.unpack_from("<I", gh, 4)[0], "guid", uuid.UUID(bytes_le=gh[8:24]))
# chunk header at offset of first page? print next words
print("  gtp bytes 24..64", gh[24:64].hex(" ", 4))
