import sys, os, struct
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
import lsf
pak = os.path.join(lsf.DATA, "Materials.pak")
idx = lsf.pak_index(pak)
caps = [r"C:\Users\quali\Documents\Code Projects\BG3RTX\captures\%s\shaders" % c for c in ("20261026-094651","20261026-115620","20261026-122703")]
have = set()
for c in caps:
    for f in os.listdir(c): have.add(f.split(".")[0])
def fnv(b):
    h = 1469598103934665603
    for x in b: h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h
names = [n for n in idx if n.endswith("_Vulkan.bshd")]
print("vulkan bshd in Materials.pak:", len(names))
hits = 0; tot = 0; sample = []
import random; random.seed(1)
for n in names[:400] + [x for x in names if "Base_VT_ST_DEF" in x]:
    d = lsf.pak_read(pak, idx[n])
    i = 0
    while True:
        i = d.find(b"\x03\x02\x23\x07", i)
        if i < 0: break
        # SPIR-V header: magic, version, generator, bound, schema; length unknown -> try next magic or end
        j = d.find(b"\x03\x02\x23\x07", i + 4)
        # also size may be stored just before
        sz = struct.unpack_from("<I", d, i - 4)[0] if i >= 4 else 0
        end = i + sz if 20 < sz <= len(d) - i else (j if j > 0 else len(d))
        blob = d[i:end]
        h = "%016x" % fnv(blob); tot += 1
        if h in have: hits += 1; sample.append((n, h))
        i += 4
print("spirv blobs", tot, "matched capture hashes", hits)
for s in sample[:10]: print(s)
