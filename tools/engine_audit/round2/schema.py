"""schema.py BANK : union schema (node path -> attr -> count, sample, #distinct) across all resources of BANK in all files listed in roots.json."""
import sys, os, json, collections
sys.path.insert(0, r"C:\Users\quali\AppData\Local\Temp\bg3rtx_audit")
from lsf import DATA, pak_index, pak_read, parse_lsf
bank = sys.argv[1]
w = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "roots.json")))
cnt = collections.Counter(); samp = {}; dist = collections.defaultdict(set); nres = 0; names = []
idxs = {}
for pak, path, n in w[bank]:
    pp = os.path.join(DATA, pak)
    if pak not in idxs: idxs[pak] = pak_index(pp)
    ver, nodes = parse_lsf(pak_read(pp, idxs[pak][path]))
    def walk(i, p):
        nd = nodes[i]; q = p + "/" + nd["name"]
        cnt[(q, None)] += 1
        for k, v in nd["attrs"].items():
            cnt[(q, k)] += 1; samp.setdefault((q, k), v); dist[(q, k)].add(json.dumps(v)[:60])
        for c in nd["children"]: walk(c, q)
    for r in [i for i, x in enumerate(nodes) if x["parent"] < 0]:
        if nodes[r]["name"] != bank: continue
        for res in nodes[r]["children"]:
            nres += 1; names.append(nodes[res]["attrs"].get("Name")); walk(res, "")
print("resources", nres)
for (q, k), c in sorted(cnt.items(), key=lambda x: (x[0][0], x[0][1] or "")):
    if k is None: print(f"{q}  [{c}]")
    else: print(f"    {k} n={c} distinct={len(dist[(q,k)])} e.g. {json.dumps(samp[(q,k)])[:120]}")
if "-n" in sys.argv: print(names)
