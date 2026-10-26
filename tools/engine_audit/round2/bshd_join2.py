import sys, os, struct, collections
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
import lsf
pak = os.path.join(lsf.DATA, "Materials.pak")
idx = lsf.pak_index(pak)
caps = {c: set(f.split(".")[0] for f in os.listdir(r"C:\Users\quali\Documents\Code Projects\BG3RTX\captures\%s\shaders" % c)) for c in ("20261026-094651","20261026-115620","20261026-122703")}
have = set().union(*caps.values())
def fnv(b):
    h = 1469598103934665603
    for x in b: h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h
names = [n for n in idx if n.endswith("_Vulkan.bshd") and any(p in n for p in ("_DEF_", "_EMI_", "_FOR_"))]
print("candidates", len(names), flush=True)
out = open("bshd_map.tsv", "w")
matched = collections.defaultdict(set)
for k, n in enumerate(names):
    d = lsf.pak_read(pak, idx[n]); i = 0
    while True:
        i = d.find(b"\x03\x02\x23\x07", i)
        if i < 0: break
        sz = struct.unpack_from("<I", d, i - 4)[0]
        j = d.find(b"\x03\x02\x23\x07", i + 4)
        end = i + sz if 20 < sz <= len(d) - i else (j if j > 0 else len(d))
        h = "%016x" % fnv(d[i:end])
        out.write(f"{n}\t{i}\t{end-i}\t{h}\t{int(h in have)}\n")
        if h in have: matched[h].add(n)
        i += 4
    if k % 500 == 0: print(k, len(matched), flush=True)
print("unique capture hashes matched:", len(matched))
for c, s in caps.items(): print(c, "capture shaders", len(s), "matched", len(s & set(matched)))
for h in ("14678345","20864dc6","42a95e17","aa4efa4d","159a6211"):
    print(h, [sorted(v)[:3] for k, v in matched.items() if k.startswith(h)])
